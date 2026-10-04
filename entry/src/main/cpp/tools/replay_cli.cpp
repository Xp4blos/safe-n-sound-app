// Dev tool: replays a recording (16 kHz mono 16-bit raw PCM or WAV) through SoundEngine the way the app does and
// prints what happens: alarm and custom events, and when the first alarm is learned. Usage:
//   replay_cli <file.pcm|file.wav> [match_threshold]
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "sound_engine.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: replay_cli <file.pcm|file.wav> [match_threshold]\n");
        return 2;
    }
    FILE* f = std::fopen(argv[1], "rb");
    if (!f) {
        std::fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    std::vector<unsigned char> bytes;
    unsigned char buf[8192];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) bytes.insert(bytes.end(), buf, buf + n);
    std::fclose(f);
    size_t offset = 0;
    if (bytes.size() > 44 && std::memcmp(bytes.data(), "RIFF", 4) == 0) offset = 44;  // plain 44-byte WAV header
    std::vector<int16_t> pcm((bytes.size() - offset) / 2);
    std::memcpy(pcm.data(), bytes.data() + offset, pcm.size() * 2);

    sns::SoundEngine engine(sns::kSampleRate);
    if (argc > 2) engine.SetMatchThreshold(static_cast<float>(std::atof(argv[2])));
    double learnAt = -1.0, learnEventTime = 0.0;
    int learned = 0;
    for (size_t pos = 0; pos < pcm.size(); pos += 480) {
        const size_t len = std::min<size_t>(480, pcm.size() - pos);
        const double now = static_cast<double>(pos + len) / sns::kSampleRate;
        const auto out = engine.Process(pcm.data() + pos, len);
        for (const auto& e : out.events) {
            std::printf("%7.2fs  %-6s  start=%.2f freq=%.0f level=%.0f %s\n", e.timeSec, e.type.c_str(), e.startSec,
                        e.freqHz, e.levelDb, e.label.c_str());
            if (e.type == "alarm" && learned == 0 && learnAt < 0) {
                learnAt = now + 3.7;
                learnEventTime = e.timeSec;
            }
        }
        if (learnAt >= 0 && now >= learnAt) {
            const auto r = engine.LearnFromRing("snd-1", learnEventTime);
            std::printf("%7.2fs  LEARN  ok=%d %s\n", now, r.ok ? 1 : 0, r.message.c_str());
            learned = 1;
            learnAt = -1.0;
        }
    }
    return 0;
}
