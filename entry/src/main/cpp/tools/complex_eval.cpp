// Dev tool: how does the engine cope with complex multi-note signals in real room noise?
// Mixes five synthetic signals (different pitches, spacings and dynamics, see tests/complex_sounds.h) into room noise
// recorded by the phone, at several levels above the noise, and measures, for the same flow the app uses:
//   detect   - is the signal reported as an alarm, and can it be auto-learned?
//   teach    - can it be taught from two takes (taught at 20 dB above the noise)?
//   recog    - how many of 3 repeats (random +-2 dB) are recognised under the right name?
//   confuse  - with all five taught, which names fire when each signal is played?
// Run from the repository root:  complex_eval.exe
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "complex_sounds.h"
#include "sound_engine.h"

using namespace complex_sounds;

namespace {

bool LoadWav(const char* path, std::vector<int16_t>* pcm) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::vector<unsigned char> bytes;
    unsigned char buf[8192];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) bytes.insert(bytes.end(), buf, buf + n);
    std::fclose(f);
    if (bytes.size() <= 44) return false;
    pcm->resize((bytes.size() - 44) / 2);
    std::memcpy(pcm->data(), bytes.data() + 44, pcm->size() * 2);
    return true;
}

struct Heard {
    int alarms = 0;
    std::map<std::string, int> customs;
};

Heard Feed(sns::SoundEngine& engine, const std::vector<int16_t>& pcm) {
    Heard h;
    for (size_t pos = 0; pos < pcm.size(); pos += 480) {
        const auto out = engine.Process(pcm.data() + pos, std::min<size_t>(480, pcm.size() - pos));
        for (const auto& e : out.events) {
            if (e.type == "alarm") ++h.alarms;
            else ++h.customs[e.label];
        }
    }
    return h;
}

// A 6 s take as the Teach flow records it: the signal starts 1.2 s after the recording starts.
std::vector<int16_t> Take(NoiseSource& noise, const std::vector<double>& sig, double snr) {
    const double len = static_cast<double>(sig.size()) / kSynthRate;
    return Scene(noise, sig, snr, 1.2, std::max(0.2, 6.0 - 1.2 - len));
}


// --export <dir>: writes the five signals as 16 kHz mono WAV files (RMS 0.45) for playback tests on a phone.
void WriteWav(const std::string& path, const std::vector<double>& sig) {
    // Equal average loudness for every signal (RMS 0.45), hard-limited at 0.98, so a decaying chime is as loud as a siren.
    const double rms = Rms(sig);
    std::vector<int16_t> pcm(sig.size());
    for (size_t i = 0; i < sig.size(); ++i) {
        const double v = rms > 0 ? 0.45 * sig[i] / rms : 0.0;
        pcm[i] = ToSample(std::max(-0.98, std::min(0.98, v)));
    }
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    const uint32_t dataBytes = static_cast<uint32_t>(pcm.size() * 2), rate = 16000, byteRate = rate * 2, fmtSize = 16;
    const uint16_t pcmFmt = 1, channels = 1, blockAlign = 2, bits = 16;
    const uint32_t riffSize = 36 + dataBytes;
    std::fwrite("RIFF", 1, 4, f); std::fwrite(&riffSize, 4, 1, f); std::fwrite("WAVEfmt ", 1, 8, f);
    std::fwrite(&fmtSize, 4, 1, f); std::fwrite(&pcmFmt, 2, 1, f); std::fwrite(&channels, 2, 1, f);
    std::fwrite(&rate, 4, 1, f); std::fwrite(&byteRate, 4, 1, f); std::fwrite(&blockAlign, 2, 1, f);
    std::fwrite(&bits, 2, 1, f); std::fwrite("data", 1, 4, f); std::fwrite(&dataBytes, 4, 1, f);
    std::fwrite(pcm.data(), 2, pcm.size(), f);
    std::fclose(f);
}

}  // namespace

int main(int argc, char** argv) {
    // --recog <wav> <25 onsets, 5 per sound in All() order>: teaches every sound from its first three plays (cut like
    // Teach takes), replays the whole recording and prints which learned sound fired after each of the 25 plays.
    if (argc == 28 && std::strcmp(argv[1], "--recog") == 0) {
        std::vector<int16_t> rec;
        if (!LoadWav(argv[2], &rec)) return 1;
        const auto& snds = All();
        std::vector<double> on;
        for (int i = 3; i < 28; ++i) on.push_back(std::atof(argv[i]));
        sns::SoundEngine engine(sns::kSampleRate);
        for (size_t k = 0; k < snds.size(); ++k) {
            std::vector<std::vector<int16_t>> takes;
            for (size_t j = 0; j < 3; ++j) {
                const long from = std::lround((on[k * 5 + j] - 1.2) * 16000.0);
                takes.emplace_back(rec.begin() + from, rec.begin() + from + 96000);
            }
            const auto t = engine.TrainFromTakes(snds[k].name, takes);
            std::printf("taught %-9s %s dropped=%d %s\n", snds[k].name, t.ok ? "ok" : "FAILED", t.droppedTake, t.message.c_str());
        }
        std::vector<std::pair<double, std::string>> fired;
        for (size_t pos = 0; pos < rec.size(); pos += 480) {
            const auto out = engine.Process(rec.data() + pos, std::min<size_t>(480, rec.size() - pos));
            for (const auto& e : out.events)
                if (e.type == "custom") fired.emplace_back(static_cast<double>(pos) / 16000.0, e.label);
        }
        int right = 0, missed = 0, wrong = 0;
        for (size_t k = 0; k < snds.size(); ++k) {
            std::printf("%-9s", snds[k].name);
            for (size_t j = 0; j < 5; ++j) {
                const double t0 = on[k * 5 + j];
                std::string got;
                for (const auto& f : fired)
                    if (f.first >= t0 && f.first < t0 + 6.0) got += (got.empty() ? "" : "+") + f.second;
                const bool taughtPlay = j < 3;
                const char* mark = got == snds[k].name ? "ok" : got.empty() ? "MISS" : "WRONG";
                if (got == snds[k].name) ++right; else if (got.empty()) ++missed; else ++wrong;
                std::printf("  %s%s=%s", taughtPlay ? "T:" : "N:", mark, got.empty() ? "-" : got.c_str());
            }
            std::printf("\n");
        }
        std::printf("right %d, missed %d, wrong %d of 25 (T = a play used for teaching, N = a new play)\n", right, missed, wrong);
        return 0;
    }
    // --takes <wav> <startSec>...: cut 6 s takes (signal 1.2 s in) from a phone recording and run the Teach checks.
    if (argc > 3 && std::strcmp(argv[1], "--takes") == 0) {
        std::vector<int16_t> rec;
        if (!LoadWav(argv[2], &rec)) return 1;
        std::vector<std::vector<int16_t>> takes;
        sns::SoundEngine engine(sns::kSampleRate);
        for (int i = 3; i < argc; ++i) {
            const long from = std::lround((std::atof(argv[i]) - 1.2) * 16000.0);
            if (from < 0 || from + 96000 > static_cast<long>(rec.size())) continue;
            takes.emplace_back(rec.begin() + from, rec.begin() + from + 96000);
            if (std::getenv("ORACLE_DUR")) {  // keep only [onset-0.4 s, onset+dur+0.4 s] of the take
                const double dur = std::atof(std::getenv("ORACLE_DUR")) + (i % 2 ? 0.0 : 0.0);
                const long keepFrom = std::lround((1.2 - 0.4) * 16000.0), keepTo = std::lround((1.2 + dur + 0.4) * 16000.0);
                for (long k = 0; k < 96000; ++k) if (k < keepFrom || k > keepTo) takes.back()[static_cast<size_t>(k)] = 0;
            }
            const auto r = engine.CheckTake(takes.back());
            std::printf("take at %s s: %s %s\n", argv[i], r.ok ? "ok" : "REJECTED", r.message.c_str());
        }
        if (std::getenv("ALL_TAKES")) {  // one training call with every take (the Teach flow with 2-3 takes)
            const auto t = engine.TrainFromTakes("all", takes);
            std::printf("train all %zu takes: %s dropped=%d %s\n", takes.size(), t.ok ? "ok" : "FAILED", t.droppedTake,
                        t.message.c_str());
            return 0;
        }
        for (size_t a = 0; a + 1 < takes.size(); a += 2) {
            const auto t = engine.TrainFromTakes("x" + std::to_string(a), {takes[a], takes[a + 1]});
            std::printf("train takes %zu+%zu: %s %s\n", a, a + 1, t.ok ? "ok" : "FAILED", t.message.c_str());
        }
        return 0;
    }
    if (argc > 2 && std::strcmp(argv[1], "--export") == 0) {
        for (const auto& snd : All()) WriteWav(std::string(argv[2]) + "/" + snd.name + ".wav", snd.make());
        std::printf("exported\n");
        return 0;
    }
    std::vector<int16_t> wav;
    if (!LoadWav("entry/src/main/cpp/tests/data/phone_alarm_3x.wav", &wav) || wav.size() < 23 * 16000) {
        std::fprintf(stderr, "run from the repository root (needs tests/data/phone_alarm_3x.wav)\n");
        return 1;
    }
    NoiseSource noise(std::vector<int16_t>(wav.begin() + 13 * 16000, wav.begin() + 23 * 16000));  // room noise only
    std::printf("room noise of the phone recording: %.1f dB RMS\n\n", 20.0 * std::log10(noise.rms()));

    const std::vector<double> snrs = {25, 20, 15, 10};
    const auto& sounds = All();
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> jitter(-2.0, 2.0);

    // ---- detect
    std::printf("DETECT (alarm events | auto-learn ok)  - level above room noise\n%-10s", "");
    for (double s : snrs) std::printf("%9.0f dB", s);
    std::printf("\n");
    for (const auto& snd : sounds) {
        std::printf("%-10s", snd.name);
        for (double snr : snrs) {
            sns::SoundEngine engine(sns::kSampleRate);
            const auto sig = snd.make();
            const auto scene = Scene(noise, sig, snr, 3.0, 6.0);
            double firstAlarm = -1.0;
            int alarms = 0;
            bool learned = false, learnTried = false;
            std::string failure;
            for (size_t pos = 0; pos < scene.size(); pos += 480) {
                const size_t len = std::min<size_t>(480, scene.size() - pos);
                const double now = static_cast<double>(pos + len) / kSynthRate;
                const auto out = engine.Process(scene.data() + pos, len);
                for (const auto& e : out.events) {
                    if (e.type == "alarm") {
                        ++alarms;
                        if (firstAlarm < 0) firstAlarm = e.timeSec;
                    }
                }
                // like the app: learn 3.7 s after the first alarm event, while the audio is still in the ring
                if (firstAlarm >= 0 && !learnTried && now >= firstAlarm + 3.7) {
                    learnTried = true;
                    const auto lr = engine.LearnFromRing("x", firstAlarm);
                    learned = lr.ok;
                    failure = lr.message;
                }
            }
            if (learnTried && !learned && snr == 20) {
                std::printf("\n      [%s auto-learn failed: %s]\n%-10s", snd.name, failure.c_str(), "");
            }
            std::printf("   %2d | %-3s ", alarms, firstAlarm < 0 ? "-" : (learned ? "ok" : "no"));
        }
        std::printf("\n");
    }

    // ---- teach + recognise repeats
    std::printf("\nTEACH at 20 dB from 2 takes, then RECOGNISE 3 repeats at the level shown (n/3 recognised)\n%-10s%7s", "", "teach");
    for (double s : snrs) std::printf("%9.0f dB", s);
    std::printf("\n");
    for (const auto& snd : sounds) {
        sns::SoundEngine engine(sns::kSampleRate);
        const auto sig = snd.make();
        const auto t1 = Take(noise, sig, 20), t2 = Take(noise, sig, 20);
        const auto taught = engine.TrainFromTakes("own", {t1, t2});
        std::printf("%-10s%7s", snd.name, taught.ok ? "ok" : "FAIL");
        if (!taught.ok) {
            std::printf("  (%s)\n", taught.message.c_str());
            continue;
        }
        for (double snr : snrs) {
            int hits = 0;
            for (int rep = 0; rep < 3; ++rep) {
                const auto h = Feed(engine, Scene(noise, sig, snr, 3.0, 8.0, jitter(rng)));
                if (h.customs.count("own")) ++hits;
            }
            std::printf("      %d/3  ", hits);
        }
        std::printf("\n");
    }

    // ---- confusion
    std::printf("\nCONFUSION: all five taught at 20 dB; each signal played 3 times; names that fired (own / other)\n");
    for (double snr : snrs) {
        sns::SoundEngine engine(sns::kSampleRate);
        std::vector<std::vector<double>> sigs;
        bool allTaught = true;
        for (size_t i = 0; i < sounds.size(); ++i) {
            sigs.push_back(sounds[i].make());
            const auto r = engine.TrainFromTakes(sounds[i].name, {Take(noise, sigs[i], 20), Take(noise, sigs[i], 20)});
            allTaught = allTaught && r.ok;
        }
        std::printf("  played at %.0f dB%s\n", snr, allTaught ? "" : "  (not all sounds could be taught)");
        for (size_t i = 0; i < sounds.size(); ++i) {
            int own = 0, other = 0;
            std::string others;
            for (int rep = 0; rep < 3; ++rep) {
                const auto h = Feed(engine, Scene(noise, sigs[i], snr, 3.0, 8.0, jitter(rng)));
                for (const auto& kv : h.customs) {
                    if (kv.first == sounds[i].name) own += kv.second;
                    else {
                        other += kv.second;
                        others += (others.empty() ? "" : ",") + kv.first;
                    }
                }
            }
            std::printf("    %-10s own %d/3   confused %d %s\n", sounds[i].name, own, other,
                        other ? ("(as " + others + ")").c_str() : "");
        }
    }
    return 0;
}
