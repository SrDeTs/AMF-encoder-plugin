#include "amf_encoder.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

extern "C" AMF_RESULT AMF_CDECL_CALL AMFInit(amf_uint64 version, amf::AMFFactory** factory);

namespace IOPlugin {

namespace {

std::string ResultText(const AMF_RESULT result) {
    return "AMF result " + std::to_string(static_cast<int>(result));
}

bool IsAv1(const EncoderDescriptor& descriptor) {
    return descriptor.fourCC == MakeFourCC('a', 'v', '0', '1');
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
}

StatusCode AMFEncoder::DoInit(HostPropertyCollectionRef* properties) {
    if (properties == nullptr || descriptor.formats.empty()) return errNone;

    const EncoderFormat& format = descriptor.formats.front();
    properties->SetProperty(pIOPropColorModel, propTypeUInt32, &format.colorModel, 1);
    properties->SetProperty(pIOPropHSubsampling, propTypeUInt8, &format.hSubsampling, 1);
    properties->SetProperty(pIOPropVSubsampling, propTypeUInt8, &format.vSubsampling, 1);
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
        const uint8_t fields = fieldProgressive | fieldTop | fieldBottom;
        const uint8_t threadSafe = 0;
        const char containerList[] = "mp4\0mov\0mkv";

        codecInfo.SetProperty(pIOPropUUID, propTypeUInt8, uuid.data(), static_cast<int>(uuid.size()));
        codecInfo.SetProperty(pIOPropGroup, propTypeString, descriptor.group, static_cast<int>(std::strlen(descriptor.group)));
        codecInfo.SetProperty(pIOPropName, propTypeString, format.name, static_cast<int>(std::strlen(format.name)));
        codecInfo.SetProperty(pIOPropFourCC, propTypeUInt32, &descriptor.fourCC, 1);
        codecInfo.SetProperty(pIOPropMediaType, propTypeUInt32, &mediaType, 1);
        codecInfo.SetProperty(pIOPropCodecDirection, propTypeUInt32, &direction, 1);
        codecInfo.SetProperty(pIOPropColorModel, propTypeUInt32, &format.colorModel, 1);
        codecInfo.SetProperty(pIOPropHSubsampling, propTypeUInt8, &format.hSubsampling, 1);
        codecInfo.SetProperty(pIOPropVSubsampling, propTypeUInt8, &format.vSubsampling, 1);
        codecInfo.SetProperty(pIOPropDataRange, propTypeUInt8, dataRange, 2);
        codecInfo.SetProperty(pIOPropBitDepth, propTypeUInt32, &format.bitDepth, 1);
        codecInfo.SetProperty(pIOPropBitsPerSample, propTypeUInt32, &format.bitDepth, 1);
        codecInfo.SetProperty(pIOPropTemporalReordering, propTypeUInt32, &temporalReordering, 1);
        codecInfo.SetProperty(pIOPropFieldOrder, propTypeUInt8, &fields, 1);
        codecInfo.SetProperty(pIOPropThreadSafe, propTypeUInt8, &threadSafe, 1);
        codecInfo.SetProperty(pIOPropHWAcc, propTypeUInt8, &hardwareAcceleration, 1);
        codecInfo.SetProperty(pIOPropContainerList, propTypeString, containerList, sizeof(containerList) - 1);

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
    const wchar_t* property = av1 ? AMF_VIDEO_ENCODER_AV1_EXTRA_DATA : AMF_VIDEO_ENCODER_EXTRADATA;
    amf::AMFVariant value;
    const AMF_RESULT result = encoder->GetProperty(property, &value);
    if (result != AMF_OK) {
        g_Log(logLevelWarn, "AMF encoder: no codec header (%s)", ResultText(result).c_str());
        return errNone;
    }

    amf::AMFBufferPtr extraData(static_cast<amf::AMFInterface*>(value));
    if (extraData == nullptr || extraData->GetNative() == nullptr || extraData->GetSize() == 0) {
        g_Log(logLevelWarn, "AMF encoder: codec header is empty");
        return errNone;
    }

    const auto* extraBytes = static_cast<const uint8_t*>(extraData->GetNative());
    const size_t extraSize = static_cast<size_t>(extraData->GetSize());
    const uint8_t* cookieBytes = extraBytes;
    size_t cookieSize = extraSize;
    uint32_t cookieType = 0;
    std::vector<uint8_t> av1Cookie;

    if (av1) {
        cookieType = MakeFourCC('a', 'v', '1', 'C');

        // AMF exposes the raw sequence-header OBU. Resolve's muxers expect av1C.
        if (extraSize >= 4 && extraBytes[0] == 0x81) {
            av1Cookie.assign(extraBytes, extraBytes + extraSize);
        } else {
            amf_int64 level = AMF_VIDEO_ENCODER_AV1_LEVEL_4_0;
            amf::AMFVariant levelValue;
            if (encoder->GetProperty(AMF_VIDEO_ENCODER_AV1_LEVEL, &levelValue) == AMF_OK) {
                level = std::clamp(levelValue.ToInt64(), amf_int64(0), amf_int64(31));
            }

            const bool highBitDepth = descriptor.formats[formatIndex].bitDepth > 8;
            av1Cookie.reserve(extraSize + 4);
            av1Cookie.push_back(0x81);  // marker = 1, version = 1
            av1Cookie.push_back(static_cast<uint8_t>(level));  // Main profile, level from AMF
            av1Cookie.push_back(static_cast<uint8_t>((highBitDepth ? 0x40 : 0x00) | 0x0c));
            av1Cookie.push_back(0x00);  // no initial presentation delay
            av1Cookie.insert(av1Cookie.end(), extraBytes, extraBytes + extraSize);
        }
        cookieBytes = av1Cookie.data();
        cookieSize = av1Cookie.size();
    }

    const StatusCode cookieStatus =
        buffer->SetProperty(pIOPropMagicCookie, propTypeUInt8, cookieBytes, static_cast<int>(cookieSize));
    const StatusCode typeStatus = buffer->SetProperty(pIOPropMagicCookieType, propTypeUInt32, &cookieType, 1);
    if (cookieStatus != errNone || typeStatus != errNone) {
        return SetError(buffer, errFail, "Could not set AMF codec header for Resolve.");
    }
    g_Log(logLevelInfo, "AMF encoder: supplied %s codec header (%zu bytes)", av1 ? "av1C" : "Annex B", cookieSize);
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
    const size_t bytesPerComponent = format.bitDepth > 8 ? 2 : 1;
    const size_t lumaBytes = static_cast<size_t>(width) * static_cast<size_t>(height) * bytesPerComponent;
    const size_t requiredBytes = lumaBytes + lumaBytes / 2;
    if (inputSize < requiredBytes) {
        buffer->UnlockBuffer();
        return SetError(buffer, errInvalidParam, "Input frame buffer is smaller than selected AMF pixel format.");
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

    const size_t rowBytes = static_cast<size_t>(width) * bytesPerComponent;
    const auto* source = reinterpret_cast<const uint8_t*>(input);
    auto* destinationY = static_cast<uint8_t*>(yPlane->GetNative());
    auto* destinationUv = static_cast<uint8_t*>(uvPlane->GetNative());
    const size_t destinationYPitch = static_cast<size_t>(yPlane->GetHPitch());
    const size_t destinationUvPitch = static_cast<size_t>(uvPlane->GetHPitch());
    for (int row = 0; row < height; ++row) {
        std::memcpy(destinationY + static_cast<size_t>(row) * destinationYPitch, source + static_cast<size_t>(row) * rowBytes,
                    rowBytes);
    }
    const uint8_t* sourceUv = source + lumaBytes;
    for (int row = 0; row < height / 2; ++row) {
        std::memcpy(destinationUv + static_cast<size_t>(row) * destinationUvPitch,
                    sourceUv + static_cast<size_t>(row) * rowBytes, rowBytes);
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
        HostBufferRef output(false);
        const size_t packetSize = packet->GetSize();
        if (!output.IsValid() || !output.Resize(packetSize)) return errAlloc;

        char* outputData = nullptr;
        size_t outputSize = 0;
        if (!output.LockBuffer(&outputData, &outputSize) || outputData == nullptr || outputSize < packetSize) return errAlloc;
        std::memcpy(outputData, packet->GetNative(), packetSize);
        output.UnlockBuffer();

        const int64_t pts = data->GetPts();
        output.SetProperty(pIOPropPTS, propTypeInt64, &pts, 1);
        output.SetProperty(pIOPropDTS, propTypeInt64, &pts, 1);
        amf::AMFVariant pictureType;
        const wchar_t* pictureTypeProperty = IsAv1(descriptor) ? AMF_VIDEO_ENCODER_AV1_OUTPUT_FRAME_TYPE
                                                                : AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE;
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
    return EmitPackets(buffer);
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
