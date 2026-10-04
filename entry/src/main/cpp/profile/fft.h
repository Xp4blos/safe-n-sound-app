#pragma once

namespace sns {

constexpr int kFftSize = 512;
constexpr int kFftBins = kFftSize / 2 + 1;

// Magnitude spectrum (bins 0..kFftSize/2) of a real input of kFftSize samples. Scaled so that a
// full-scale sine bin has magnitude ~ kFftSize/4 when Hann-windowed by the caller.
void RealFftMagnitude(const float* in, float* outMagnitude);

}  // namespace sns
