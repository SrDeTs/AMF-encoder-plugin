#include "amf_encoder.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#include "hevc_config.h"
#include "rgb16_to_p010.h"

extern "C" AMF_RESULT AMF_CDECL_CALL AMFInit(amf_uint64 version, amf::AMFFactory** factory);

namespace IOPlugin {

namespace {

std::string ResultText(const AMF_RESULT result) {
    return "AMF result " + std::to_string(static_cast<int>(result));
}

bool IsAv1(const EncoderDescriptor& descriptor) {
    return descriptor.fourCC == MakeFourCC('a', 'v', '0', '1');
}

bool IsHevc(const EncoderDescriptor& descriptor) {
    return descriptor.fourCC == MakeFourCC('h', 'v', 'c', '1');
}

}  // namespace

AMFEncoder::AMFEncoder(const EncoderDescriptor& descriptor, const uint32_t formatIndex)
    : descriptor(descriptor), formatIndex(formatIndex) {}

AMFEncoder::~AMFEncoder() {
    ReleaseResources();
}

StatusCode AMFEncoder::SetError(HostBufferRef* buffer, const StatusCode status, const std::string& message) const {
    g_Log(logLevelError, "AMF encoder: %s", message.c_str());
    if (buffer != nullptr && buffer->IsValid()) {
        buffer->SetProperty(pIOCustomErrorString, propTypeString, message.c_str(), static_cast<int>(message.size()));
    }
    return status;
}

void AMFEncoder::ReleaseResources() {
    if (encoder != nullptr) {
        encoder->Terminate();
        encoder->Release();
        encoder = nullptr;
    }
    if (context != nullptr) {
        context->Terminate();
        context->Release();
        context = nullptr;
    }
    factory = nullptr;
    drainRequested = false;
    inputLayoutLogged = false;
    inputAlignmentKnown = false;
    shift10BitSamples = false;
}

StatusCode AMFEncoder::DoInit(HostPropertyCollectionRef* properties) {
    if (properties == nullptr || descriptor.formats.empty() || formatIndex >= descriptor.formats.size()) return errNone;

    const EncoderFormat& format = descriptor.formats[formatIndex];
    const uint32_t bitDepth = static_cast<uint32_t>(format.bitDepth);
    const uint32_t sampleBits = format.sampleBits != 0 ? format.sampleBits : static_cast<uint32_t>(format.bitDepth);
    g_Log(logLevelInfo, "AMF encoder: init %s, color model %u, bit depth %u, sample bits %u", format.name,
          format.colorModel, bitDepth, sampleBits);
    if (!format.configureInputOnInit) return errNone;

    if (const StatusCode status = properties->SetProperty(pIOPropColorModel, propTypeUInt32, &format.colorModel, 1);
        status != errNone) {
        return status;
    }
    if (format.advertiseSubsampling) {
        if (const StatusCode status =
                properties->SetProperty(pIOPropHSubsampling, propTypeUInt8, &format.hSubsampling, 1);
            status != errNone) {
            return status;
        }
        if (const StatusCode status =
                properties->SetProperty(pIOPropVSubsampling, propTypeUInt8, &format.vSubsampling, 1);
            status != errNone) {
            return status;
        }
    }
    return errNone;
}

StatusCode AMFEncoder::RegisterCodecs(HostListRef* list, const EncoderDescriptor& descriptor) {
    if (list == nullptr || !list->IsValid()) return errInvalidParam;

    for (size_t index = 0; index < descriptor.formats.size(); ++index) {
        const EncoderFormat& format = descriptor.formats[index];
        HostPropertyCollectionRef codecInfo;
        if (!codecInfo.IsValid()) return errAlloc;

        std::array<uint8_t, 16> uuid = descriptor.uuid;
        uuid[15] = static_cast<uint8_t>(uuid[15] + index);

        const uint32_t mediaType = mediaVideo;
        const uint32_t direction = dirEncode;
        const uint8_t hardwareAcceleration = 1;
        const uint8_t dataRange[] = {0, 1};
        const uint32_t temporalReordering = 0;
        const uint32_t bitDepth = static_cast<uint32_t>(format.bitDepth);
        const uint32_t sampleBits = format.sampleBits != 0 ? format.sampleBits : static_cast<uint32_t>(format.bitDepth);
        const uint8_t fields = fieldProgressive | fieldTop | fieldBottom;
        const uint8_t threadSafe = 0;
        std::string containerList;
        for (size_t containerIndex = 0; containerIndex < descriptor.containers.size(); ++containerIndex) {
            containerList.append(descriptor.containers[containerIndex]);
            if (containerIndex + 1 < descriptor.containers.size()) containerList.push_back('\0');
        }

        codecInfo.SetProperty(pIOPropUUID, propTypeUInt8, uuid.data(), static_cast<int>(uuid.size()));
        codecInfo.SetProperty(pIOPropGroup, propTypeString, descriptor.group, static_cast<int>(std::strlen(descriptor.group)));
        codecInfo.SetProperty(pIOPropName, propTypeString, format.name, static_cast<int>(std::strlen(format.name)));
        codecInfo.SetProperty(pIOPropFourCC, propTypeUInt32, &descriptor.fourCC, 1);
        codecInfo.SetProperty(pIOPropMediaType, propTypeUInt32, &mediaType, 1);
        codecInfo.SetProperty(pIOPropCodecDirection, propTypeUInt32, &direction, 1);
        codecInfo.SetProperty(pIOPropColorModel, propTypeUInt32, &format.colorModel, 1);
        if (format.advertiseSubsampling) {
            codecInfo.SetProperty(pIOPropHSubsampling, propTypeUInt8, &format.hSubsampling, 1);
            codecInfo.SetProperty(pIOPropVSubsampling, propTypeUInt8, &format.vSubsampling, 1);
        }
        codecInfo.SetProperty(pIOPropDataRange, propTypeUInt8, dataRange, 2);
        codecInfo.SetProperty(pIOPropBitDepth, propTypeUInt32, &bitDepth, 1);
        codecInfo.SetProperty(pIOPropBitsPerSample, propTypeUInt32, &sampleBits, 1);
        codecInfo.SetProperty(pIOPropTemporalReordering, propTypeUInt32, &temporalReordering, 1);
        codecInfo.SetProperty(pIOPropFieldOrder, propTypeUInt8, &fields, 1);
        codecInfo.SetProperty(pIOPropThreadSafe, propTypeUInt8, &threadSafe, 1);
        codecInfo.SetProperty(pIOPropHWAcc, propTypeUInt8, &hardwareAcceleration, 1);
        codecInfo.SetProperty(pIOPropContainerList, propTypeString, containerList.data(),
                              static_cast<int>(containerList.size()));

        if (!list->Append(&codecInfo)) return errFail;
    }
    return errNone;
}

StatusCode AMFEncoder::GetEncoderSettings(HostPropertyCollectionRef* values, HostListRef* settingsList,
                                          const EncoderDescriptor& descriptor) {
    if (values == nullptr || settingsList == nullptr) return errInvalidParam;
    AmfSettings settings(descriptor);
    settings.Load(values);
    return settings.AppendTo(settingsList);
}

StatusCode AMFEncoder::ConfigureEncoder(HostBufferRef* buffer, const uint32_t frameRateNum, const uint32_t frameRateDen) {
    if (encoder == nullptr || settings == nullptr) return errInvalidOperation;

    const EncoderFormat& format = descriptor.formats[formatIndex];
    const bool av1 = IsAv1(descriptor);
    const bool hevc = IsHevc(descriptor);
    const AMFSize frameSize = AMFConstructSize(width, height);
    const AMFRate frameRate = AMFConstructRate(frameRateNum, frameRateDen);
    const amf_int64 quality = settings->GetQualityFactor();
    const amf_int64 targetBitrate = settings->GetBitRate();
    const amf_int64 peakBitrate = settings->GetRateControl() == RateControl::CBR ? targetBitrate : settings->GetMaxBitRate();
    const amf_int64 bufferSize = settings->GetBufferSize();
    AMF_RESULT result = AMF_OK;

    if (av1) {
        const amf_int64 presetValues[] = {AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_HIGH_QUALITY,
                                           AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_QUALITY,
                                           AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_BALANCED,
                                           AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_SPEED};
        const int32_t preset = std::clamp(settings->GetPreset(), 0, 3);
        result = encoder->SetProperty(AMF_VIDEO_ENCODER_AV1_USAGE, amf_int64(settings->GetUsage()));
        if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_AV1_QUERY_TIMEOUT, amf_int64(10));
        if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_AV1_FRAMESIZE, frameSize);
        if (result == AMF_OK) result = encoder->SetProperty(
            AMF_VIDEO_ENCODER_AV1_ALIGNMENT_MODE,
            amf_int64(AMF_VIDEO_ENCODER_AV1_ALIGNMENT_MODE_NO_RESTRICTIONS));
        if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_AV1_COLOR_BIT_DEPTH, amf_int64(format.bitDepth));
        if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET, presetValues[preset]);
        if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_AV1_FRAMERATE, frameRate);
        if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_AV1_HEADER_INSERTION_MODE,
                                                              amf_int64(AMF_VIDEO_ENCODER_AV1_HEADER_INSERTION_MODE_KEY_FRAME_ALIGNED));
        if (settings->GetRateControl() == RateControl::CQP) {
            if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD,
                                                                  amf_int64(AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_CONSTANT_QP));
            if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_AV1_Q_INDEX_INTRA, quality);
            if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_AV1_Q_INDEX_INTER, quality);
            if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_AV1_Q_INDEX_INTER_B, quality);
        } else {
            const amf_int64 mode = settings->GetRateControl() == RateControl::CBR
                                       ? AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_CBR
                                       : AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR;
            if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD, mode);
            if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_AV1_TARGET_BITRATE, targetBitrate);
            if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_AV1_PEAK_BITRATE, peakBitrate);
            if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_AV1_VBV_BUFFER_SIZE, bufferSize);
        }
    } else if (hevc) {
        const amf_int64 presetValues[] = {AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_QUALITY,
                                           AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_BALANCED,
                                           AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_SPEED,
                                           AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_HIGH_QUALITY};
        const int32_t preset = std::clamp(settings->GetPreset(), 0, 3);
        const amf_int64 profile = format.bitDepth > 8 ? AMF_VIDEO_ENCODER_HEVC_PROFILE_MAIN_10
                                                       : AMF_VIDEO_ENCODER_HEVC_PROFILE_MAIN;
        result = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_USAGE, amf_int64(settings->GetUsage()));
        if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_QUERY_TIMEOUT, amf_int64(10));
        if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMESIZE, frameSize);
        if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_PROFILE, profile);
        if (result == AMF_OK) {
            result = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_COLOR_BIT_DEPTH, amf_int64(format.bitDepth));
        }
        if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET, presetValues[preset]);
        if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMERATE, frameRate);
        if (result == AMF_OK) {
            result = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_HEADER_INSERTION_MODE,
                                          amf_int64(AMF_VIDEO_ENCODER_HEVC_HEADER_INSERTION_MODE_SUPPRESSED));
        }
        if (settings->GetRateControl() == RateControl::CQP) {
            if (result == AMF_OK) {
                result = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD,
                                              amf_int64(AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CONSTANT_QP));
            }
            if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_QP_I, quality);
            if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_QP_P, quality);
        } else {
            const amf_int64 mode = settings->GetRateControl() == RateControl::CBR
                                       ? AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CBR
                                       : AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR;
            if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD, mode);
            if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, targetBitrate);
            if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_PEAK_BITRATE, peakBitrate);
            if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_VBV_BUFFER_SIZE, bufferSize);
        }
    } else {
        const amf_int64 presetValues[] = {AMF_VIDEO_ENCODER_QUALITY_PRESET_QUALITY,
                                           AMF_VIDEO_ENCODER_QUALITY_PRESET_BALANCED,
                                           AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED,
                                           AMF_VIDEO_ENCODER_QUALITY_PRESET_HIGH_QUALITY};
        const int32_t preset = std::clamp(settings->GetPreset(), 0, 3);
        result = encoder->SetProperty(AMF_VIDEO_ENCODER_USAGE, amf_int64(settings->GetUsage()));
        if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_QUERY_TIMEOUT, amf_int64(10));
        if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_FRAMESIZE, frameSize);
        if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_QUALITY_PRESET, presetValues[preset]);
        if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_FRAMERATE, frameRate);
        if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_HEADER_INSERTION_SPACING, amf_int64(0));
        if (settings->GetRateControl() == RateControl::CQP) {
            if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD,
                                                                  amf_int64(AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CONSTANT_QP));
            if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_QP_I, quality);
            if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_QP_P, quality);
            if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_QP_B, quality);
        } else {
            const amf_int64 mode = settings->GetRateControl() == RateControl::CBR
                                       ? AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR
                                       : AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR;
            if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD, mode);
            if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, targetBitrate);
            if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_PEAK_BITRATE, peakBitrate);
            if (result == AMF_OK) result = encoder->SetProperty(AMF_VIDEO_ENCODER_VBV_BUFFER_SIZE, bufferSize);
        }
    }
    if (result != AMF_OK) return SetError(buffer, errInvalidParam, "Could not configure AMF encoder: " + ResultText(result));

    result = encoder->Init(format.surfaceFormat, width, height);
    if (result != AMF_OK) return SetError(buffer, errNoCodec, "Could not open AMD AMF encoder: " + ResultText(result));
    return SetMagicCookie(buffer);
}

StatusCode AMFEncoder::SetMagicCookie(HostBufferRef* buffer) {
    const bool av1 = IsAv1(descriptor);
    const bool hevc = IsHevc(descriptor);
    const wchar_t* property = av1 ? AMF_VIDEO_ENCODER_AV1_EXTRA_DATA
                                  : (hevc ? AMF_VIDEO_ENCODER_HEVC_EXTRADATA : AMF_VIDEO_ENCODER_EXTRADATA);
    amf::AMFVariant value;
    const AMF_RESULT result = encoder->GetProperty(property, &value);
    if (result != AMF_OK) {
        if (hevc) {
            return SetError(buffer, errFail, "Could not read HEVC codec header: " + ResultText(result));
        }
        g_Log(logLevelWarn, "AMF encoder: no codec header (%s)", ResultText(result).c_str());
        return errNone;
    }

    amf::AMFBufferPtr extraData(static_cast<amf::AMFInterface*>(value));
    if (extraData == nullptr || extraData->GetNative() == nullptr || extraData->GetSize() == 0) {
        if (hevc) return SetError(buffer, errFail, "AMD AMF returned an empty HEVC codec header.");
        g_Log(logLevelWarn, "AMF encoder: codec header is empty");
        return errNone;
    }

    const auto* extraBytes = static_cast<const uint8_t*>(extraData->GetNative());
    const size_t extraSize = static_cast<size_t>(extraData->GetSize());
    const uint8_t* cookieBytes = extraBytes;
    size_t cookieSize = extraSize;
    uint32_t cookieType = 0;
    std::vector<uint8_t> formattedCookie;

    if (av1) {
        cookieType = MakeFourCC('a', 'v', '1', 'C');

        // AMF exposes the raw sequence-header OBU. Resolve's muxers expect av1C.
        if (extraSize >= 4 && extraBytes[0] == 0x81) {
            formattedCookie.assign(extraBytes, extraBytes + extraSize);
        } else {
            amf_int64 level = AMF_VIDEO_ENCODER_AV1_LEVEL_4_0;
            amf::AMFVariant levelValue;
            if (encoder->GetProperty(AMF_VIDEO_ENCODER_AV1_LEVEL, &levelValue) == AMF_OK) {
                level = std::clamp(levelValue.ToInt64(), amf_int64(0), amf_int64(31));
            }

            const bool highBitDepth = descriptor.formats[formatIndex].bitDepth > 8;
            formattedCookie.reserve(extraSize + 4);
            formattedCookie.push_back(0x81);  // marker = 1, version = 1
            formattedCookie.push_back(static_cast<uint8_t>(level));  // Main profile, level from AMF
            formattedCookie.push_back(static_cast<uint8_t>((highBitDepth ? 0x40 : 0x00) | 0x0c));
            formattedCookie.push_back(0x00);  // no initial presentation delay
            formattedCookie.insert(formattedCookie.end(), extraBytes, extraBytes + extraSize);
        }
        cookieBytes = formattedCookie.data();
        cookieSize = formattedCookie.size();
    } else if (hevc) {
        std::string conversionError;
        if (!BuildHevcDecoderConfigurationRecord(extraBytes, extraSize, formattedCookie, conversionError)) {
            return SetError(buffer, errFail, conversionError);
        }
        cookieType = MakeFourCC('h', 'v', 'c', 'C');
        cookieBytes = formattedCookie.data();
        cookieSize = formattedCookie.size();
    }

    const StatusCode cookieStatus =
        buffer->SetProperty(pIOPropMagicCookie, propTypeUInt8, cookieBytes, static_cast<int>(cookieSize));
    const StatusCode typeStatus = buffer->SetProperty(pIOPropMagicCookieType, propTypeUInt32, &cookieType, 1);
    if (cookieStatus != errNone || typeStatus != errNone) {
        return SetError(buffer, errFail, "Could not set AMF codec header for Resolve.");
    }
    const char* headerType = av1 ? "av1C" : (hevc ? "hvcC" : "Annex B");
    g_Log(logLevelInfo, "AMF encoder: supplied %s codec header (%zu bytes)", headerType, cookieSize);
    return errNone;
}

StatusCode AMFEncoder::DoOpen(HostBufferRef* buffer) {
    ReleaseResources();
    if (buffer == nullptr || !buffer->IsValid() || descriptor.formats.empty() || formatIndex >= descriptor.formats.size()) {
        return SetError(buffer, errInvalidParam, "Invalid AMD AMF encoder configuration.");
    }

    commonConfig.Load(buffer);
    width = static_cast<int>(commonConfig.GetWidth());
    height = static_cast<int>(commonConfig.GetHeight());
    if (width <= 0 || height <= 0) return SetError(buffer, errInvalidParam, "Frame dimensions are missing.");

    uint32_t frameRateNum = commonConfig.GetFrameRateNum();
    uint32_t frameRateDen = commonConfig.GetFrameRateDen();
    if (frameRateNum == 0 || frameRateDen == 0) {
        frameRateNum = 30;
        frameRateDen = 1;
    }

    settings = std::make_unique<AmfSettings>(descriptor);
    settings->Load(buffer);

    AMF_RESULT result = AMFInit(AMF_FULL_VERSION, &factory);
    if (result != AMF_OK || factory == nullptr) {
        return SetError(buffer, errNoCodec, "Could not load libamfrt64.so.1: " + ResultText(result));
    }
    result = factory->CreateContext(&context);
    if (result != AMF_OK || context == nullptr) {
        return SetError(buffer, errNoCodec, "Could not create AMD AMF context: " + ResultText(result));
    }
    amf::AMFContext1Ptr context1(context);
    if (context1 == nullptr) return SetError(buffer, errNoCodec, "AMF runtime does not expose Vulkan context support.");
    result = context1->InitVulkan(nullptr);
    if (result != AMF_OK && result != AMF_ALREADY_INITIALIZED) {
        return SetError(buffer, errNoCodec, "Could not initialize AMD AMF Vulkan context: " + ResultText(result));
    }

    result = factory->CreateComponent(context, descriptor.amfComponent, &encoder);
    if (result != AMF_OK || encoder == nullptr) {
        return SetError(buffer, errNoCodec, "Could not create AMD AMF component: " + ResultText(result));
    }
    if (const StatusCode status = ConfigureEncoder(buffer, frameRateNum, frameRateDen); status != errNone) return status;

    const uint8_t multipass = 0;
    const uint32_t temporalReordering = 0;
    buffer->SetProperty(pIOPropMultiPass, propTypeUInt8, &multipass, 1);
    buffer->SetProperty(pIOPropTemporalReordering, propTypeUInt32, &temporalReordering, 1);
    return errNone;
}

StatusCode AMFEncoder::CopyInputFrame(HostBufferRef* buffer, amf::AMFSurfacePtr& surface) {
    if (buffer == nullptr || !buffer->IsValid()) return errInvalidParam;

    char* input = nullptr;
    size_t inputSize = 0;
    if (!buffer->LockBuffer(&input, &inputSize) || input == nullptr) {
        return SetError(buffer, errFail, "Could not lock input frame.");
    }

    uint32_t inputWidth = 0;
    uint32_t inputHeight = 0;
    int64_t pts = 0;
    const bool metadataValid = buffer->GetUINT32(pIOPropWidth, inputWidth) && buffer->GetUINT32(pIOPropHeight, inputHeight) &&
                               buffer->GetINT64(pIOPropPTS, pts);
    if (!metadataValid || inputWidth != static_cast<uint32_t>(width) || inputHeight != static_cast<uint32_t>(height)) {
        buffer->UnlockBuffer();
        return SetError(buffer, errInvalidParam, "Input frame metadata does not match AMF session.");
    }

    const EncoderFormat& format = descriptor.formats[formatIndex];
    if ((width & 1) != 0 || (height & 1) != 0) {
        buffer->UnlockBuffer();
        return SetError(buffer, errInvalidParam, "AMF 4:2:0 encoding requires even frame dimensions.");
    }
    const uint32_t sampleBits = format.sampleBits != 0 ? format.sampleBits : static_cast<uint32_t>(format.bitDepth);
    const size_t bytesPerComponent = sampleBits > 8 ? 2 : 1;
    const size_t lumaBytes = static_cast<size_t>(width) * static_cast<size_t>(height) * bytesPerComponent;
    const size_t chromaPlaneBytes = lumaBytes / 4;
    const size_t rowBytes = static_cast<size_t>(width) * bytesPerComponent;
    const bool semiPlanarInput = format.colorModel == clrNV12;
    const bool planarInput = format.colorModel == clrYUVp;
    const bool packed422Input = format.colorModel == clrUYVY;
    if (!semiPlanarInput && !planarInput && !packed422Input) {
        buffer->UnlockBuffer();
        return SetError(buffer, errUnsupported, "AMF encoder received an unsupported color model.");
    }
    const size_t packedRowBytes = rowBytes * 2;
    const size_t rgb16RowBytes = static_cast<size_t>(width) * 3 * sizeof(uint16_t);
    const size_t rgb16FrameBytes = rgb16RowBytes * static_cast<size_t>(height);
    const bool rgb16Input = format.bitDepth == 10 && bytesPerComponent == 2 && inputSize == rgb16FrameBytes;
    size_t sourcePackedStride = packedRowBytes;
    bool reportedStride = false;
    if (packed422Input) {
        PropertyType strideType = propTypeNull;
        const void* strideData = nullptr;
        int strideCount = 0;
        if (buffer->GetProperty(pIOPropStride, &strideType, &strideData, &strideCount) == errNone &&
            strideType == propTypeUInt32 && strideData != nullptr && strideCount > 0) {
            const size_t candidateStride = static_cast<const uint32_t*>(strideData)[0];
            if (candidateStride >= packedRowBytes) {
                sourcePackedStride = candidateStride;
                reportedStride = true;
            }
        }
        if (!reportedStride && height > 0 && inputSize % static_cast<size_t>(height) == 0) {
            const size_t inferredStride = inputSize / static_cast<size_t>(height);
            if (inferredStride >= packedRowBytes) sourcePackedStride = inferredStride;
        }
        if (sourcePackedStride < packedRowBytes) {
            buffer->UnlockBuffer();
            return SetError(buffer, errInvalidParam, "UYVY input stride is smaller than one frame row.");
        }
    }
    const size_t requiredBytes = rgb16Input
                                     ? rgb16FrameBytes
                                     : (packed422Input
                                            ? sourcePackedStride * (static_cast<size_t>(height) - 1) + packedRowBytes
                                            : lumaBytes + chromaPlaneBytes * 2);
    if (inputSize < requiredBytes) {
        buffer->UnlockBuffer();
        return SetError(buffer, errInvalidParam, "Input frame buffer is smaller than selected AMF pixel format.");
    }

    bool leftAlign10Bit = false;
    if (!rgb16Input && bytesPerComponent == 2 && format.bitDepth == 10) {
        if (!inputAlignmentKnown) {
            uint16_t maximumSample = 0;
            const auto* samples = reinterpret_cast<const uint8_t*>(input);
            const size_t sampleCount = requiredBytes / sizeof(uint16_t);
            const size_t sampleStep = std::max<size_t>(1, sampleCount / 8192);
            for (size_t index = 0; index < sampleCount; index += sampleStep) {
                uint16_t sample = 0;
                std::memcpy(&sample, samples + index * sizeof(uint16_t), sizeof(sample));
                maximumSample = std::max(maximumSample, sample);
            }
            shift10BitSamples = maximumSample <= 1023;
            inputAlignmentKnown = true;
        }
        leftAlign10Bit = shift10BitSamples;
    }
    if (!inputLayoutLogged) {
        if (rgb16Input) {
            g_Log(logLevelInfo, "AMF encoder: input buffer %zu bytes, row %zu, layout RGB16 -> P010",
                  inputSize, rgb16RowBytes);
        } else if (packed422Input) {
            g_Log(logLevelInfo,
                  "AMF encoder: input buffer %zu bytes, packed row %zu, stride %zu (%s), left-align 10-bit %s",
                  inputSize, packedRowBytes, sourcePackedStride, reportedStride ? "reported" : "inferred/default",
                  leftAlign10Bit ? "yes" : "no");
        } else {
            g_Log(logLevelInfo, "AMF encoder: input buffer %zu bytes, row %zu, layout %s, left-align 10-bit %s",
                  inputSize, rowBytes, semiPlanarInput ? "NV12/P010" : "planar YUV",
                  leftAlign10Bit ? "yes" : "no");
        }
        inputLayoutLogged = true;
    }

    amf::AMFSurface* rawSurface = nullptr;
    const AMF_RESULT allocation = context->AllocSurface(amf::AMF_MEMORY_HOST, format.surfaceFormat, width, height, &rawSurface);
    if (allocation != AMF_OK || rawSurface == nullptr) {
        buffer->UnlockBuffer();
        return SetError(buffer, errAlloc, "Could not allocate AMF host surface: " + ResultText(allocation));
    }
    surface.Attach(rawSurface);
    amf::AMFPlane* yPlane = surface->GetPlane(amf::AMF_PLANE_Y);
    amf::AMFPlane* uvPlane = surface->GetPlane(amf::AMF_PLANE_UV);
    if (yPlane == nullptr || uvPlane == nullptr || yPlane->GetNative() == nullptr || uvPlane->GetNative() == nullptr) {
        buffer->UnlockBuffer();
        return SetError(buffer, errFail, "AMF allocated an invalid host surface.");
    }

    const auto* source = reinterpret_cast<const uint8_t*>(input);
    auto* destinationY = static_cast<uint8_t*>(yPlane->GetNative());
    auto* destinationUv = static_cast<uint8_t*>(uvPlane->GetNative());
    const size_t destinationYPitch = static_cast<size_t>(yPlane->GetHPitch());
    const size_t destinationUvPitch = static_cast<size_t>(uvPlane->GetHPitch());
    if (rgb16Input) {
        ConvertRgb16ToP010(source, rgb16RowBytes, destinationY, destinationYPitch, destinationUv,
                           destinationUvPitch, width, height, commonConfig.IsFullRange());
    } else if (!packed422Input) {
        for (int row = 0; row < height; ++row) {
            uint8_t* destinationRow = destinationY + static_cast<size_t>(row) * destinationYPitch;
            const uint8_t* sourceRow = source + static_cast<size_t>(row) * rowBytes;
            if (leftAlign10Bit) {
                for (size_t column = 0; column < static_cast<size_t>(width); ++column) {
                    uint16_t sample = 0;
                    std::memcpy(&sample, sourceRow + column * 2, sizeof(sample));
                    sample = static_cast<uint16_t>(std::min<uint16_t>(sample, 1023) << 6);
                    std::memcpy(destinationRow + column * 2, &sample, sizeof(sample));
                }
            } else {
                std::memcpy(destinationRow, sourceRow, rowBytes);
            }
        }
    } else {
        const size_t macropixelBytes = bytesPerComponent * 4;
        for (int row = 0; row < height; ++row) {
            uint8_t* destinationRow = destinationY + static_cast<size_t>(row) * destinationYPitch;
            const uint8_t* sourceRow = source + static_cast<size_t>(row) * sourcePackedStride;
            for (size_t column = 0; column < static_cast<size_t>(width) / 2; ++column) {
                const uint8_t* sourceMacropixel = sourceRow + column * macropixelBytes;
                if (leftAlign10Bit) {
                    uint16_t y0 = 0;
                    uint16_t y1 = 0;
                    std::memcpy(&y0, sourceMacropixel + bytesPerComponent, sizeof(y0));
                    std::memcpy(&y1, sourceMacropixel + bytesPerComponent * 3, sizeof(y1));
                    y0 = static_cast<uint16_t>(std::min<uint16_t>(y0, 1023) << 6);
                    y1 = static_cast<uint16_t>(std::min<uint16_t>(y1, 1023) << 6);
                    std::memcpy(destinationRow + (column * 2) * bytesPerComponent, &y0, sizeof(y0));
                    std::memcpy(destinationRow + (column * 2 + 1) * bytesPerComponent, &y1, sizeof(y1));
                } else {
                    std::memcpy(destinationRow + (column * 2) * bytesPerComponent,
                                sourceMacropixel + bytesPerComponent, bytesPerComponent);
                    std::memcpy(destinationRow + (column * 2 + 1) * bytesPerComponent,
                                sourceMacropixel + bytesPerComponent * 3, bytesPerComponent);
                }
            }
        }
    }
    if (!rgb16Input && semiPlanarInput) {
        const uint8_t* sourceUv = source + lumaBytes;
        for (int row = 0; row < height / 2; ++row) {
            uint8_t* destinationRow = destinationUv + static_cast<size_t>(row) * destinationUvPitch;
            const uint8_t* sourceRow = sourceUv + static_cast<size_t>(row) * rowBytes;
            if (leftAlign10Bit) {
                for (size_t column = 0; column < static_cast<size_t>(width); ++column) {
                    uint16_t sample = 0;
                    std::memcpy(&sample, sourceRow + column * 2, sizeof(sample));
                    sample = static_cast<uint16_t>(std::min<uint16_t>(sample, 1023) << 6);
                    std::memcpy(destinationRow + column * 2, &sample, sizeof(sample));
                }
            } else {
                std::memcpy(destinationRow, sourceRow, rowBytes);
            }
        }
    } else if (!rgb16Input && planarInput) {
        const size_t chromaWidth = static_cast<size_t>(width) / 2;
        const size_t chromaRowBytes = chromaWidth * bytesPerComponent;
        const uint8_t* sourceU = source + lumaBytes;
        const uint8_t* sourceV = sourceU + chromaPlaneBytes;
        for (int row = 0; row < height / 2; ++row) {
            uint8_t* destinationRow = destinationUv + static_cast<size_t>(row) * destinationUvPitch;
            const uint8_t* sourceURow = sourceU + static_cast<size_t>(row) * chromaRowBytes;
            const uint8_t* sourceVRow = sourceV + static_cast<size_t>(row) * chromaRowBytes;
            for (size_t column = 0; column < chromaWidth; ++column) {
                if (leftAlign10Bit) {
                    uint16_t u = 0;
                    uint16_t v = 0;
                    std::memcpy(&u, sourceURow + column * 2, sizeof(u));
                    std::memcpy(&v, sourceVRow + column * 2, sizeof(v));
                    u = static_cast<uint16_t>(std::min<uint16_t>(u, 1023) << 6);
                    v = static_cast<uint16_t>(std::min<uint16_t>(v, 1023) << 6);
                    std::memcpy(destinationRow + column * 4, &u, sizeof(u));
                    std::memcpy(destinationRow + column * 4 + 2, &v, sizeof(v));
                } else {
                    std::memcpy(destinationRow + (column * 2) * bytesPerComponent,
                                sourceURow + column * bytesPerComponent, bytesPerComponent);
                    std::memcpy(destinationRow + (column * 2 + 1) * bytesPerComponent,
                                sourceVRow + column * bytesPerComponent, bytesPerComponent);
                }
            }
        }
    } else if (!rgb16Input && packed422Input) {
        const size_t macropixelBytes = bytesPerComponent * 4;
        for (int row = 0; row < height / 2; ++row) {
            uint8_t* destinationRow = destinationUv + static_cast<size_t>(row) * destinationUvPitch;
            const uint8_t* sourceTop = source + static_cast<size_t>(row * 2) * sourcePackedStride;
            const uint8_t* sourceBottom = sourceTop + sourcePackedStride;
            for (size_t column = 0; column < static_cast<size_t>(width) / 2; ++column) {
                const uint8_t* top = sourceTop + column * macropixelBytes;
                const uint8_t* bottom = sourceBottom + column * macropixelBytes;
                if (bytesPerComponent == 1) {
                    destinationRow[column * 2] =
                        static_cast<uint8_t>((static_cast<unsigned>(top[0]) + bottom[0] + 1) / 2);
                    destinationRow[column * 2 + 1] =
                        static_cast<uint8_t>((static_cast<unsigned>(top[2]) + bottom[2] + 1) / 2);
                } else {
                    uint16_t topU = 0;
                    uint16_t bottomU = 0;
                    uint16_t topV = 0;
                    uint16_t bottomV = 0;
                    std::memcpy(&topU, top, sizeof(topU));
                    std::memcpy(&bottomU, bottom, sizeof(bottomU));
                    std::memcpy(&topV, top + bytesPerComponent * 2, sizeof(topV));
                    std::memcpy(&bottomV, bottom + bytesPerComponent * 2, sizeof(bottomV));
                    uint16_t outputU = static_cast<uint16_t>((static_cast<uint32_t>(topU) + bottomU + 1) / 2);
                    uint16_t outputV = static_cast<uint16_t>((static_cast<uint32_t>(topV) + bottomV + 1) / 2);
                    if (leftAlign10Bit) {
                        outputU = static_cast<uint16_t>(std::min<uint16_t>(outputU, 1023) << 6);
                        outputV = static_cast<uint16_t>(std::min<uint16_t>(outputV, 1023) << 6);
                    }
                    std::memcpy(destinationRow + column * 4, &outputU, sizeof(outputU));
                    std::memcpy(destinationRow + column * 4 + 2, &outputV, sizeof(outputV));
                }
            }
        }
    }
    buffer->UnlockBuffer();
    surface->SetPts(pts);
    return errNone;
}

StatusCode AMFEncoder::EmitPackets(HostBufferRef* errorBuffer) {
    if (encoder == nullptr || m_pCallback == nullptr) return errInvalidOperation;

    bool emitted = false;
    while (true) {
        amf::AMFDataPtr data;
        const AMF_RESULT result = encoder->QueryOutput(&data);
        if (result == AMF_REPEAT || result == AMF_NEED_MORE_INPUT) return emitted ? errNone : errMoreData;
        if (result == AMF_EOF) return errNone;
        if (result != AMF_OK) {
            return SetError(errorBuffer, errFail, "Could not read AMD AMF output: " + ResultText(result));
        }
        if (data == nullptr) return emitted ? errNone : errMoreData;

        amf::AMFBufferPtr packet(data);
        if (packet == nullptr || packet->GetNative() == nullptr || packet->GetSize() == 0) {
            return SetError(errorBuffer, errFail, "AMD AMF returned an empty encoded packet.");
        }
        const auto* packetBytes = static_cast<const uint8_t*>(packet->GetNative());
        size_t packetSize = static_cast<size_t>(packet->GetSize());
        std::vector<uint8_t> formattedPacket;
        if (IsHevc(descriptor)) {
            std::string conversionError;
            if (!ConvertHevcSampleToLengthPrefixed(packetBytes, packetSize, formattedPacket, conversionError)) {
                return SetError(errorBuffer, errFail, conversionError);
            }
            packetBytes = formattedPacket.data();
            packetSize = formattedPacket.size();
        }

        HostBufferRef output(false);
        if (!output.IsValid() || !output.Resize(packetSize)) return errAlloc;

        char* outputData = nullptr;
        size_t outputSize = 0;
        if (!output.LockBuffer(&outputData, &outputSize) || outputData == nullptr || outputSize < packetSize) return errAlloc;
        std::memcpy(outputData, packetBytes, packetSize);
        output.UnlockBuffer();

        const int64_t pts = data->GetPts();
        output.SetProperty(pIOPropPTS, propTypeInt64, &pts, 1);
        output.SetProperty(pIOPropDTS, propTypeInt64, &pts, 1);
        amf::AMFVariant pictureType;
        const wchar_t* pictureTypeProperty = IsAv1(descriptor)
                                                 ? AMF_VIDEO_ENCODER_AV1_OUTPUT_FRAME_TYPE
                                                 : (IsHevc(descriptor) ? AMF_VIDEO_ENCODER_HEVC_OUTPUT_DATA_TYPE
                                                                       : AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE);
        const bool hasPictureType = data->GetProperty(pictureTypeProperty, &pictureType) == AMF_OK;
        const uint8_t keyFrame = hasPictureType && pictureType.ToInt64() == 0 ? 1 : 0;
        output.SetProperty(pIOPropIsKeyFrame, propTypeUInt8, &keyFrame, 1);

        const StatusCode sent = m_pCallback->SendOutput(&output);
        if (sent != errNone) return sent;
        emitted = true;
    }
}

StatusCode AMFEncoder::DoProcess(HostBufferRef* buffer) {
    const std::lock_guard<std::mutex> lock(processMutex);
    if (encoder == nullptr) return errInvalidOperation;
    if (drainRequested) return EmitPackets(buffer);

    if (buffer == nullptr || !buffer->IsValid()) {
        const AMF_RESULT result = encoder->Drain();
        if (result != AMF_OK && result != AMF_EOF) {
            return SetError(nullptr, errFail, "Could not flush AMD AMF encoder: " + ResultText(result));
        }
        drainRequested = true;
        return EmitPackets(nullptr);
    }

    amf::AMFSurfacePtr surface;
    if (const StatusCode status = CopyInputFrame(buffer, surface); status != errNone) return status;
    AMF_RESULT result = encoder->SubmitInput(surface);
    const auto submitDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (result == AMF_INPUT_FULL && std::chrono::steady_clock::now() < submitDeadline) {
        const StatusCode emitted = EmitPackets(buffer);
        if (emitted != errNone && emitted != errMoreData) return emitted;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        result = encoder->SubmitInput(surface);
    }
    if (result != AMF_OK) {
        return SetError(buffer, errFail, "Could not submit frame to AMD AMF: " + ResultText(result));
    }
    const StatusCode outputStatus = EmitPackets(buffer);
    return outputStatus == errMoreData ? errNone : outputStatus;
}

void AMFEncoder::DoFlush() {
    const std::lock_guard<std::mutex> lock(processMutex);
    if (encoder == nullptr || drainRequested) return;
    const AMF_RESULT result = encoder->Drain();
    if (result != AMF_OK && result != AMF_EOF) {
        g_Log(logLevelError, "AMF encoder: flush failed: %s", ResultText(result).c_str());
        return;
    }
    drainRequested = true;
    EmitPackets(nullptr);
}

}
