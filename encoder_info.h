#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <AMF/core/Surface.h>

namespace IOPlugin {

constexpr uint32_t MakeFourCC(char a, char b, char c, char d) {
    return (static_cast<uint32_t>(static_cast<uint8_t>(a)) << 24) |
           (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 8) |
           static_cast<uint32_t>(static_cast<uint8_t>(d));
}

static_assert(MakeFourCC('a', 'v', 'c', '1') == 0x61766331U);

enum class RateControl : int32_t {
    CQP = 1,
    VBR = 2,
    CBR = 4,
};

struct EncoderFormat {
    const char* name{};
    int32_t bitDepth{8};
    uint32_t colorModel{};
    uint8_t hSubsampling{2};
    uint8_t vSubsampling{2};
    amf::AMF_SURFACE_FORMAT surfaceFormat{amf::AMF_SURFACE_UNKNOWN};
};

struct PresetOption {
    int32_t value{};
    const char* label{};
};

struct EncoderDescriptor {
    std::array<uint8_t, 16> uuid{};
    const char* group{};
    uint32_t fourCC{};
    const wchar_t* amfComponent{};
    const char* settingsId{};
    int32_t rateControls{};
    int32_t qualityMin{1};
    int32_t qualityDefault{20};
    int32_t qualityMax{51};
    int32_t defaultPreset{};
    std::vector<PresetOption> presets{};
    std::vector<EncoderFormat> formats{};
};

}
