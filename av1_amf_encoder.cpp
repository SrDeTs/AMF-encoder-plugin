#include "av1_amf_encoder.h"

const EncoderInfo Av1AMFEncoder::encoderInfo = {
    .UUID{0x03, 0xe3, 0x29, 0x7a, 0x03, 0x93, 0x46, 0xf9, 0xad, 0x7d, 0x22, 0xdd, 0xe1, 0x60, 0x95, 0xa8},
    .codecGroup = "AV1",
    .fourCC = 'av01',
    .encoder = "av1_amf",
    .hwAcceleration = AMF,
    .qualityModes = CQP | VBR | CBR,
    .qp = {1, 25, 63},
    .presets = {{0, "High Quality"}, {1, "Quality"}, {2, "Balanced"}, {3, "Speed"}},
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

Av1AMFEncoder::Av1AMFEncoder(const int formatIndex) {
    FFmpegEncoder::encoderInfo = encoderInfo;
    FFmpegEncoder::formatIndex = formatIndex;
}

StatusCode Av1AMFEncoder::RegisterCodecs(HostListRef* list) {
    return FFmpegEncoder::RegisterCodecs(list, encoderInfo);
}

StatusCode Av1AMFEncoder::GetEncoderSettings(HostPropertyCollectionRef* values, HostListRef* settingsList) {
    return FFmpegEncoder::GetEncoderSettings(values, settingsList, encoderInfo);
}
