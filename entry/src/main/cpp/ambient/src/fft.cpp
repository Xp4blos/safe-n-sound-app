#include "ambient/fft.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace ambient {

Fft::Fft(std::size_t n) : n_(n) {
    if (n < 2 || (n & (n - 1)) != 0) {
        throw std::invalid_argument("Fft: size must be a power of two >= 2");
    }
    std::size_t bits = 0;
    while ((std::size_t{1} << bits) < n) ++bits;

    rev_.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        std::size_t r = 0;
        for (std::size_t b = 0; b < bits; ++b) {
            if (i & (std::size_t{1} << b)) r |= std::size_t{1} << (bits - 1 - b);
        }
        rev_[i] = r;
    }

    const double pi = std::acos(-1.0);
    tw_.resize(n / 2);
    for (std::size_t k = 0; k < n / 2; ++k) {
        const double a = -2.0 * pi * static_cast<double>(k) / static_cast<double>(n);
        tw_[k] = {static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a))};
    }
}

void Fft::forward(std::complex<float>* a) const {
    for (std::size_t i = 0; i < n_; ++i) {
        if (i < rev_[i]) std::swap(a[i], a[rev_[i]]);
    }
    for (std::size_t len = 2; len <= n_; len <<= 1) {
        const std::size_t half = len / 2;
        const std::size_t step = n_ / len;
        for (std::size_t i = 0; i < n_; i += len) {
            for (std::size_t j = 0; j < half; ++j) {
                const std::complex<float> u = a[i + j];
                const std::complex<float> v = a[i + j + half] * tw_[j * step];
                a[i + j] = u + v;
                a[i + j + half] = u - v;
            }
        }
    }
}

}  // namespace ambient
