#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "h264_config.h"

int main() {
    const std::vector<uint8_t> header = {
        0x00, 0x00, 0x00, 0x01, 0x67, 0x4d, 0x00, 0x2a, 0xaa,
        0x00, 0x00, 0x01, 0x68, 0xee, 0x3c, 0x80,
    };
    const std::vector<uint8_t> expectedHeader = {
        0x01, 0x4d, 0x00, 0x2a, 0xff, 0xe1, 0x00, 0x05, 0x67, 0x4d, 0x00, 0x2a, 0xaa,
        0x01, 0x00, 0x04, 0x68, 0xee, 0x3c, 0x80,
    };

    std::vector<uint8_t> output;
    std::string error;
    if (!IOPlugin::BuildH264DecoderConfigurationRecord(header.data(), header.size(), output, error) ||
        output != expectedHeader) {
        std::cerr << "avcC conversion failed: " << error << '\n';
        return 1;
    }
    if (!IOPlugin::BuildH264DecoderConfigurationRecord(expectedHeader.data(), expectedHeader.size(), output, error) ||
        output != expectedHeader) {
        std::cerr << "avcC pass-through failed: " << error << '\n';
        return 1;
    }

    const std::vector<uint8_t> annexB = {
        0x00, 0x00, 0x00, 0x01, 0x09, 0x10,
        0x00, 0x00, 0x01, 0x65, 0xaa, 0xbb,
    };
    const std::vector<uint8_t> expectedSample = {
        0x00, 0x00, 0x00, 0x02, 0x09, 0x10,
        0x00, 0x00, 0x00, 0x03, 0x65, 0xaa, 0xbb,
    };
    if (!IOPlugin::ConvertH264SampleToLengthPrefixed(annexB.data(), annexB.size(), output, error) ||
        output != expectedSample) {
        std::cerr << "H.264 Annex B conversion failed: " << error << '\n';
        return 1;
    }
    if (!IOPlugin::ConvertH264SampleToLengthPrefixed(expectedSample.data(), expectedSample.size(), output, error) ||
        output != expectedSample) {
        std::cerr << "H.264 length-prefixed pass-through failed: " << error << '\n';
        return 1;
    }
    if (!IOPlugin::H264SampleContainsIdr(annexB.data(), annexB.size()) ||
        !IOPlugin::H264SampleContainsIdr(expectedSample.data(), expectedSample.size())) {
        std::cerr << "H.264 IDR detection failed\n";
        return 1;
    }

    const std::vector<uint8_t> invalid = {0x01, 0x02, 0x03};
    if (IOPlugin::ConvertH264SampleToLengthPrefixed(invalid.data(), invalid.size(), output, error) || error.empty()) {
        std::cerr << "Invalid H.264 packet was accepted\n";
        return 1;
    }
    return 0;
}
