#include "av1_amf_encoder.h"

namespace IOPlugin {

const EncoderDescriptor Av1AMFEncoder::descriptor = {
    .uuid = {0x03, 0xe3, 0x29, 0x7a, 0x03, 0x93, 0x46, 0xf9, 0xad, 0x7d, 0x22, 0xdd, 0xe1, 0x60, 0x95, 0xa8},
    .group = "AV1",
    .fourCC = MakeFourCC('a', 'v', '0', '1'),
    .amfComponent = AMFVideoEncoder_AV1,
    .settingsId = "av1",
    .rateControls = static_cast<int32_t>(RateControl::CQP) | static_cast<int32_t>(RateControl::VBR) |
                    static_cast<int32_t>(RateControl::CBR),
    .qualityMin = 1,
    .qualityDefault = 100,
    .qualityMax = 255,
    .defaultPreset = 1,
    .presets = {{0, "High Quality"}, {1, "Quality"}, {2, "Balanced"}, {3, "Speed"}},
    .formats =
        {
            {
                .name = "AMF 8-bit 4:2:0",
                .bitDepth = 8,
                .colorModel = clrNV12,
                .hSubsampling = 2,
                .vSubsampling = 2,
                .surfaceFormat = amf::AMF_SURFACE_NV12,
            },
            {
                .name = "AMF 10-bit 4:2:0",
                .bitDepth = 10,
                .colorModel = clrNV12,
                .hSubsampling = 2,
                .vSubsampling = 2,
                .surfaceFormat = amf::AMF_SURFACE_P010,
            },
        },
    .containers = {"mp4", "mov", "mkv"},
};

Av1AMFEncoder::Av1AMFEncoder(const uint32_t formatIndex) : AMFEncoder(descriptor, formatIndex) {}

StatusCode Av1AMFEncoder::RegisterCodecs(HostListRef* list) {
    return AMFEncoder::RegisterCodecs(list, descriptor);
}

StatusCode Av1AMFEncoder::GetEncoderSettings(HostPropertyCollectionRef* values, HostListRef* settingsList) {
    return AMFEncoder::GetEncoderSettings(values, settingsList, descriptor);
}

}
