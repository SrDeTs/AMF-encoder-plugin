#include "av1_container_fix.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

bool Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

std::vector<std::uint8_t> MakeResolveMkvVideoTrack() {
    return {
        0x1A, 0x45, 0xDF, 0xA3, 0x80,
        0x18, 0x53, 0x80, 0x67, 0xA7,
        0xEC, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x16, 0x54, 0xAE, 0x6B, 0x9A,
        0xAE, 0x98,
        0x86, 0x85, 'V',  '_',  'A',  'V',  '1',
        0xE0, 0x8F,
        0xB0, 0x82, 0x07, 0x80,
        0xBA, 0x82, 0x04, 0x38,
        0x9A, 0x81, 0x02,
        0x54, 0xB2, 0x81, 0x04,
    };
}

}  // namespace

int main() {
    auto data = MakeResolveMkvVideoTrack();
    const auto original_size = data.size();
    const auto result = amf_plugin::FixAv1Mkv1080(data);
    bool ok = Check(result.status == amf_plugin::Av1ContainerFixStatus::Fixed,
                    "Resolve-style MKV must be fixed");
    ok &= Check(data.size() == original_size, "file size must remain unchanged");
    const std::vector<std::uint8_t> height = {0xBA, 0x82, 0x04, 0x3A};
    const std::vector<std::uint8_t> crop = {0x54, 0xAA, 0x81, 0x02};
    const std::vector<std::uint8_t> display = {0x54, 0xBA, 0x82, 0x04, 0x38};
    ok &= Check(std::search(data.begin(), data.end(), height.begin(), height.end()) !=
                    data.end(),
                "PixelHeight must become 1082");
    ok &= Check(std::search(data.begin(), data.end(), crop.begin(), crop.end()) != data.end(),
                "DisplayUnit must become PixelCropBottom=2");
    ok &= Check(std::search(data.begin(), data.end(), display.begin(), display.end()) !=
                    data.end(),
                "DisplayHeight must become 1080");

    const auto second = amf_plugin::FixAv1Mkv1080(data);
    ok &= Check(second.status == amf_plugin::Av1ContainerFixStatus::AlreadyFixed,
                "fix must be idempotent");

    auto invalid = MakeResolveMkvVideoTrack();
    invalid[27] = 'H';
    const auto unsupported = amf_plugin::FixAv1Mkv1080(invalid);
    ok &= Check(unsupported.status == amf_plugin::Av1ContainerFixStatus::Unsupported,
                "non-AV1 track must be rejected");
    return ok ? 0 : 1;
}
