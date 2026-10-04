#include "ambient/custom.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace ambient {

namespace {
constexpr float kBandLoHz = 200.f;
constexpr float kBandHiHz = 7600.f;
constexpr std::size_t kMaxFrames = 4000;
constexpr std::size_t kMaxName = 256;
constexpr int kPeakConfirmFrames = 2;
constexpr std::uint32_t kVersion = 2;  // v2 = v1 + trailing CRC32; v1 is still read
constexpr float kMaxRefractoryS = 3600.f;

std::uint32_t crc32(const std::uint8_t* p, std::size_t n) {
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}
const char kMagic[4] = {'A', 'S', 'N', 'D'};

void put32(std::vector<std::uint8_t>& o, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) o.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}
void putf(std::vector<std::uint8_t>& o, float f) {
    std::uint32_t v;
    std::memcpy(&v, &f, 4);
    put32(o, v);
}

struct Reader {
    const std::uint8_t* p;
    std::size_t left;
    void need(std::size_t n) const {
        if (n > left) throw std::invalid_argument("custom sound: truncated data");
    }
    std::uint32_t u32() {
        need(4);
        const std::uint32_t v = p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
        p += 4;
        left -= 4;
        return v;
    }
    float f32() {
        const std::uint32_t v = u32();
        float f;
        std::memcpy(&f, &v, 4);
        return f;
    }
};
}  // namespace

// ---------------------------------------------------------------- BandBank
BandBank::BandBank(int sample_rate, std::size_t frame_size) : edges_(kBands + 1) {
    const double bin_hz = static_cast<double>(sample_rate) / static_cast<double>(frame_size);
    const std::size_t half = frame_size / 2;
    const double hi = std::min<double>(kBandHiHz, 0.95 * sample_rate / 2.0);
    for (std::size_t i = 0; i <= kBands; ++i) {
        const double hz = kBandLoHz * std::pow(hi / kBandLoHz, static_cast<double>(i) / kBands);
        std::size_t e = static_cast<std::size_t>(std::lround(hz / bin_hz));
        e = std::max<std::size_t>(e, i == 0 ? 1 : edges_[i - 1] + 1);
        edges_[i] = e;
    }
    for (std::size_t i = 0; i <= kBands; ++i)  // keep strictly increasing and inside the spectrum
        edges_[i] = std::min(edges_[i], half - (kBands - i));
}

void BandBank::compute(const std::complex<float>* spec, float* out_db) const {
    for (std::size_t b = 0; b < kBands; ++b) {
        double e = 0.0;
        for (std::size_t k = edges_[b]; k < edges_[b + 1]; ++k) e += std::norm(spec[k]);
        out_db[b] = 10.f * std::log10(static_cast<float>(e) + 1e-12f);
    }
}

// ---------------------------------------------------------------- CustomSound
void validate_sound(const CustomSound& s) {
    if (s.name.empty() || s.name.size() > kMaxName) throw std::invalid_argument("custom sound: bad name");
    if (s.sample_rate < 8000 || s.frame_size < 64 || s.hop_size == 0 || s.hop_size > s.frame_size)
        throw std::invalid_argument("custom sound: bad audio parameters");
    if (s.frames < 2 || s.frames > kMaxFrames) throw std::invalid_argument("custom sound: bad length");
    if (s.tmpl.size() != static_cast<std::size_t>(s.frames) * kBands)
        throw std::invalid_argument("custom sound: template size mismatch");
    if (!(s.threshold > 0.f && s.threshold < 1.f)) throw std::invalid_argument("custom sound: bad threshold");
    if (!std::isfinite(s.min_level_db) || !(s.refractory_s >= 0.f) || !std::isfinite(s.refractory_s))
        throw std::invalid_argument("custom sound: bad parameters");
    if (s.refractory_s > kMaxRefractoryS || s.min_level_db < -140.f || s.min_level_db > 0.f)
        throw std::invalid_argument("custom sound: parameter out of range");
    if ((s.frame_size & (s.frame_size - 1)) != 0 || s.frame_size > 16384 || s.sample_rate > 192000)
        throw std::invalid_argument("custom sound: bad audio parameters");
    if (!s.env.empty() && s.env.size() != s.frames) throw std::invalid_argument("custom sound: envelope size mismatch");
    for (float v : s.tmpl)
        if (!std::isfinite(v)) throw std::invalid_argument("custom sound: non-finite template");
    for (float v : s.env)
        if (!std::isfinite(v)) throw std::invalid_argument("custom sound: non-finite envelope");
}

std::vector<std::uint8_t> serialize(const CustomSound& s) {
    validate_sound(s);
    std::vector<std::uint8_t> o(kMagic, kMagic + 4);
    put32(o, kVersion);
    put32(o, s.sample_rate);
    put32(o, s.frame_size);
    put32(o, s.hop_size);
    put32(o, static_cast<std::uint32_t>(kBands));
    put32(o, s.frames);
    putf(o, s.threshold);
    putf(o, s.min_level_db);
    putf(o, s.refractory_s);
    put32(o, static_cast<std::uint32_t>(s.name.size()));
    o.insert(o.end(), s.name.begin(), s.name.end());
    for (float v : s.tmpl) putf(o, v);
    put32(o, static_cast<std::uint32_t>(s.env.size()));
    for (float v : s.env) putf(o, v);
    put32(o, crc32(o.data(), o.size()));
    return o;
}

CustomSound deserialize_sound(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr) throw std::invalid_argument("custom sound: no data");
    Reader r{data, size};
    r.need(8);
    if (std::memcmp(r.p, kMagic, 4) != 0) throw std::invalid_argument("custom sound: bad magic");
    r.p += 4;
    r.left -= 4;
    const std::uint32_t version = r.u32();
    if (version != 1 && version != kVersion) throw std::invalid_argument("custom sound: unsupported version");
    if (version == kVersion) {  // integrity check over everything before the CRC
        if (size < 12) throw std::invalid_argument("custom sound: truncated data");
        const std::uint8_t* c = data + size - 4;
        const std::uint32_t stored = c[0] | (c[1] << 8) | (c[2] << 16) | (static_cast<std::uint32_t>(c[3]) << 24);
        if (crc32(data, size - 4) != stored) throw std::invalid_argument("custom sound: checksum mismatch");
        r.left -= 4;  // the CRC itself is not payload
    }

    CustomSound s;
    s.sample_rate = r.u32();
    s.frame_size = r.u32();
    s.hop_size = r.u32();
    if (r.u32() != kBands) throw std::invalid_argument("custom sound: band count mismatch");
    s.frames = r.u32();
    s.threshold = r.f32();
    s.min_level_db = r.f32();
    s.refractory_s = r.f32();
    const std::uint32_t nlen = r.u32();
    if (nlen == 0 || nlen > kMaxName) throw std::invalid_argument("custom sound: bad name");
    r.need(nlen);
    s.name.assign(reinterpret_cast<const char*>(r.p), nlen);
    r.p += nlen;
    r.left -= nlen;
    if (s.frames < 2 || s.frames > kMaxFrames) throw std::invalid_argument("custom sound: bad length");
    r.need(static_cast<std::size_t>(s.frames) * kBands * 4);
    s.tmpl.resize(static_cast<std::size_t>(s.frames) * kBands);
    for (auto& v : s.tmpl) v = r.f32();
    const std::uint32_t elen = r.u32();
    if (elen != 0 && elen != s.frames) throw std::invalid_argument("custom sound: envelope size mismatch");
    r.need(static_cast<std::size_t>(elen) * 4);
    s.env.resize(elen);
    for (auto& v : s.env) v = r.f32();
    if (r.left != 0) throw std::invalid_argument("custom sound: trailing data");
    validate_sound(s);
    return s;
}

bool prepare_window(const float* db, std::size_t n, float* out) {
    float mx = db[0];
    for (std::size_t i = 1; i < n; ++i) mx = std::max(mx, db[i]);
    const float lo = mx - kDynRangeDb;
    double mean = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = std::max(db[i], lo);
        mean += out[i];
    }
    mean /= static_cast<double>(n);
    double ss = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        out[i] -= static_cast<float>(mean);
        ss += static_cast<double>(out[i]) * out[i];
    }
    if (ss < 1e-6) return false;
    const float inv = static_cast<float>(1.0 / std::sqrt(ss));
    for (std::size_t i = 0; i < n; ++i) out[i] *= inv;
    return true;
}

// ---------------------------------------------------------------- CustomMatcher
void CustomMatcher::configure(double hop_s, double frame_s) {
    hop_s_ = hop_s;
    frame_s_ = frame_s;
}

int CustomMatcher::add(CustomSound s) {
    validate_sound(s);
    for (auto& sl : slots_) {
        if (sl.s.name == s.name) {
            const int id = sl.id;
            sl = Slot{};
            sl.id = id;
            sl.s = std::move(s);
            rebuild_ring();
            return id;
        }
    }
    Slot sl;
    sl.id = next_id_++;
    sl.s = std::move(s);
    slots_.push_back(std::move(sl));
    rebuild_ring();
    return slots_.back().id;
}

bool CustomMatcher::remove(const std::string& name) {
    const auto it = std::find_if(slots_.begin(), slots_.end(), [&](const Slot& x) { return x.s.name == name; });
    if (it == slots_.end()) return false;
    slots_.erase(it);
    rebuild_ring();
    return true;
}

const CustomSound* CustomMatcher::find(int id) const {
    for (const auto& sl : slots_)
        if (sl.id == id) return &sl.s;
    return nullptr;
}

std::vector<std::string> CustomMatcher::names() const {
    std::vector<std::string> v;
    for (const auto& sl : slots_) v.push_back(sl.s.name);
    return v;
}

void CustomMatcher::rebuild_ring() {  // new/removed sound: history restarts
    cap_ = 0;
    for (const auto& sl : slots_) cap_ = std::max<std::size_t>(cap_, sl.s.frames);
    ring_.assign(cap_ * kBands, 0.f);
    ring_level_.assign(cap_, 0.f);
    win_.assign(cap_ * kBands, 0.f);
    prep_.assign(cap_ * kBands, 0.f);
    lvl_.assign(cap_, 0.f);
    env_prep_.assign(cap_, 0.f);
    n_ = 0;
    for (auto& sl : slots_) {
        sl.above = false;
        sl.cooldown = 0;
    }
}

void CustomMatcher::reset() {
    n_ = 0;
    for (auto& sl : slots_) {
        sl.above = false;
        sl.cooldown = 0;
    }
}

void CustomMatcher::push(const float* bands, float level_db, double t, std::vector<Match>& out) {
    if (slots_.empty()) return;
    const std::size_t at = static_cast<std::size_t>(n_ % cap_);
    std::copy(bands, bands + kBands, ring_.begin() + static_cast<std::ptrdiff_t>(at * kBands));
    ring_level_[at] = level_db;
    ++n_;

    for (auto& sl : slots_) {
        if (sl.cooldown > 0) {
            --sl.cooldown;
            continue;
        }
        const std::size_t len = sl.s.frames;
        if (n_ < len) continue;

        float peak = -200.f;
        for (std::size_t j = 0; j < len; ++j) {
            const std::size_t idx = static_cast<std::size_t>((n_ - len + j) % cap_);
            std::copy(ring_.begin() + static_cast<std::ptrdiff_t>(idx * kBands),
                      ring_.begin() + static_cast<std::ptrdiff_t>((idx + 1) * kBands),
                      win_.begin() + static_cast<std::ptrdiff_t>(j * kBands));
            lvl_[j] = ring_level_[idx];
            peak = std::max(peak, ring_level_[idx]);
        }

        float score = -1.f;
        if (peak >= sl.s.min_level_db && prepare_window(win_.data(), len * kBands, prep_.data())) {
            double dot = 0.0;
            for (std::size_t i = 0; i < len * kBands; ++i) dot += static_cast<double>(prep_[i]) * sl.s.tmpl[i];
            score = static_cast<float>(dot);
            if (!sl.s.env.empty()) {  // also require the same on/off rhythm
                double e = 0.0;
                if (prepare_window(lvl_.data(), len, env_prep_.data()))
                    for (std::size_t i = 0; i < len; ++i) e += static_cast<double>(env_prep_[i]) * sl.s.env[i];
                score = 0.5f * (score + static_cast<float>(e));
            }
        }

        bool fire = false;
        if (score >= sl.s.threshold) {
            if (!sl.above || score > sl.best) {
                sl.above = true;
                sl.best = score;
                sl.best_t = t;
                sl.best_level = peak;
                sl.since_best = 0;
            } else if (++sl.since_best >= kPeakConfirmFrames) {
                fire = true;  // no improvement for a few frames: `best` was the peak
            }
        } else if (sl.above) {
            fire = true;
        }

        if (fire) {
            const double start = sl.best_t - frame_s_ - static_cast<double>(len - 1) * hop_s_;
            out.push_back({sl.id, sl.best, sl.best_t, start, sl.best_level});
            sl.above = false;
            sl.cooldown = static_cast<int>(std::lround(sl.s.refractory_s / hop_s_));
        }
    }
}

}  // namespace ambient
