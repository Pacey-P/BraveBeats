#pragma once

// Minimal RIFF/WAVE writer for the rendered piece
// 16 and 24 bit PCM, interleaved stereo, little endian

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace bravebeats {

class WavWriter {
public:
    static bool write(const std::string &path,
                      const std::vector<float> &interleaved,
                      int channels,
                      int sampleRate,
                      int bitDepth = 24) {
        if (channels <= 0 || sampleRate <= 0) return false;
        if (bitDepth != 16 && bitDepth != 24) return false;

        const int bytesPerSample = bitDepth / 8;
        const uint32_t frames = static_cast<uint32_t>(interleaved.size() / static_cast<std::size_t>(channels));
        const uint32_t dataBytes = frames * static_cast<uint32_t>(channels * bytesPerSample);

        FILE *file = std::fopen(path.c_str(), "wb");
        if (!file) return false;

        writeTag(file, "RIFF");
        writeU32(file, 36u + dataBytes);
        writeTag(file, "WAVE");

        writeTag(file, "fmt ");
        writeU32(file, 16u);
        writeU16(file, 1u);  // PCM
        writeU16(file, static_cast<uint16_t>(channels));
        writeU32(file, static_cast<uint32_t>(sampleRate));
        writeU32(file, static_cast<uint32_t>(sampleRate * channels * bytesPerSample));
        writeU16(file, static_cast<uint16_t>(channels * bytesPerSample));
        writeU16(file, static_cast<uint16_t>(bitDepth));

        writeTag(file, "data");
        writeU32(file, dataBytes);

        std::vector<uint8_t> block;
        block.reserve(interleaved.size() * static_cast<std::size_t>(bytesPerSample));
        for (float sample : interleaved) {
            const int32_t quantised = quantise(sample, bitDepth);
            for (int byte = 0; byte < bytesPerSample; ++byte) {
                block.push_back(static_cast<uint8_t>((quantised >> (8 * byte)) & 0xFF));
            }
        }
        const bool ok = block.empty() ||
                        std::fwrite(block.data(), 1, block.size(), file) == block.size();
        std::fclose(file);
        return ok;
    }

private:
    static int32_t quantise(float sample, int bitDepth) {
        if (!std::isfinite(sample)) sample = 0.0f;
        if (sample > 1.0f) sample = 1.0f;
        if (sample < -1.0f) sample = -1.0f;
        // Scale by the positive maximum so full-scale input never wraps
        const float peak = bitDepth == 16 ? 32767.0f : 8388607.0f;
        return static_cast<int32_t>(std::lround(sample * peak));
    }

    static void writeTag(FILE *file, const char *tag) { std::fwrite(tag, 1, 4, file); }

    static void writeU32(FILE *file, uint32_t value) {
        const uint8_t bytes[4] = {static_cast<uint8_t>(value & 0xFF),
                                  static_cast<uint8_t>((value >> 8) & 0xFF),
                                  static_cast<uint8_t>((value >> 16) & 0xFF),
                                  static_cast<uint8_t>((value >> 24) & 0xFF)};
        std::fwrite(bytes, 1, 4, file);
    }

    static void writeU16(FILE *file, uint16_t value) {
        const uint8_t bytes[2] = {static_cast<uint8_t>(value & 0xFF),
                                  static_cast<uint8_t>((value >> 8) & 0xFF)};
        std::fwrite(bytes, 1, 2, file);
    }
};

}  // namespace bravebeats
