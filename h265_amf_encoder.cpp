#include "h265_amf_encoder.h"

const EncoderInfo H265AMFEncoder::encoderInfo = {
    .UUID{0xa6, 0xd3, 0xdd, 0x95, 0x33, 0xc4, 0x41, 0x1e, 0xbe, 0xe7, 0x21, 0x40, 0x8b, 0xd6, 0xe4, 0xfc},
    .codecGroup = "H.265",
    .fourCC = 'hvc1',
    .encoder = "hevc_amf",
    .hwAcceleration = AMF,
    .qualityModes = CQP | VBR | CBR,
    .qp = {1, 25, 51},
    .presets = {{0, "Quality"}, {1, "Balanced"}, {2, "Speed"}},
    .defaultPreset = 1,
    .formats =
        {
            {
                .codecName = "AMF 8-bit 4:2:0 (FFmpeg)",
                .bitDepth = 8,
                .colorModel = clrNV12,
                .hSubsampling = 2,
                .vSubsampling = 2,
                .pixelFormat = AV_PIX_FMT_NV12,
            },
            {
                .codecName = "AMF 10-bit 4:2:0 (FFmpeg)",
                .bitDepth = 10,
                .colorModel = clrNV12,
                .hSubsampling = 2,
                .vSubsampling = 2,
                .pixelFormat = AV_PIX_FMT_P010,
            },
        },
};

H265AMFEncoder::H265AMFEncoder(const int formatIndex) {
    FFmpegEncoder::encoderInfo = encoderInfo;
    FFmpegEncoder::formatIndex = formatIndex;
}

StatusCode H265AMFEncoder::RegisterCodecs(HostListRef* list) {
    return FFmpegEncoder::RegisterCodecs(list, encoderInfo);
}

StatusCode H265AMFEncoder::GetEncoderSettings(HostPropertyCollectionRef* values, HostListRef* settingsList) {
    return FFmpegEncoder::GetEncoderSettings(values, settingsList, encoderInfo);
}
