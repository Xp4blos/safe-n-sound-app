// Safety / robustness tests: chunk invariance, config validation, stored-sound integrity,
// real-world signal conditions, thread safety.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <thread>
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

constexpr int kSr = 16000;
constexpr double kTwoPi = 6.283185307179586;
using Pcm = std::vector<std::int16_t>;

static std::int16_t q(double v) {
    return static_cast<std::int16_t>(std::lround(std::max(-1.0, std::min(1.0, v)) * 32767.0));
}
struct Lcg {
    std::uint32_t s = 4242;
    double next() {
        s = s * 1664525u + 1013904223u;
        return (static_cast<double>(s >> 8) / 8388608.0) - 1.0;
    }
};
static Lcg g_rng;
static void add(Pcm& a, const Pcm& b) { a.insert(a.end(), b.begin(), b.end()); }
static Pcm noise(double sec, double amp) {
    Pcm p(static_cast<std::size_t>(sec * kSr));
    for (auto& x : p) x = q(amp * g_rng.next());
    return p;
}
static Pcm tone(double sec, double hz, double amp, double na = 0.002) {
    Pcm p(static_cast<std::size_t>(sec * kSr));
    for (std::size_t i = 0; i < p.size(); ++i)
        p[i] = q(amp * std::sin(kTwoPi * hz * static_cast<double>(i) / kSr) + na * g_rng.next());
    return p;
}
static Pcm knock(double amp) {
    Pcm p(static_cast<std::size_t>(0.2 * kSr));
    for (std::size_t i = 0; i < p.size(); ++i)
        p[i] = q(amp * std::exp(-(static_cast<double>(i) / kSr) / 0.015) * g_rng.next());
    return p;
}
static Pcm beeps(double hz, int n, double amp, double na = 0.002) {
    Pcm o;
    for (int k = 0; k < n; ++k) {
        const std::size_t m = static_cast<std::size_t>(0.15 * kSr);
        for (std::size_t i = 0; i < m; ++i) {
            const double t = static_cast<double>(i) / kSr;
            const double env = std::min({1.0, t / 0.005, (0.15 - t) / 0.005});
            o.push_back(q(amp * env * std::sin(kTwoPi * hz * t) + na * g_rng.next()));
        }
        if (k + 1 < n) add(o, noise(0.2, na));
    }
    return o;
}

static std::vector<Event> run(Detector& d, const Pcm& p, std::size_t chunk) {
    std::vector<Event> all;
    for (std::size_t i = 0; i < p.size(); i += chunk) {
        auto ev = d.process(p.data() + i, std::min(chunk, p.size() - i));
        all.insert(all.end(), ev.begin(), ev.end());
    }
    auto tail = d.flush();
    all.insert(all.end(), tail.begin(), tail.end());
    return all;
}
static int count(const std::vector<Event>& ev, EventType t) {
    return static_cast<int>(std::count_if(ev.begin(), ev.end(), [&](const Event& e) { return e.type == t; }));
}

// ------------------------------------------------------------------ tests
static void test_chunk_invariance_builtin() {
    Pcm p = noise(1, .002);
    add(p, tone(1, 2000, .3));
    add(p, noise(1, .002));
    add(p, knock(.6));
    add(p, noise(1, .002));
    add(p, noise(1.5, .5));
    add(p, noise(1, .002));
    Detector ref;
    const auto base = run(ref, p, 4096);
    CHECK(count(base, EventType::Alarm) == 1 && count(base, EventType::Knock) == 1 &&
          count(base, EventType::LoudSound) == 1);
    for (std::size_t chunk : {std::size_t{1}, std::size_t{97}, std::size_t{777}, std::size_t{1024}, std::size_t{50000}}) {
        Detector d;
        const auto ev = run(d, p, chunk);
        CHECK(ev.size() == base.size());
        for (std::size_t i = 0; i < std::min(ev.size(), base.size()); ++i) {
            CHECK(ev[i].type == base[i].type);
            CHECK(std::fabs(ev[i].time_s - base[i].time_s) < 1e-9);
        }
    }
}

static void test_config_validation() {
    auto bad = [](auto mutate) {
        Config c;
        mutate(c);
        try { Detector d(c); } catch (const std::invalid_argument&) { return true; } catch (...) { return false; }
        return false;
    };
    CHECK(bad([](Config& c) { c.min_level_db = std::nanf(""); }));
    CHECK(bad([](Config& c) { c.min_level_db = 5.f; }));
    CHECK(bad([](Config& c) { c.onset_db = 0.f; }));
    CHECK(bad([](Config& c) { c.decay_db = -1.f; }));
    CHECK(bad([](Config& c) { c.alarm_min_s = 0.f; }));
    CHECK(bad([](Config& c) { c.knock_max_s = -1.f; }));
    CHECK(bad([](Config& c) { c.alarm_gap_frames = -1; }));
    CHECK(bad([](Config& c) { c.background_alpha = 0.f; }));
    CHECK(bad([](Config& c) { c.background_alpha = 1.5f; }));
    CHECK(bad([](Config& c) { c.sample_rate = 500000; }));
    CHECK(bad([](Config& c) { c.frame_size = 1u << 20; c.hop_size = 512; }));
    Config ok;
    Detector d(ok);  // defaults still valid
    (void)d;
}

static CustomSound trained_fridge(const Config& cfg) {
    Pcm learn = noise(.8, .002);
    add(learn, beeps(3000, 2, .25));
    add(learn, noise(.8, .002));
    return train_custom_sound(cfg, "fridge", {{learn.data(), learn.size()}}).sound;
}

static void test_stored_sound_integrity() {
    Config cfg;
    const CustomSound s = trained_fridge(cfg);
    const auto bytes = serialize(s);
    CHECK(deserialize_sound(bytes.data(), bytes.size()).name == "fridge");

    // every single flipped byte is rejected (CRC) or yields a valid sound -- never a crash
    int accepted = 0;
    for (std::size_t i = 0; i < bytes.size(); i += 7) {
        auto c = bytes;
        c[i] ^= 0x5A;
        try { deserialize_sound(c.data(), c.size()); ++accepted; } catch (const std::invalid_argument&) {}
    }
    CHECK(accepted == 0);

    auto trailing = bytes;
    trailing.push_back(0);
    bool threw = false;
    try { deserialize_sound(trailing.data(), trailing.size()); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);

    // version-1 data (no CRC) is still readable
    auto v1 = bytes;
    v1.resize(v1.size() - 4);
    v1[4] = 1;
    CHECK(deserialize_sound(v1.data(), v1.size()).frames == s.frames);

    // out-of-range parameters are refused
    CustomSound huge = s;
    huge.refractory_s = 1e9f;
    threw = false;
    try { validate_sound(huge); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}

static void test_robust_signal_conditions() {
    {  // alarm still found with a very noisy floor
        Detector d;
        Pcm p = noise(1, .2);
        add(p, tone(1, 2000, .3, .2));
        add(p, noise(1, .2));
        CHECK(count(run(d, p, 512), EventType::Alarm) == 1);
    }
    {  // mains hum (below alarm band): nothing
        Detector d;
        Pcm p(5 * kSr);
        for (std::size_t i = 0; i < p.size(); ++i)
            p[i] = q(.3 * std::sin(kTwoPi * 50 * static_cast<double>(i) / kSr) +
                     .2 * std::sin(kTwoPi * 120 * static_cast<double>(i) / kSr) + .002 * g_rng.next());
        CHECK(run(d, p, 512).empty());
    }
    {  // DC offset on a quiet mic: nothing
        Detector d;
        Pcm p = noise(4, .003);
        for (auto& x : p) x = static_cast<std::int16_t>(std::min(32767, x + 6000));
        CHECK(run(d, p, 512).empty());
    }
    {  // steady loud fan from t=0: nothing
        Detector d;
        CHECK(run(d, noise(5, .1), 512).empty());
    }
    {  // clipped full-scale tone is an alarm, not a crash
        Detector d;
        Pcm p = noise(1, .002);
        add(p, tone(1, 1000, 3.0));
        add(p, noise(1, .002));
        CHECK(count(run(d, p, 512), EventType::Alarm) == 1);
    }
}

static void test_alarm_boundaries() {
    auto alarms = [](double sec) {
        Detector d;
        Pcm p = noise(1, .002);
        add(p, tone(sec, 2000, .3));
        add(p, noise(1, .002));
        return count(run(d, p, 512), EventType::Alarm);
    };
    CHECK(alarms(0.2) == 0);  // shorter than alarm_min_s (0.4 s)
    CHECK(alarms(0.8) == 1);
    auto tone_at = [](double hz) {
        Detector d;
        Pcm p = noise(1, .002);
        add(p, tone(1, hz, .3));
        add(p, noise(1, .002));
        return count(run(d, p, 512), EventType::Alarm);
    };
    CHECK(tone_at(500) == 0);   // below alarm_min_hz
    CHECK(tone_at(6000) == 0);  // above alarm_max_hz
    CHECK(tone_at(1000) == 1 && tone_at(4000) == 1);
}

// A loud bang landing inside a learned sound's window must still be reported by someone
// (either the custom match no longer holds, or the louder transient survives the hold-back).
static void test_loud_bang_inside_custom_window_is_reported() {
    Config cfg;
    const CustomSound s = trained_fridge(cfg);
    Detector d;
    d.add_custom_sound(s);
    Pcm p = noise(1, .002);
    Pcm b = beeps(3000, 2, .05);
    const Pcm k = knock(.5);
    const std::size_t at = static_cast<std::size_t>(0.17 * kSr);  // in the gap between the beeps
    for (std::size_t i = 0; i < k.size() && at + i < b.size(); ++i)
        b[at + i] = q((static_cast<double>(b[at + i]) + static_cast<double>(k[i])) / 32768.0);
    add(p, b);
    add(p, noise(2, .002));
    const auto ev = run(d, p, 512);
    CHECK(count(ev, EventType::Knock) + count(ev, EventType::LoudSound) >= 1);
}

static void test_stream_clock_and_reset() {
    Detector d;
    CHECK(d.stream_time_s() == 0.0);
    const Pcm p = noise(1, .002);
    d.process(p.data(), p.size());
    CHECK(d.stream_time_s() > 0.9 && d.stream_time_s() <= 1.0 + 1e-9);
    d.reset();
    CHECK(d.stream_time_s() == 0.0);
}

static void test_trainer_selfcheck_still_accepts_good_sounds() {
    Config cfg;
    Pcm a = noise(.8, .002), b = noise(.8, .002), c = noise(.8, .002);
    add(a, beeps(3000, 2, .25)); add(a, noise(.8, .002));
    add(b, beeps(3000, 2, .20)); add(b, noise(.8, .002));
    add(c, beeps(3000, 2, .30)); add(c, noise(.8, .002));
    const auto r = train_custom_sound(cfg, "x", {{a.data(), a.size()}, {b.data(), b.size()}, {c.data(), c.size()}});
    CHECK(r.sound.frames > 4);
}

// UI thread adds/removes sounds while the audio thread streams. Run under TSan/ASan.
static void test_concurrent_add_remove_while_processing() {
    Config cfg;
    const CustomSound s = trained_fridge(cfg);
    Detector d;
    Pcm p = noise(1, .002);
    add(p, beeps(3000, 2, .25));
    add(p, noise(1, .002));
    std::atomic<bool> stop{false};
    std::thread audio([&] {
        while (!stop.load()) {
            for (std::size_t i = 0; i < p.size(); i += 512) d.process(p.data() + i, std::min<std::size_t>(512, p.size() - i));
        }
    });
    for (int i = 0; i < 300; ++i) {
        d.add_custom_sound(s);
        (void)d.custom_sound_names();
        (void)d.stream_time_s();
        d.remove_custom_sound("fridge");
    }
    stop.store(true);
    audio.join();
    CHECK(true);  // reaching here without a crash / sanitizer report is the assertion
}

int main() {
    test_chunk_invariance_builtin();
    test_config_validation();
    test_stored_sound_integrity();
    test_robust_signal_conditions();
    test_alarm_boundaries();
    test_loud_bang_inside_custom_window_is_reported();
    test_stream_clock_and_reset();
    test_trainer_selfcheck_still_accepts_good_sounds();
    test_concurrent_add_remove_while_processing();
    if (g_failed == 0) std::puts("ALL SAFETY TESTS PASSED");
    else std::printf("%d CHECK(S) FAILED\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
