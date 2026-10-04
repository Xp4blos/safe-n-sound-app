#pragma once
#include <complex>
#include <cstddef>
#include <vector>

namespace ambient {

// In-place radix-2 FFT (decimation in time). Size must be a power of two.
class Fft {
public:
    explicit Fft(std::size_t n);  // throws std::invalid_argument
    std::size_t size() const { return n_; }
    void forward(std::complex<float>* data) const;

private:
    std::size_t n_;
    std::vector<std::size_t> rev_;
    std::vector<std::complex<float>> tw_;
};

}  // namespace ambient
