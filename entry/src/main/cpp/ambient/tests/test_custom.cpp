// Extra tests for custom sounds: edge cases, robustness, parameter ranges.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include "ambient/detector.hpp"
#include "ambient/trainer.hpp"

using namespace ambient;

static int g_failed = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            ++g_failed;                                                          \
        }                                                                        \
    } while (0)

constexpr double kTwoPi = 6.283185307179586;
using Pcm = std::vector<std::int16_t>;

struct Lcg {
    std::uint32_t s = 777;
    double next() {
        s = s * 1664525u + 1013904223u;
        return (static_cast<double>(s >> 8) / 8388608.0) - 1.0;
    }
    std::uint32_t bits() { next(); return s; }
};

static std::int16_t to_pcm(double v) {
    return static_cast<std::int16_t>(std::lround(std::max(-1.0, std::min(1.0, v)) * 32767.0));
}
static void append(Pcm& a, const Pcm& b) { a.insert(a.end(), b.begin(), b.end()); }
static Pcm noise(double sec, double amp, Lcg& r, int sr = 16000) {
    Pcm p(static_cast<std::size_t>(sec * sr));
    for (auto& s : p) s = to_pcm(amp * r.next());
    return p;
}
// n beeps of partials `hz`, with background noise `bg` mixed in.
static Pcm beeps(const std::vector<double>& hz, int n, double beep_s, double gap_s, double amp, Lcg& r,
                 int sr = 16000, double bg = 0.002) {
    Pcm p;
    for (int k = 0; k < n; ++k) {
        Pcm b(static_cast<std::size_t>(beep_s * sr));
        for (std::size_t i = 0; i < b.size(); ++i) {
            const double t = static_cast<double>(i) / sr;
            const double env = std::min({1.0, t / 0.005, (beep_s - t) / 0.005});
            double v = 0;
            for (double f : hz) v += std::sin(kTwoPi * f * t) / static_cast<double>(hz.size());
            b[i] = to_pcm(amp * env * v + bg * r.next());
        }
        append(p, b);
        if (k + 1 < n) append(p, noise(gap_s, bg, r, sr));
    }
    return p;
}
static Pcm framed(const Pcm& s, Lcg& r, int sr = 16000) {
    Pcm p = noise(0.8, 0.002, r, sr);
    append(p, s);
    append(p, noise(0.8, 0.002, r, sr));
    return p;
}
static Pcm fridge(double amp, Lcg& r) { return beeps({3000.0}, 2, 0.15, 0.2, amp, r); }

static std::vector<Event> run(Detector& d, const Pcm& pcm, std::size_t chunk = 480) {
    std::vector<Event> all;
    for (std::size_t i = 0; i < pcm.size(); i += chunk) {
        auto ev = d.process(pcm.data() + i, std::min(chunk, pcm.size() - i));
        all.insert(all.end(), ev.begin(), ev.end());
    }
    auto tail = d.flush();
    all.insert(all.end(), tail.begin(), tail.end());
    return all;
}
static int count(const std::vector<Event>& ev, EventType t) {
    return static_cast<int>(std::count_if(ev.begin(), ev.end(), [t](const Event& e) { return e.type == t; }));
}
static CustomSound learn(const Config& c, const char* name, const std::vector<Pcm>& recs) {
    std::vector<Recording> v;
    for (const auto& p : recs) v.push_back({p.data(), p.size()});
    return train_custom_sound(c, name, v).sound;
}
static CustomSound learn(const char* name, const std::vector<Pcm>& recs) { return learn(Config{}, name, recs); }

template <class F>
static bool throws_invalid(F f) {
    try { f(); } catch (const std::invalid_argument&) { return true; }
    return false;
}

// ---------------------------------------------------------------------------
static void test_prepare_window() {
    std::puts("prepare_window: zero mean, unit norm, level invariant, flat rejected");
    std::vector<float> a(kBands * 5), b, out(a.size()), out2(a.size());
    Lcg r;
    for (auto& v : a) v = static_cast<float>(-40.0 + 25.0 * r.next());
    b = a;
    for (auto& v : b) v += 17.f;  // same pattern, louder
    CHECK(prepare_window(a.data(), a.size(), out.data()));
    CHECK(prepare_window(b.data(), b.size(), out2.data()));
    double mean = 0, ss = 0, diff = 0;
    for (std::size_t i = 0; i < out.size(); ++i) {
        mean += out[i];
        ss += static_cast<double>(out[i]) * out[i];
        diff = std::max(diff, static_cast<double>(std::fabs(out[i] - out2[i])));
    }
    CHECK(std::fabs(mean) < 1e-3);
    CHECK(std::fabs(ss - 1.0) < 1e-4);
    CHECK(diff < 1e-4);
    std::vector<float> flat(a.size(), -50.f);
    CHECK(!prepare_window(flat.data(), flat.size(), out.data()));
}

static void test_band_layout_all_rates() {
    std::puts("detector works for several sample rates / frame sizes");
    Lcg r;
    for (int sr : {8000, 16000, 22050, 44100, 48000}) {
        for (std::size_t frame : {64u, 256u, 1024u, 2048u}) {
            Config c;
            c.sample_rate = sr;
            c.frame_size = frame;
            c.hop_size = frame / 2;
            c.alarm_max_hz = std::min(4500.f, sr / 2.f - 100.f);
            Detector d(c);
            Pcm rec = noise(0.6, 0.002, r, sr);
            append(rec, beeps({2000.0}, 2, 0.15, 0.2, 0.25, r, sr));
            append(rec, noise(0.6, 0.002, r, sr));
            bool ok = true;
            try {
                d.add_custom_sound(learn(c, "b", {rec}));
                run(d, rec);
            } catch (const std::exception& e) {
                std::printf("  sr=%d frame=%zu: %s\n", sr, frame, e.what());
                ok = false;
            }
            CHECK(ok);
        }
    }
}

static void test_other_sample_rate_detection() {
    std::puts("custom sound is found at 8 kHz and 48 kHz");
    for (int sr : {8000, 48000}) {
        Lcg r;
        Config c;
        c.sample_rate = sr;
        c.frame_size = sr == 8000 ? 512 : 2048;
        c.hop_size = c.frame_size / 2;
        c.alarm_max_hz = std::min(4500.f, sr / 2.f - 100.f);
        Detector d(c);
        d.add_custom_sound(learn(c, "b", {framed(beeps({2500.0}, 2, 0.15, 0.2, 0.25, r, sr), r, sr)}));
        Pcm p = noise(1.0, 0.002, r, sr);
        append(p, beeps({2500.0}, 2, 0.15, 0.2, 0.25, r, sr));
        append(p, noise(1.0, 0.002, r, sr));
        CHECK(count(run(d, p), EventType::Custom) == 1);
    }
}

static void test_event_timing() {
    std::puts("event time/start bracket the real sound");
    Lcg r;
    Detector d;
    d.add_custom_sound(learn("f", {framed(fridge(0.25, r), r)}));
    Pcm p = noise(2.0, 0.002, r);   // beep pattern spans 2.0 .. 2.5 s
    append(p, fridge(0.25, r));
    append(p, noise(1.5, 0.002, r));
    const auto ev = run(d, p);
    CHECK(count(ev, EventType::Custom) == 1);
    for (const auto& e : ev) {
        if (e.type != EventType::Custom) continue;
        CHECK(e.start_s < 2.15 && e.start_s > 1.5);
        CHECK(e.time_s > 2.5 && e.time_s < 3.2);
        CHECK(e.sound_id == 0 && e.label == "f");
        CHECK(e.level_db > -25.f);
    }
}

static void test_noise_robustness() {
    std::puts("beep over moderate background noise is still found; pure noise soak has no hits");
    Lcg r;
    Detector d;
    d.add_custom_sound(learn("f", {framed(fridge(0.25, r), r)}));
    Pcm p = noise(1.0, 0.02, r);
    append(p, beeps({3000.0}, 2, 0.15, 0.2, 0.25, r, 16000, 0.02));  // background ~ -34 dBFS
    append(p, noise(2.0, 0.02, r));
    CHECK(count(run(d, p), EventType::Custom) == 1);

    d.reset();
    CHECK(count(run(d, noise(60.0, 0.01, r)), EventType::Custom) == 0);
    d.reset();
    CHECK(count(run(d, noise(30.0, 0.3, r)), EventType::Custom) == 0);
}

static void test_refractory() {
    std::puts("refractory_s: repeats inside it are suppressed, after it are reported");
    Lcg r;
    CustomSound s = learn("f", {framed(fridge(0.25, r), r)});
    Pcm p = noise(1.0, 0.002, r);
    for (int i = 0; i < 3; ++i) {  // beeps every ~1.5 s
        append(p, fridge(0.25, r));
        append(p, noise(1.0, 0.002, r));
    }
    {
        s.refractory_s = 2.0f;  // beeps are 1.5 s apart: 2nd is skipped, 3rd (3 s after 1st) is reported
        Detector d;
        d.add_custom_sound(s);
        CHECK(count(run(d, p), EventType::Custom) == 2);
    }
    {
        s.refractory_s = 0.5f;
        Detector d;
        d.add_custom_sound(s);
        CHECK(count(run(d, p), EventType::Custom) == 3);
    }
}

static void test_threshold_param() {
    std::puts("threshold: 0.99 rejects a slightly different sound that 0.8 accepts");
    Lcg r;
    CustomSound s = learn("f", {framed(fridge(0.25, r), r)});
    Pcm p = noise(1.0, 0.002, r);
    append(p, beeps({3000.0}, 2, 0.15, 0.24, 0.25, r));  // gap 40 ms longer than the learned one
    append(p, noise(1.0, 0.002, r));
    Detector lo, hi;
    s.threshold = 0.8f;
    lo.add_custom_sound(s);
    s.threshold = 0.99f;
    hi.add_custom_sound(s);
    CHECK(count(run(lo, p), EventType::Custom) == 1);
    CHECK(count(run(hi, p), EventType::Custom) == 0);
}

static void test_hold_behaviour() {
    std::puts("custom_hold_s: 0 reports the beep as knock too; flush() releases held events");
    Lcg r;
    const CustomSound s = learn("f", {framed(fridge(0.25, r), r)});
    Pcm p = noise(1.0, 0.002, r);
    append(p, fridge(0.25, r));
    append(p, noise(1.5, 0.002, r));
    {
        Config c;
        c.custom_hold_s = 0.f;
        c.detect_chirp = false;  // otherwise the beep is (correctly) a Chirp, not a Knock
        Detector d(c);
        d.add_custom_sound(learn(c, "f", {framed(fridge(0.25, r), r)}));
        const auto ev = run(d, p);
        CHECK(count(ev, EventType::Custom) == 1);
        CHECK(count(ev, EventType::Knock) >= 1);
    }
    {   // a real knock right at the end of the stream is only available after flush()
        Detector d;
        d.add_custom_sound(s);
        Pcm q = noise(1.0, 0.002, r);
        Pcm k(3200);
        for (std::size_t i = 0; i < k.size(); ++i)
            k[i] = to_pcm(0.6 * std::exp(-(static_cast<double>(i) / 16000.0) / 0.015) * r.next());
        append(q, k);
        append(q, noise(0.1, 0.002, r));
        auto ev = d.process(q.data(), q.size());
        CHECK(count(ev, EventType::Knock) == 0);
        CHECK(count(d.flush(), EventType::Knock) == 1);
        CHECK(d.flush().empty());
    }
    {   // negative hold is an invalid config
        Config c;
        c.custom_hold_s = -1.f;
        CHECK(throws_invalid([&] { Detector d(c); }));
    }
}

static void test_replace_remove_ids() {
    std::puts("add with the same name replaces and keeps id; names listed; remove works");
    Lcg r;
    Detector d;
    const int a = d.add_custom_sound(learn("a", {framed(fridge(0.25, r), r)}));
    const int b = d.add_custom_sound(learn("b", {framed(beeps({1500.0}, 3, 0.2, 0.2, 0.25, r), r)}));
    CHECK(a != b);
    CHECK(d.add_custom_sound(learn("a", {framed(fridge(0.2, r), r)})) == a);
    CHECK(d.custom_sound_names().size() == 2);
    CHECK(d.remove_custom_sound("a"));
    CHECK(d.custom_sound_names().size() == 1 && d.custom_sound_names()[0] == "b");
    CHECK(!d.remove_custom_sound("zzz"));
    Pcm p = noise(1.0, 0.002, r);
    append(p, beeps({1500.0}, 3, 0.2, 0.2, 0.25, r));
    append(p, noise(1.5, 0.002, r));
    const auto ev = run(d, p);
    CHECK(count(ev, EventType::Custom) == 1);
    for (const auto& e : ev)
        if (e.type == EventType::Custom) CHECK(e.sound_id == b && e.label == "b");
}

static void test_trainer_options_and_errors() {
    std::puts("trainer: option limits, null/empty input, bad names");
    Lcg r;
    const Pcm good = framed(fridge(0.25, r), r);
    const Config c;
    auto train = [&](const std::string& name, std::vector<Recording> v, TrainOptions o = {}) {
        return [=] { train_custom_sound(c, name, v, o); };
    };
    CHECK(throws_invalid(train("", {{good.data(), good.size()}})));
    CHECK(throws_invalid(train(std::string(300, 'x'), {{good.data(), good.size()}})));
    CHECK(throws_invalid(train("x", {{nullptr, 100}})));
    CHECK(throws_invalid(train("x", {{good.data(), 0}})));
    TrainOptions o;
    o.max_duration_s = 0.2f;
    CHECK(throws_invalid(train("x", {{good.data(), good.size()}}, o)));
    o = {};
    o.min_contrast_db = 80.f;
    CHECK(throws_invalid(train("x", {{good.data(), good.size()}}, o)));
    CHECK(!throws_invalid(train("x", {{good.data(), good.size()}})));

    // result sanity
    const TrainResult tr = train_custom_sound(c, "x", {{good.data(), good.size()}});
    CHECK(tr.sound.frames >= 4 && tr.sound.tmpl.size() == tr.sound.frames * kBands);
    CHECK(tr.sound.env.size() == tr.sound.frames);
    CHECK(tr.sound.threshold > 0.f && tr.sound.threshold < 1.f);
    CHECK(tr.sound.min_level_db < -10.f && tr.sound.min_level_db >= -75.f);
    // very quiet recording still trains and its gate follows the recording level
    const Pcm quiet = framed(beeps({3000.0}, 2, 0.15, 0.2, 0.01, r), r);
    CHECK(train_custom_sound(c, "q", {{quiet.data(), quiet.size()}}).sound.min_level_db < tr.sound.min_level_db);
}

static void test_serialization_fuzz() {
    std::puts("deserialize never crashes on corrupted data");
    Lcg r;
    const CustomSound s = learn("f", {framed(fridge(0.25, r), r)});
    const auto good = serialize(s);
    int accepted = 0, rejected = 0;
    for (int i = 0; i < 3000; ++i) {
        auto b = good;
        const int flips = 1 + static_cast<int>(r.bits() % 4);
        for (int k = 0; k < flips; ++k) b[r.bits() % b.size()] = static_cast<std::uint8_t>(r.bits());
        if (r.bits() % 5 == 0) b.resize(r.bits() % b.size());
        try {
            const CustomSound t = deserialize_sound(b.data(), b.size());
            Detector d;
            try { d.add_custom_sound(t); } catch (const std::invalid_argument&) {}
            ++accepted;
        } catch (const std::invalid_argument&) {
            ++rejected;
        }
    }
    CHECK(rejected > 0);
    std::printf("  (accepted %d, rejected %d)\n", accepted, rejected);
    CHECK(throws_invalid([] { deserialize_sound(nullptr, 0); }));
    // untouched data still round-trips exactly
    CHECK(serialize(deserialize_sound(good.data(), good.size())) == good);
}

int main() {
    test_prepare_window();
    test_band_layout_all_rates();
    test_other_sample_rate_detection();
    test_event_timing();
    test_noise_robustness();
    test_refractory();
    test_threshold_param();
    test_hold_behaviour();
    test_replace_remove_ids();
    test_trainer_options_and_errors();
    test_serialization_fuzz();
    if (g_failed == 0) std::puts("ALL CUSTOM TESTS PASSED");
    else std::printf("%d CHECK(S) FAILED\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
