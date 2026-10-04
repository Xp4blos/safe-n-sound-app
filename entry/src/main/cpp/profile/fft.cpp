#include "fft.h"

#include <cmath>
#include <complex>
#include <vector>

namespace sns {

namespace {
constexpr double kPi = 3.14159265358979323846;

void BitReverse(std::vector<std::complex<float>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
}
}  // namespace

void RealFftMagnitude(const float* in, float* outMagnitude) {
    std::vector<std::complex<float>> a(kFftSize);
    for (int i = 0; i < kFftSize; ++i) a[i] = in[i];
    BitReverse(a);
    for (int len = 2; len <= kFftSize; len <<= 1) {
        const double ang = -2.0 * kPi / len;
        const std::complex<float> wl(static_cast<float>(std::cos(ang)), static_cast<float>(std::sin(ang)));
        for (int i = 0; i < kFftSize; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (int k = 0; k < len / 2; ++k) {
                const std::complex<float> u = a[i + k];
                const std::complex<float> v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    for (int i = 0; i < kFftBins; ++i) outMagnitude[i] = std::abs(a[i]);
}

}  // namespace sns
