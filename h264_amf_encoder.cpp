#include "h264_amf_encoder.h"

namespace IOPlugin {

const EncoderDescriptor H264AMFEncoder::descriptor = {
    .uuid = {0x2e, 0xf3, 0x3a, 0x3f, 0xb9, 0x17, 0x44, 0xdf, 0xa4, 0x93, 0x8b, 0x9a, 0x77, 0x66, 0xcf, 0x81},
    .group = "H.264",
    .fourCC = MakeFourCC('a', 'v', 'c', '1'),
    .amfComponent = AMFVideoEncoderVCE_AVC,
    .settingsId = "h264",
    .rateControls = static_cast<int32_t>(RateControl::CQP) | static_cast<int32_t>(RateControl::VBR) |
                    static_cast<int32_t>(RateControl::CBR),
    .qualityMin = 0,
    .qualityDefault = 20,
    .qualityMax = 51,
    .defaultPreset = 1,
    .presets = {{3, "High Quality"}, {0, "Quality"}, {1, "Balanced"}, {2, "Speed"}},
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
        },
};

H264AMFEncoder::H264AMFEncoder(const uint32_t formatIndex) : AMFEncoder(descriptor, formatIndex) {}

StatusCode H264AMFEncoder::RegisterCodecs(HostListRef* list) {
    return AMFEncoder::RegisterCodecs(list, descriptor);
}

StatusCode H264AMFEncoder::GetEncoderSettings(HostPropertyCollectionRef* values, HostListRef* settingsList) {
    return AMFEncoder::GetEncoderSettings(values, settingsList, descriptor);
}

}
