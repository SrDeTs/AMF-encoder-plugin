#include "h264_amf_encoder.h"

const EncoderInfo H264AMFEncoder::encoderInfo = {
    .UUID{0x2e, 0xf3, 0x3a, 0x3f, 0xb9, 0x17, 0x44, 0xdf, 0xa4, 0x93, 0x8b, 0x9a, 0x77, 0x66, 0xcf, 0x81},
    .codecGroup = "H.264",
    .fourCC = 'avc1',
    .encoder = "h264_amf",
    .hwAcceleration = AMF,
    .qualityModes = CQP | VBR | CBR,
    .qp = {1, 20, 51},
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
        },
};

H264AMFEncoder::H264AMFEncoder(const int formatIndex) {
    FFmpegEncoder::encoderInfo = encoderInfo;
    FFmpegEncoder::formatIndex = formatIndex;
}

StatusCode H264AMFEncoder::RegisterCodecs(HostListRef* list) {
    return FFmpegEncoder::RegisterCodecs(list, encoderInfo);
}

StatusCode H264AMFEncoder::GetEncoderSettings(HostPropertyCollectionRef* values, HostListRef* settingsList) {
    return FFmpegEncoder::GetEncoderSettings(values, settingsList, encoderInfo);
}
