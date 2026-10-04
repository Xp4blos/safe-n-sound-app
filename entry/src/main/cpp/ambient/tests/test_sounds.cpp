// Built-in Chirp (smoke-alarm low-battery beep) and Cry (baby crying) detectors.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "ambient/detector.hpp"

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

struct Lcg {
    std::uint32_t s = 4242;
    double next() {
        s = s * 1664525u + 1013904223u;
        return (static_cast<double>(s >> 8) / 8388608.0) - 1.0;
    }
};
static std::int16_t to_pcm(double v) {
    return static_cast<std::int16_t>(std::lround(std::max(-1.0, std::min(1.0, v)) * 32767.0));
}
static void append(Pcm& a, const Pcm& b) { a.insert(a.end(), b.begin(), b.end()); }
static Pcm noise(double sec, double amp, Lcg& r) {
    Pcm p(static_cast<std::size_t>(sec * kSr));
    for (auto& s : p) s = to_pcm(amp * r.next());
    return p;
}
static Pcm beep(double sec, double hz, double amp, Lcg& r) {
    Pcm p(static_cast<std::size_t>(sec * kSr));
    for (std::size_t i = 0; i < p.size(); ++i) {
        const double t = static_cast<double>(i) / kSr;
        const double env = std::min({1.0, t / 0.004, (sec - t) / 0.004});
        p[i] = to_pcm(amp * env * std::sin(kTwoPi * hz * t) + 0.002 * r.next());
    }
    return p;
}
// Harmonic "cry" pulse: fundamental glides f0a -> f0b (rise-fall), 10 harmonics with 1/k^0.8 tilt.
static Pcm cry_pulse(double sec, double f0a, double f0b, double amp, Lcg& r) {
    Pcm p(static_cast<std::size_t>(sec * kSr));
    double phase = 0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const double u = static_cast<double>(i) / static_cast<double>(p.size());
        const double f0 = f0a + (f0b - f0a) * std::sin(3.14159265 * u);
        phase += kTwoPi * f0 / kSr;
        const double env = std::min({1.0, u / 0.08, (1.0 - u) / 0.1});
        double v = 0;
        for (int k = 1; k <= 10; ++k) v += std::sin(k * phase) / std::pow(k, 0.8);
        p[i] = to_pcm(amp * env * v / 3.0 + 0.003 * r.next());
    }
    return p;
}
static std::vector<Event> run(const Pcm& pcm, std::size_t chunk = 480) {
    Detector d;
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

static void test_smoke_chirp() {
    std::puts("smoke alarm low-battery chirp: periodic 3.2 kHz blips -> chirps, repeats gain confidence");
    Lcg r;
    Pcm p = noise(1.0, 0.002, r);
    for (int i = 0; i < 3; ++i) {
        append(p, beep(0.09, 3200.0, 0.3, r));
        append(p, noise(i < 2 ? 8.0 : 1.5, 0.002, r));  // chirp every ~8 s
    }
    const auto ev = run(p);
    CHECK(count(ev, EventType::Chirp) == 3);
    CHECK(count(ev, EventType::Knock) == 0);
    CHECK(count(ev, EventType::Alarm) == 0);
    CHECK(count(ev, EventType::LoudSound) == 0);
    int idx = 0;
    float first = 0, later = 0;
    for (const auto& e : ev) {
        if (e.type != EventType::Chirp) continue;
        CHECK(std::fabs(e.freq_hz - 3200.f) < 150.f);
        if (idx == 0) first = e.confidence; else later = std::max(later, e.confidence);
        ++idx;
    }
    CHECK(later > first);
}

static void test_chirp_vs_alarm() {
    std::puts("sustained tone is an alarm, not a chirp; chunking does not matter");
    Lcg r;
    Pcm p = noise(1.0, 0.002, r);
    append(p, beep(1.0, 2000.0, 0.3, r));
    append(p, noise(1.0, 0.002, r));
    auto ev = run(p);
    CHECK(count(ev, EventType::Alarm) == 1 && count(ev, EventType::Chirp) == 0);

    Pcm q = noise(1.0, 0.002, r);
    append(q, beep(0.09, 3200.0, 0.3, r));
    append(q, noise(1.0, 0.002, r));
    const auto a = run(q, q.size());
    for (std::size_t chunk : {1u, 7u, 480u, 4096u}) {
        const auto b = run(q, chunk);
        CHECK(a.size() == b.size());
        for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) CHECK(a[i].type == b[i].type);
    }
    CHECK(count(a, EventType::Chirp) == 1);
}

static void test_cry() {
    std::puts("baby crying: pulsed harmonic wails -> one cry event per episode");
    Lcg r;
    Pcm p = noise(1.0, 0.003, r);
    for (int i = 0; i < 4; ++i) {  // 4 pulses, ~1 s each, ~0.4 s apart, f0 ~400-520 Hz
        append(p, cry_pulse(1.0, 400.0, 520.0, 0.3, r));
        append(p, noise(0.4, 0.003, r));
    }
    append(p, noise(5.0, 0.003, r));
    const auto ev = run(p);
    CHECK(count(ev, EventType::Cry) == 1);
    CHECK(count(ev, EventType::LoudSound) == 0);  // crying is not also "loud sound"
    CHECK(count(ev, EventType::Alarm) == 0);
    for (const auto& e : ev)
        if (e.type == EventType::Cry) CHECK(e.freq_hz > 380.f && e.freq_hz < 560.f && e.confidence > 0.5f);

    // a second episode after a long pause is reported again
    Pcm q = p;
    for (int i = 0; i < 3; ++i) {
        append(q, cry_pulse(0.9, 450.0, 600.0, 0.3, r));
        append(q, noise(0.4, 0.003, r));
    }
    append(q, noise(5.0, 0.003, r));
    CHECK(count(run(q), EventType::Cry) == 2);
}

static void test_cry_rejects() {
    std::puts("cry: steady harmonic tone, low (adult) pitch, pure tone, noise, knock are not cries");
    Lcg r;
    Pcm p = noise(1.0, 0.003, r);
    for (int i = 0; i < 4; ++i) {  // constant pitch: machine, not a voice
        append(p, cry_pulse(1.0, 450.0, 450.0, 0.3, r));
        append(p, noise(0.4, 0.003, r));
    }
    append(p, noise(5.0, 0.003, r));
    CHECK(count(run(p), EventType::Cry) == 0);

    Pcm q = noise(1.0, 0.003, r);
    for (int i = 0; i < 4; ++i) {  // adult-range pitch
        append(q, cry_pulse(1.0, 110.0, 160.0, 0.3, r));
        append(q, noise(0.4, 0.003, r));
    }
    CHECK(count(run(q), EventType::Cry) == 0);

    Pcm s = noise(1.0, 0.003, r);
    append(s, beep(2.0, 500.0, 0.3, r));
    append(s, noise(1.0, 0.003, r));
    append(s, noise(10.0, 0.4, r));
    CHECK(count(run(s), EventType::Cry) == 0);
}

static void test_bang_on_beep_is_not_hidden() {
    std::puts("a loud bang landing on a chirp is still reported (steady beeps must not mask it)");
    Lcg r;
    Pcm p = noise(1.0, 0.002, r);
    Pcm b = beep(0.15, 3200.0, 0.05, r);
    for (std::size_t i = 0; i < 0.1 * kSr && 800 + i < b.size() + 3200; ++i) {
        const double t = static_cast<double>(i) / kSr;
        const double k = 0.5 * std::exp(-t / 0.015) * r.next();
        if (800 + i < b.size()) b[800 + i] = to_pcm(static_cast<double>(b[800 + i]) / 32768.0 + k);
    }
    append(p, b);
    append(p, noise(1.5, 0.002, r));
    const auto ev = run(p);
    CHECK(count(ev, EventType::Knock) + count(ev, EventType::LoudSound) >= 1);
}

// ------------------------------------------------------------------ siren / scream
// Tone whose instantaneous frequency is hz(t); optional 2nd harmonic (siren horns are not pure).
template <class F>
static Pcm sweep(double sec, F hz, double amp, Lcg& r) {
    Pcm p(static_cast<std::size_t>(sec * kSr));
    double ph = 0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const double t = static_cast<double>(i) / kSr;
        ph += kTwoPi * hz(t) / kSr;
        const double env = std::min({1.0, t / 0.05, (sec - t) / 0.05});
        p[i] = to_pcm(amp * env * (std::sin(ph) + 0.2 * std::sin(2 * ph)) / 1.2 + 0.002 * r.next());
    }
    return p;
}
static Pcm wail(double sec, Lcg& r) {  // 600 <-> 1400 Hz, 3.5 s per cycle
    return sweep(sec, [](double t) { return 1000.0 - 400.0 * std::cos(kTwoPi * t / 3.5); }, 0.3, r);
}
static Pcm yelp(double sec, Lcg& r) {  // 700 <-> 1500 Hz, 0.4 s per cycle
    return sweep(sec, [](double t) { return 1100.0 - 400.0 * std::cos(kTwoPi * t / 0.4); }, 0.3, r);
}
static Pcm hilo(double sec, Lcg& r) {  // 650 / 850 Hz alternating every 0.5 s
    return sweep(sec, [](double t) { return std::fmod(t, 1.0) < 0.5 ? 650.0 : 850.0; }, 0.3, r);
}
// Rough, loud voiced scream: moving f0 with jitter, flat-ish harmonic tilt, amplitude roughness, breath noise.
static Pcm scream(double sec, double f0c, double amp, Lcg& r) {
    Pcm p(static_cast<std::size_t>(sec * kSr));
    double ph = 0, jit = 0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const double t = static_cast<double>(i) / kSr;
        if (i % 160 == 0) jit = 0.04 * r.next();
        const double f0 = f0c * (1.0 + 0.15 * std::sin(kTwoPi * 1.3 * t) + jit);
        ph += kTwoPi * f0 / kSr;
        const double env = std::min({1.0, t / 0.01, (sec - t) / 0.05});
        const double rough = 1.0 + 0.4 * std::sin(kTwoPi * 70.0 * t);
        double v = 0;
        for (int k = 1; k <= 8; ++k) v += std::sin(k * ph) * std::pow(k, -0.3);
        p[i] = to_pcm(amp * env * rough * v / 6.0 + 0.04 * amp * r.next());
    }
    return p;
}

static void test_sirens() {
    std::puts("siren: wail, yelp and hi-lo are sirens (once); steady tone / cry / scream are not");
    Lcg r;
    for (int kind = 0; kind < 3; ++kind) {
        Pcm p = noise(1.0, 0.002, r);
        append(p, kind == 0 ? wail(8.0, r) : kind == 1 ? yelp(6.0, r) : hilo(6.0, r));
        append(p, noise(2.0, 0.002, r));
        const auto ev = run(p);
        if (count(ev, EventType::Siren) != 1) std::printf("  (kind %d)\n", kind);
        CHECK(count(ev, EventType::Siren) == 1);
        CHECK(count(ev, EventType::Scream) == 0);
        CHECK(count(ev, EventType::Cry) == 0);
        CHECK(count(ev, EventType::Alarm) <= 2);  // early alarm(s) only; none once the siren is confirmed
        for (const auto& e : ev)
            if (e.type == EventType::Siren) CHECK(e.time_s > 3.0 && e.time_s < 7.5 && e.confidence > 0.5f);
    }
    Pcm s = noise(1.0, 0.002, r);
    append(s, beep(4.0, 1000.0, 0.3, r));  // steady tone: Alarm territory
    append(s, noise(1.5, 0.002, r));
    CHECK(count(run(s), EventType::Siren) == 0);
    Pcm q = noise(1.0, 0.002, r);
    append(q, wail(1.2, r));  // a short whoop is not enough
    append(q, noise(2.0, 0.002, r));
    CHECK(count(run(q), EventType::Siren) == 0);
}

static void test_siren_chunking() {
    std::puts("siren/scream: chunked input == one-shot input");
    Lcg r;
    Pcm p = noise(1.0, 0.002, r);
    append(p, wail(6.0, r));
    append(p, noise(2.0, 0.002, r));
    append(p, scream(1.2, 900.0, 0.4, r));
    append(p, noise(2.0, 0.002, r));
    const auto a = run(p, p.size());
    CHECK(count(a, EventType::Siren) == 1 && count(a, EventType::Scream) == 1);
    for (std::size_t chunk : {1u, 7u, 480u, 4096u}) {
        const auto b = run(p, chunk);
        CHECK(a.size() == b.size());
        for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i)
            CHECK(a[i].type == b[i].type && std::fabs(a[i].time_s - b[i].time_s) < 1e-9);
    }
}

static void test_scream() {
    std::puts("scream: loud rough high-pitched voice -> one scream per event, no loud_sound / cry / siren");
    Lcg r;
    Pcm p = noise(1.0, 0.003, r);
    append(p, scream(1.2, 900.0, 0.4, r));
    append(p, noise(5.0, 0.003, r));
    append(p, scream(0.9, 1100.0, 0.4, r));
    append(p, noise(2.0, 0.003, r));
    const auto ev = run(p);
    CHECK(count(ev, EventType::Scream) == 2);
    CHECK(count(ev, EventType::LoudSound) == 0);
    CHECK(count(ev, EventType::Cry) == 0);
    CHECK(count(ev, EventType::Siren) == 0);
    for (const auto& e : ev)
        if (e.type == EventType::Scream) CHECK(e.freq_hz > 700.f && e.freq_hz < 1400.f && e.confidence > 0.5f);
}

static void test_scream_rejects() {
    std::puts("scream: low yell, cry, quiet voice, steady harmonic tone and whistle are not screams");
    Lcg r;
    Pcm p = noise(1.0, 0.003, r);
    append(p, scream(1.2, 280.0, 0.4, r));  // adult-pitch yell
    append(p, noise(3.0, 0.003, r));
    for (int i = 0; i < 3; ++i) {            // crying
        append(p, cry_pulse(1.0, 400.0, 520.0, 0.3, r));
        append(p, noise(0.4, 0.003, r));
    }
    append(p, noise(3.0, 0.003, r));
    append(p, scream(1.2, 900.0, 0.004, r));  // far too quiet
    append(p, noise(3.0, 0.003, r));
    append(p, sweep(1.5, [](double) { return 1000.0; }, 0.3, r));  // steady tone
    append(p, noise(3.0, 0.003, r));
    append(p, sweep(1.0, [](double t) { return 900.0 + 500.0 * t; }, 0.3, r));  // whistle glide
    append(p, noise(3.0, 0.003, r));
    CHECK(count(run(p), EventType::Scream) == 0);
}

static void test_ranges() {
    std::puts("other sample rates / frame sizes do not crash and config is validated");
    Lcg r;
    for (int sr : {8000, 22050, 44100, 48000}) {
        Config c;
        c.sample_rate = sr;
        c.frame_size = sr >= 44100 ? 2048 : 512;
        c.hop_size = c.frame_size / 2;
        c.alarm_max_hz = std::min(4500.f, sr / 2.f - 100.f);
        Detector d(c);
        Pcm p(static_cast<std::size_t>(sr * 3));
        for (auto& s : p) s = to_pcm(0.2 * r.next());
        d.process(p.data(), p.size());
    }
    bool threw = false;
    try { Config c; c.chirp_min_s = 1.f; Detector d(c); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { Config c; c.cry_min_hz = 900.f; Detector d(c); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { Config c; c.siren_min_s = 0.f; Detector d(c); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { Config c; c.scream_harmonic_max = 0.2f; Detector d(c); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}

int main() {
    test_smoke_chirp();
    test_chirp_vs_alarm();
    test_cry();
    test_cry_rejects();
    test_bang_on_beep_is_not_hidden();
    test_sirens();
    test_siren_chunking();
    test_scream();
    test_scream_rejects();
    test_ranges();
    if (g_failed == 0) std::puts("ALL SOUND TESTS PASSED");
    else std::printf("%d CHECK(S) FAILED\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
