#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "hevc_config.h"

int main() {
    const std::vector<uint8_t> annexB = {
        0x00, 0x00, 0x00, 0x01, 0x26, 0x01, 0xaa,
        0x00, 0x00, 0x01, 0x02, 0xbb, 0xcc,
    };
    const std::vector<uint8_t> expected = {
        0x00, 0x00, 0x00, 0x03, 0x26, 0x01, 0xaa,
        0x00, 0x00, 0x00, 0x03, 0x02, 0xbb, 0xcc,
    };
    std::vector<uint8_t> output;
    std::string error;
    if (!IOPlugin::ConvertHevcSampleToLengthPrefixed(annexB.data(), annexB.size(), output, error) ||
        output != expected) {
        std::cerr << "Annex B conversion failed: " << error << '\n';
        return 1;
    }

    if (!IOPlugin::ConvertHevcSampleToLengthPrefixed(expected.data(), expected.size(), output, error) ||
        output != expected) {
        std::cerr << "Length-prefixed pass-through failed: " << error << '\n';
        return 1;
    }

    const std::vector<uint8_t> invalid = {0x01, 0x02, 0x03};
    if (IOPlugin::ConvertHevcSampleToLengthPrefixed(invalid.data(), invalid.size(), output, error) || error.empty()) {
        std::cerr << "Invalid HEVC packet was accepted\n";
        return 1;
    }
    return 0;
}
