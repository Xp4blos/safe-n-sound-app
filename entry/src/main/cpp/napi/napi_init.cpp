// NAPI bridge between ArkTS and the Safe'n'Sound sound engine. Thin on purpose: argument checking and
// conversion only; all signal processing lives in ../ambient, ../profile and ../wrapper.
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "hilog/log.h"
#include "napi/native_api.h"

#include "ambient/speech.hpp"
#include "sound_engine.h"

namespace {

constexpr unsigned int kLogDomain = 0x3201;
constexpr const char* kLogTag = "SnsNative";

std::mutex g_mutex;
std::map<int, std::unique_ptr<sns::SoundEngine>> g_engines;
int g_nextHandle = 1;
std::unique_ptr<ambient::SpeechPipeline> g_speech;  // keyword alerts for the text of the speech recogniser

napi_value Undefined(napi_env env) {
    napi_value v;
    napi_get_undefined(env, &v);
    return v;
}

napi_value Throw(napi_env env, const char* message) {
    napi_throw_error(env, nullptr, message);
    return nullptr;
}

napi_value MakeNumber(napi_env env, double d) {
    napi_value v;
    napi_create_double(env, d, &v);
    return v;
}

napi_value MakeBool(napi_env env, bool b) {
    napi_value v;
    napi_get_boolean(env, b, &v);
    return v;
}

napi_value MakeString(napi_env env, const std::string& s) {
    napi_value v;
    napi_create_string_utf8(env, s.c_str(), s.size(), &v);
    return v;
}

napi_value MakeArrayBuffer(napi_env env, const std::vector<uint8_t>& bytes) {
    void* data = nullptr;
    napi_value buffer;
    napi_create_arraybuffer(env, bytes.size(), &data, &buffer);
    if (!bytes.empty() && data != nullptr) std::memcpy(data, bytes.data(), bytes.size());
    return buffer;
}

void SetProp(napi_env env, napi_value obj, const char* name, napi_value value) {
    napi_set_named_property(env, obj, name, value);
}

bool ReadHandle(napi_env env, napi_value value, int* handle) {
    int32_t h = 0;
    if (napi_get_value_int32(env, value, &h) != napi_ok) return false;
    *handle = h;
    return true;
}

bool ReadString(napi_env env, napi_value value, std::string* out) {
    size_t length = 0;
    if (napi_get_value_string_utf8(env, value, nullptr, 0, &length) != napi_ok) return false;
    std::string s(length, '\0');
    size_t copied = 0;
    if (napi_get_value_string_utf8(env, value, &s[0], length + 1, &copied) != napi_ok) return false;
    s.resize(copied);
    *out = s;
    return true;
}

// Copies the bytes of an ArrayBuffer (the source may not be aligned, so never alias it).
bool ReadBytes(napi_env env, napi_value value, std::vector<uint8_t>* out) {
    void* data = nullptr;
    size_t length = 0;
    if (napi_get_arraybuffer_info(env, value, &data, &length) != napi_ok) return false;
    out->resize(length);
    if (length > 0) std::memcpy(out->data(), data, length);
    return true;
}

// 16-bit little-endian PCM from an ArrayBuffer; an odd trailing byte is ignored and logged.
bool ReadPcm(napi_env env, napi_value value, std::vector<int16_t>* out) {
    std::vector<uint8_t> bytes;
    if (!ReadBytes(env, value, &bytes)) return false;
    if (bytes.size() % 2 != 0) {
        OH_LOG_Print(LOG_APP, LOG_WARN, kLogDomain, kLogTag, "odd PCM byte length %{public}zu, ignoring last byte",
                     bytes.size());
    }
    out->resize(bytes.size() / 2);
    if (!out->empty()) std::memcpy(out->data(), bytes.data(), out->size() * 2);
    return true;
}

napi_value ProfileToJs(napi_env env, const sns::SoundProfile& p) {
    napi_value obj, env8;
    napi_create_object(env, &obj);
    SetProp(env, obj, "dominantHz", MakeNumber(env, p.dominantHz));
    SetProp(env, obj, "durationSec", MakeNumber(env, p.durationSec));
    SetProp(env, obj, "beepCount", MakeNumber(env, p.beepCount));
    SetProp(env, obj, "beepsPerSec", MakeNumber(env, p.beepsPerSec));
    SetProp(env, obj, "repetition", MakeNumber(env, p.repetition));
    SetProp(env, obj, "modulation", MakeNumber(env, p.modulation));
    napi_create_array_with_length(env, p.envelope.size(), &env8);
    for (size_t i = 0; i < p.envelope.size(); ++i) {
        napi_set_element(env, env8, static_cast<uint32_t>(i), MakeNumber(env, p.envelope[i]));
    }
    SetProp(env, obj, "envelope", env8);
    return obj;
}

napi_value LearnResultToJs(napi_env env, const sns::LearnResult& r) {
    napi_value obj;
    napi_create_object(env, &obj);
    SetProp(env, obj, "ok", MakeBool(env, r.ok));
    SetProp(env, obj, "message", MakeString(env, r.message));
    SetProp(env, obj, "template", MakeArrayBuffer(env, r.templateBytes));
    SetProp(env, obj, "profile", ProfileToJs(env, r.profile));
    SetProp(env, obj, "consistency", MakeNumber(env, r.consistency));
    SetProp(env, obj, "droppedTake", MakeNumber(env, r.droppedTake));
    return obj;
}

napi_value EventToJs(napi_env env, const sns::EngineEvent& e) {
    napi_value obj;
    napi_create_object(env, &obj);
    SetProp(env, obj, "type", MakeString(env, e.type));
    SetProp(env, obj, "timeSec", MakeNumber(env, e.timeSec));
    SetProp(env, obj, "startSec", MakeNumber(env, e.startSec));
    SetProp(env, obj, "confidence", MakeNumber(env, e.confidence));
    SetProp(env, obj, "freqHz", MakeNumber(env, e.freqHz));
    SetProp(env, obj, "levelDb", MakeNumber(env, e.levelDb));
    SetProp(env, obj, "label", MakeString(env, e.label));
    return obj;
}

// Runs `fn(engine)` under the lock; throws a JS error for an unknown handle.
template <typename Fn>
napi_value WithEngine(napi_env env, int handle, Fn fn) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_engines.find(handle);
    if (it == g_engines.end()) return Throw(env, "unknown engine handle");
    return fn(*it->second);
}

napi_value CreateEngine(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    int32_t sampleRate = 0;
    if (argc < 1 || napi_get_value_int32(env, args[0], &sampleRate) != napi_ok || sampleRate < 8000 ||
        sampleRate > 48000) {
        return Throw(env, "createEngine: sampleRate must be an integer between 8000 and 48000");
    }
    try {
        std::lock_guard<std::mutex> lock(g_mutex);
        const int handle = g_nextHandle++;
        g_engines[handle] = std::make_unique<sns::SoundEngine>(sampleRate);
        return MakeNumber(env, handle);
    } catch (...) {
        return Throw(env, "createEngine: could not create the engine");
    }
}

napi_value DestroyEngine(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    int handle = 0;
    if (argc < 1 || !ReadHandle(env, args[0], &handle)) return Throw(env, "destroyEngine: bad handle");
    std::lock_guard<std::mutex> lock(g_mutex);
    g_engines.erase(handle);
    return Undefined(env);
}

napi_value Process(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    int handle = 0;
    if (argc < 2 || !ReadHandle(env, args[0], &handle)) return Throw(env, "process: bad handle");
    std::vector<int16_t> pcm;
    if (!ReadPcm(env, args[1], &pcm)) return Throw(env, "process: second argument must be an ArrayBuffer");
    return WithEngine(env, handle, [&](sns::SoundEngine& engine) -> napi_value {
        const sns::ProcessResult out = engine.Process(pcm.data(), pcm.size());
        napi_value result, events, bands;
        napi_create_object(env, &result);
        napi_create_array_with_length(env, out.events.size(), &events);
        for (size_t i = 0; i < out.events.size(); ++i) {
            napi_set_element(env, events, static_cast<uint32_t>(i), EventToJs(env, out.events[i]));
        }
        napi_create_array_with_length(env, out.bands.size(), &bands);
        for (size_t i = 0; i < out.bands.size(); ++i) {
            napi_set_element(env, bands, static_cast<uint32_t>(i), MakeNumber(env, out.bands[i]));
        }
        SetProp(env, result, "levelDb", MakeNumber(env, out.levelDb));
        SetProp(env, result, "bands", bands);
        SetProp(env, result, "events", events);
        return result;
    });
}

napi_value LearnSound(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    int handle = 0;
    std::string label;
    double eventTimeSec = 0.0;
    if (argc < 3 || !ReadHandle(env, args[0], &handle) || !ReadString(env, args[1], &label) ||
        napi_get_value_double(env, args[2], &eventTimeSec) != napi_ok) {
        return Throw(env, "learnSound: expected (handle, label, eventTimeSec)");
    }
    return WithEngine(env, handle, [&](sns::SoundEngine& engine) -> napi_value {
        return LearnResultToJs(env, engine.LearnFromRing(label, eventTimeSec));
    });
}

napi_value TrainSound(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    int handle = 0;
    std::string label;
    bool isArray = false;
    if (argc < 3 || !ReadHandle(env, args[0], &handle) || !ReadString(env, args[1], &label) ||
        napi_is_array(env, args[2], &isArray) != napi_ok || !isArray) {
        return Throw(env, "trainSound: expected (handle, label, takes: ArrayBuffer[])");
    }
    uint32_t count = 0;
    napi_get_array_length(env, args[2], &count);
    std::vector<std::vector<int16_t>> takes;
    for (uint32_t i = 0; i < count; ++i) {
        napi_value item;
        std::vector<int16_t> pcm;
        if (napi_get_element(env, args[2], i, &item) != napi_ok || !ReadPcm(env, item, &pcm)) {
            return Throw(env, "trainSound: every take must be an ArrayBuffer");
        }
        takes.push_back(std::move(pcm));
    }
    return WithEngine(env, handle, [&](sns::SoundEngine& engine) -> napi_value {
        return LearnResultToJs(env, engine.TrainFromTakes(label, takes));
    });
}

napi_value CheckTake(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    int handle = 0;
    std::vector<int16_t> pcm;
    if (argc < 2 || !ReadHandle(env, args[0], &handle) || !ReadPcm(env, args[1], &pcm)) {
        return Throw(env, "checkTake: expected (handle, take: ArrayBuffer)");
    }
    return WithEngine(env, handle, [&](sns::SoundEngine& engine) -> napi_value {
        return LearnResultToJs(env, engine.CheckTake(pcm));
    });
}

napi_value AddSound(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    int handle = 0;
    std::vector<uint8_t> bytes;
    if (argc < 2 || !ReadHandle(env, args[0], &handle) || !ReadBytes(env, args[1], &bytes)) {
        return Throw(env, "addSound: expected (handle, template: ArrayBuffer)");
    }
    return WithEngine(env, handle, [&](sns::SoundEngine& engine) -> napi_value {
        std::string error;
        const bool ok = engine.AddSound(bytes, &error);
        if (!ok) {
            OH_LOG_Print(LOG_APP, LOG_WARN, kLogDomain, kLogTag, "stored sound rejected: %{public}s", error.c_str());
        }
        return MakeBool(env, ok);
    });
}

napi_value RemoveSound(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    int handle = 0;
    std::string label;
    if (argc < 2 || !ReadHandle(env, args[0], &handle) || !ReadString(env, args[1], &label)) {
        return Throw(env, "removeSound: expected (handle, label)");
    }
    return WithEngine(env, handle, [&](sns::SoundEngine& engine) -> napi_value {
        return MakeBool(env, engine.RemoveSound(label));
    });
}

// ---- speech: captions and keyword alerts (the text comes from the recogniser on the ArkTS side) -------------------

bool OptProp(napi_env env, napi_value obj, const char* name, napi_value* out) {
    napi_valuetype type = napi_undefined;
    if (napi_typeof(env, obj, &type) != napi_ok || type != napi_object) return false;
    bool has = false;
    if (napi_has_named_property(env, obj, name, &has) != napi_ok || !has) return false;
    napi_value v;
    if (napi_get_named_property(env, obj, name, &v) != napi_ok) return false;
    napi_valuetype vt = napi_undefined;
    napi_typeof(env, v, &vt);
    if (vt == napi_undefined || vt == napi_null) return false;
    *out = v;
    return true;
}

napi_value PatternToJs(napi_env env, const ambient::VibrationPattern& p) {
    napi_value arr;
    napi_create_array_with_length(env, p.ms.size(), &arr);
    for (size_t i = 0; i < p.ms.size(); ++i) napi_set_element(env, arr, static_cast<uint32_t>(i), MakeNumber(env, p.ms[i]));
    return arr;
}

// Offsets inside the text are UTF-8 byte offsets in the engine; JavaScript strings are indexed in UTF-16 units.
double Utf16(const std::string& text, size_t byteOffset) {
    return static_cast<double>(ambient::utf8_to_utf16_offset(text, byteOffset));
}

napi_value CaptionToJs(napi_env env, const ambient::Caption& c) {
    napi_value o, hl;
    napi_create_object(env, &o);
    SetProp(env, o, "utteranceId", MakeNumber(env, c.utterance_id));
    SetProp(env, o, "isFinal", MakeBool(env, c.is_final));
    SetProp(env, o, "timeSec", MakeNumber(env, c.time_s));
    SetProp(env, o, "text", MakeString(env, c.text));
    napi_create_array_with_length(env, c.highlights.size(), &hl);
    for (size_t i = 0; i < c.highlights.size(); ++i) {
        const ambient::KeywordHit& h = c.highlights[i];
        napi_value ho;
        napi_create_object(env, &ho);
        SetProp(env, ho, "begin", MakeNumber(env, Utf16(c.text, h.begin)));
        SetProp(env, ho, "end", MakeNumber(env, Utf16(c.text, h.end)));
        SetProp(env, ho, "ruleId", MakeNumber(env, h.rule_id));
        SetProp(env, ho, "label", MakeString(env, h.label));
        napi_set_element(env, hl, static_cast<uint32_t>(i), ho);
    }
    SetProp(env, o, "highlights", hl);
    return o;
}

ambient::VibrationPattern PatternFromJs(napi_env env, napi_value v) {
    napi_valuetype type = napi_undefined;
    napi_typeof(env, v, &type);
    if (type == napi_string) {
        std::string s;
        ReadString(env, v, &s);
        if (s == "sos") return ambient::VibrationPattern::sos();
        if (s == "rapid") return ambient::VibrationPattern::rapid_burst();
        if (s == "long") return ambient::VibrationPattern::long_buzz();
        if (s == "double") return ambient::VibrationPattern::double_tap();
        throw std::invalid_argument("unknown vibration preset: " + s);
    }
    bool isArray = false;
    napi_is_array(env, v, &isArray);
    if (!isArray) throw std::invalid_argument("pattern must be an array of milliseconds or a preset name");
    uint32_t count = 0;
    napi_get_array_length(env, v, &count);
    if (count > ambient::VibrationPattern::kMaxSegments) throw std::invalid_argument("vibration pattern too long");
    ambient::VibrationPattern p;
    for (uint32_t i = 0; i < count; ++i) {
        napi_value item;
        double d = -1;
        napi_get_element(env, v, i, &item);
        napi_get_value_double(env, item, &d);
        if (!(d >= 0 && d <= ambient::VibrationPattern::kMaxSegmentMs)) throw std::invalid_argument("bad vibration segment");
        p.ms.push_back(static_cast<uint32_t>(d));
    }
    return p;
}

napi_value CreateSpeech(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    try {
        ambient::SpeechConfig cfg;
        napi_value v;
        if (argc >= 1 && OptProp(env, args[0], "useDefaultRules", &v)) napi_get_value_bool(env, v, &cfg.use_default_rules);
        if (argc >= 1 && OptProp(env, args[0], "triggerOnPartial", &v)) napi_get_value_bool(env, v, &cfg.trigger_on_partial);
        cfg.validate();
        std::lock_guard<std::mutex> lock(g_mutex);
        g_speech = std::make_unique<ambient::SpeechPipeline>(cfg, nullptr);  // vibration is done on the ArkTS side
        return Undefined(env);
    } catch (const std::exception& e) {
        return Throw(env, e.what());
    }
}

template <typename Fn>
napi_value WithSpeech(napi_env env, Fn fn) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_speech) return Throw(env, "speech pipeline not created: call createSpeech() first");
    try {
        return fn(*g_speech);
    } catch (const std::exception& e) {
        return Throw(env, e.what());
    }
}

napi_value SpeechResult(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    std::string text;
    bool isFinal = false;
    double timeSec = 0.0;
    if (argc < 3 || !ReadString(env, args[0], &text) || napi_get_value_bool(env, args[1], &isFinal) != napi_ok ||
        napi_get_value_double(env, args[2], &timeSec) != napi_ok) {
        return Throw(env, "speechResult: expected (text, isFinal, timeSec)");
    }
    return WithSpeech(env, [&](ambient::SpeechPipeline& speech) -> napi_value {
        const ambient::SpeechUpdate u = speech.on_result(text, isFinal, timeSec);
        napi_value out, alerts;
        napi_create_object(env, &out);
        SetProp(env, out, "caption", CaptionToJs(env, u.caption));
        napi_create_array_with_length(env, u.alerts.size(), &alerts);
        for (size_t i = 0; i < u.alerts.size(); ++i) {
            const ambient::KeywordAlert& k = u.alerts[i];
            napi_value ko;
            napi_create_object(env, &ko);
            SetProp(env, ko, "ruleId", MakeNumber(env, k.rule_id));
            SetProp(env, ko, "label", MakeString(env, k.label));
            SetProp(env, ko, "phrase", MakeString(env, k.phrase));
            SetProp(env, ko, "timeSec", MakeNumber(env, k.time_s));
            SetProp(env, ko, "begin", MakeNumber(env, Utf16(u.caption.text, k.begin)));
            SetProp(env, ko, "end", MakeNumber(env, Utf16(u.caption.text, k.end)));
            SetProp(env, ko, "pattern", PatternToJs(env, k.pattern));
            napi_set_element(env, alerts, static_cast<uint32_t>(i), ko);
        }
        SetProp(env, out, "alerts", alerts);
        return out;
    });
}

napi_value SpeechEndUtterance(napi_env env, napi_callback_info) {
    return WithSpeech(env, [&](ambient::SpeechPipeline& speech) -> napi_value {
        speech.end_utterance();
        return Undefined(env);
    });
}

napi_value SpeechReset(napi_env env, napi_callback_info) {
    return WithSpeech(env, [&](ambient::SpeechPipeline& speech) -> napi_value {
        speech.reset();
        return Undefined(env);
    });
}

napi_value SpeechAddRule(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    return WithSpeech(env, [&](ambient::SpeechPipeline& speech) -> napi_value {
        napi_value v;
        if (argc < 1 || !OptProp(env, args[0], "phrase", &v)) throw std::invalid_argument("rule.phrase is required");
        ambient::KeywordRule r;
        ReadString(env, v, &r.phrase);
        if (OptProp(env, args[0], "label", &v)) ReadString(env, v, &r.label);
        if (OptProp(env, args[0], "pattern", &v)) r.pattern = PatternFromJs(env, v);
        if (OptProp(env, args[0], "priority", &v)) napi_get_value_int32(env, v, &r.priority);
        double cooldown = r.cooldown_s;
        if (OptProp(env, args[0], "cooldownS", &v)) napi_get_value_double(env, v, &cooldown);
        r.cooldown_s = static_cast<float>(cooldown);
        return MakeNumber(env, speech.add_rule(std::move(r)));
    });
}

napi_value SpeechRemoveRule(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    std::string phrase;
    if (argc < 1 || !ReadString(env, args[0], &phrase)) return Throw(env, "speechRemoveRule: expected (phrase)");
    return WithSpeech(env, [&](ambient::SpeechPipeline& speech) -> napi_value {
        return MakeBool(env, speech.remove_rule(phrase));
    });
}

napi_value SpeechRules(napi_env env, napi_callback_info) {
    return WithSpeech(env, [&](ambient::SpeechPipeline& speech) -> napi_value {
        const auto rules = speech.rules();
        napi_value arr;
        napi_create_array_with_length(env, rules.size(), &arr);
        for (size_t i = 0; i < rules.size(); ++i) {
            napi_value o;
            napi_create_object(env, &o);
            SetProp(env, o, "phrase", MakeString(env, rules[i].phrase));
            SetProp(env, o, "label", MakeString(env, rules[i].label));
            SetProp(env, o, "pattern", PatternToJs(env, rules[i].pattern));
            SetProp(env, o, "priority", MakeNumber(env, rules[i].priority));
            SetProp(env, o, "cooldownS", MakeNumber(env, rules[i].cooldown_s));
            napi_set_element(env, arr, static_cast<uint32_t>(i), o);
        }
        return arr;
    });
}

napi_value SpeechHistory(napi_env env, napi_callback_info) {
    return WithSpeech(env, [&](ambient::SpeechPipeline& speech) -> napi_value {
        const auto& history = speech.history();
        napi_value arr;
        napi_create_array_with_length(env, history.size(), &arr);
        uint32_t i = 0;
        for (const ambient::Caption& c : history) napi_set_element(env, arr, i++, CaptionToJs(env, c));
        return arr;
    });
}

napi_value Init(napi_env env, napi_value exports) {
    napi_property_descriptor props[] = {
        {"createEngine", nullptr, CreateEngine, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"destroyEngine", nullptr, DestroyEngine, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"process", nullptr, Process, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"learnSound", nullptr, LearnSound, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"trainSound", nullptr, TrainSound, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"checkTake", nullptr, CheckTake, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"addSound", nullptr, AddSound, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"removeSound", nullptr, RemoveSound, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"createSpeech", nullptr, CreateSpeech, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"speechResult", nullptr, SpeechResult, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"speechEndUtterance", nullptr, SpeechEndUtterance, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"speechReset", nullptr, SpeechReset, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"speechAddRule", nullptr, SpeechAddRule, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"speechRemoveRule", nullptr, SpeechRemoveRule, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"speechRules", nullptr, SpeechRules, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"speechHistory", nullptr, SpeechHistory, nullptr, nullptr, nullptr, napi_default, nullptr},
    };
    napi_define_properties(env, exports, sizeof(props) / sizeof(props[0]), props);
    return exports;
}

napi_module g_module = {1, 0, nullptr, Init, "safensound", nullptr, {nullptr}};

}  // namespace

extern "C" __attribute__((constructor)) void RegisterSafensoundModule(void) {
    napi_module_register(&g_module);
}
