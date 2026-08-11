#include "h265_amf_encoder.h"

namespace IOPlugin {

const EncoderDescriptor H265AMFEncoder::descriptor = {
    .uuid = {0x68, 0x32, 0x65, 0x35, 0xa7, 0xd1, 0x48, 0x91, 0x92, 0x76, 0x45, 0xb8, 0x1c, 0xe4, 0x70, 0x30},
    .group = "H.265",
    .fourCC = MakeFourCC('h', 'v', 'c', '1'),
    .amfComponent = AMFVideoEncoder_HEVC,
    .settingsId = "h265",
    .rateControls = static_cast<int32_t>(RateControl::CQP) | static_cast<int32_t>(RateControl::VBR) |
                    static_cast<int32_t>(RateControl::CBR),
    .qualityMin = 0,
    .qualityDefault = 22,
    .qualityMax = 51,
    .defaultPreset = 1,
    .presets = {{3, "High Quality"}, {0, "Quality"}, {1, "Balanced"}, {2, "Speed"}},
    .formats =
        {
            {
                .name = "AMF 8-bit 4:2:0",
                .bitDepth = 8,
                .sampleBits = 8,
                .colorModel = clrUYVY,
                .hSubsampling = 2,
                .vSubsampling = 1,
                .surfaceFormat = amf::AMF_SURFACE_NV12,
            },
            {
                .name = "AMF 10-bit 4:2:0",
                .bitDepth = 10,
                .sampleBits = 16,
                .colorModel = clrNV12,
                .hSubsampling = 2,
                .vSubsampling = 2,
                .advertiseSubsampling = false,
                .configureInputOnInit = false,
                .surfaceFormat = amf::AMF_SURFACE_P010,
            },
        },
    .containers = {"mp4", "mov"},
};

H265AMFEncoder::H265AMFEncoder(const uint32_t formatIndex) : AMFEncoder(descriptor, formatIndex) {}

StatusCode H265AMFEncoder::RegisterCodecs(HostListRef* list) {
    return AMFEncoder::RegisterCodecs(list, descriptor);
}

StatusCode H265AMFEncoder::GetEncoderSettings(HostPropertyCollectionRef* values, HostListRef* settingsList) {
    return AMFEncoder::GetEncoderSettings(values, settingsList, descriptor);
}

}
