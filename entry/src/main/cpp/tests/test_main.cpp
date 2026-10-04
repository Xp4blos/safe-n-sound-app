#include <cstdio>

#include "../profile/types.h"
#include "synth.h"
#include "test_util.h"

TEST(harness_runs) {
    CHECK(sns::kFrameSize == 512);
}

TEST(synth_tone_length) {
    CHECK(Tone(1000, 0.5, 0.5).size() == 8000);
}

int main() {
    int failedCases = 0;
    for (const auto& c : sns_test::Registry()) {
        const int before = sns_test::Failures();
        c.fn();
        const bool ok = sns_test::Failures() == before;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", c.name);
        if (!ok) ++failedCases;
    }
    std::printf("%zu tests, %d failed\n", sns_test::Registry().size(), failedCases);
    return failedCases == 0 ? 0 : 1;
}
