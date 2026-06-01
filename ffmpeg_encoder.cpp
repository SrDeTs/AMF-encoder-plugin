#include "ffmpeg_encoder.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

extern "C" {
#include <libavcodec/bsf.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#undef av_err2str
av_always_inline std::string av_err2string(int errnum) {
    char str[AV_ERROR_MAX_STRING_SIZE];
    return av_make_error_string(str, AV_ERROR_MAX_STRING_SIZE, errnum);
}
#define av_err2str(err) av_err2string(err).c_str()

namespace {

const char* GetBackendName(const IOPlugin::HardwareAcceleration hwAcceleration) {
    switch (hwAcceleration) {
        case IOPlugin::AMF:
            return "AMF";
        case IOPlugin::None:
        default:
            return "CPU";
    }
}

AVHWDeviceType GetHwDeviceType(const IOPlugin::HardwareAcceleration hwAcceleration) {
    switch (hwAcceleration) {
        case IOPlugin::AMF:
            return AV_HWDEVICE_TYPE_NONE;
        case IOPlugin::None:
        default:
            return AV_HWDEVICE_TYPE_NONE;
    }
}

AVPixelFormat GetHwPixelFormat(const IOPlugin::HardwareAcceleration hwAcceleration) {
    switch (hwAcceleration) {
        case IOPlugin::AMF:
            return AV_PIX_FMT_NONE;
        case IOPlugin::None:
        default:
            return AV_PIX_FMT_NONE;
    }
}

std::vector<std::string> GetContainerList(const IOPlugin::EncoderInfo& /*encoderInfo*/) {
    return {"mov", "mp4", "mkv"};
}

bool UsesHwFrames(const IOPlugin::HardwareAcceleration hwAcceleration) { return false; }

bool IsContainer(const std::string& container, const char* expected) {
    if (container.size() != std::strlen(expected)) {
        return false;
    }

    for (size_t i = 0; i < container.size(); ++i) {
        if (static_cast<char>(std::tolower(static_cast<unsigned char>(container[i]))) != expected[i]) {
            return false;
        }
    }

    return true;
}

const char* GetAmfUsageName(const uint32_t fourCC, const int usage) {
    if (fourCC == 'av01') {
        switch (usage) {
            case 0:
                return "transcoding";
            case 1:
                return "lowlatency";
            case 2:
                return "ultralowlatency";
            case 3:
                return "webcam";
            case 4:
                return "high_quality";
            case 5:
                return "lowlatency_high_quality";
            default:
                return "transcoding";
        }
    }

    switch (usage) {
        case 0:
            return "transcoding";
        case 1:
            return "ultralowlatency";
        case 2:
            return "lowlatency";
        case 3:
            return "webcam";
        case 4:
            return "high_quality";
        case 5:
            return "lowlatency_high_quality";
        default:
            return "transcoding";
    }
}

const char* GetAmfPresetName(const uint32_t fourCC, const int preset) {
    if (fourCC == 'av01') {
        switch (preset) {
            case 0:
                return "high_quality";
            case 1:
                return "quality";
            case 2:
                return "balanced";
            case 3:
                return "speed";
            default:
                return "balanced";
        }
    }

    switch (preset) {
        case 0:
            return "quality";
        case 1:
            return "balanced";
        case 2:
            return "speed";
        default:
            return "balanced";
    }
}

bool HasAmfVideoEncodeOptions(const IOPlugin::EncoderInfo& encoderInfo) {
    return encoderInfo.fourCC == 'avc1' || encoderInfo.fourCC == 'hvc1' || encoderInfo.fourCC == 'av01';
}

int SetCustomError(HostBufferRef* buffer, const std::string& message, const StatusCode code) {
    if (buffer != nullptr && buffer->IsValid()) {
        buffer->SetProperty(pIOCustomErrorString, propTypeString, message.c_str(), static_cast<int>(message.size()));
    }
    return code;
}

}  // namespace

namespace IOPlugin {

StatusCode FFmpegEncoder::DoInit(HostPropertyCollectionRef* p_pProps) { return errNone; }

StatusCode FFmpegEncoder::RegisterCodecs(HostListRef* list, const EncoderInfo& encoderInfo) {
    for (int i = 0; i < static_cast<int>(encoderInfo.formats.size()); ++i) {
        const EncoderFormat& format = encoderInfo.formats[i];

        HostPropertyCollectionRef codecInfo;
        if (!codecInfo.IsValid()) {
            return errAlloc;
        }

        if (!IsEncoderSupported(encoderInfo, i)) {
            g_Log(logLevelWarn, "FFmpeg Plugin :: Encoder '%s' is not supported with format '%s'", encoderInfo.encoder,
                  av_get_pix_fmt_name(format.pixelFormat));
            continue;
        }

        uint8_t uuid[16];
        memcpy(uuid, encoderInfo.UUID, sizeof(uuid));
        uuid[15] += i;
        codecInfo.SetProperty(pIOPropUUID, propTypeUInt8, uuid, 16);

        const char* codecGroup = encoderInfo.codecGroup;
        codecInfo.SetProperty(pIOPropGroup, propTypeString, codecGroup, static_cast<int>(strlen(codecGroup)));

        const char* codecName = format.codecName;
        codecInfo.SetProperty(pIOPropName, propTypeString, codecName, static_cast<int>(strlen(codecName)));

        codecInfo.SetProperty(pIOPropFourCC, propTypeUInt32, &encoderInfo.fourCC, 1);

        constexpr uint32_t vMediaVideo = mediaVideo;
        codecInfo.SetProperty(pIOPropMediaType, propTypeUInt32, &vMediaVideo, 1);

        constexpr uint32_t vDirection = dirEncode;
        codecInfo.SetProperty(pIOPropCodecDirection, propTypeUInt32, &vDirection, 1);

        codecInfo.SetProperty(pIOPropColorModel, propTypeUInt32, &format.colorModel, 1);
        codecInfo.SetProperty(pIOPropHSubsampling, propTypeUInt8, &format.hSubsampling, 1);
        codecInfo.SetProperty(pIOPropVSubsampling, propTypeUInt8, &format.vSubsampling, 1);

        constexpr uint8_t dataRange[] = {0, 1};
        codecInfo.SetProperty(pIOPropDataRange, propTypeUInt8, &dataRange, sizeof(dataRange));

        codecInfo.SetProperty(pIOPropBitDepth, propTypeUInt32, &format.bitDepth, 1);
        codecInfo.SetProperty(pIOPropBitsPerSample, propTypeUInt32, &format.bitDepth, 1);

        constexpr uint32_t temp = 0;
        codecInfo.SetProperty(pIOPropTemporalReordering, propTypeUInt32, &temp, 1);

        constexpr uint8_t fieldSupport = fieldProgressive | fieldTop | fieldBottom;
        codecInfo.SetProperty(pIOPropFieldOrder, propTypeUInt8, &fieldSupport, 1);

        constexpr uint8_t threadSafe = 1;
        codecInfo.SetProperty(pIOPropThreadSafe, propTypeUInt8, &threadSafe, 1);

        const bool hwAcc = encoderInfo.hwAcceleration != None;
        codecInfo.SetProperty(pIOPropHWAcc, propTypeUInt8, &hwAcc, 1);

        const std::vector<std::string> containerVec = GetContainerList(encoderInfo);
        std::string valStrings;
        for (size_t j = 0; j < containerVec.size(); ++j) {
            valStrings.append(containerVec[j]);
            if (j < containerVec.size() - 1) {
                valStrings.append(1, '\0');
            }
        }

        codecInfo.SetProperty(pIOPropContainerList, propTypeString, valStrings.c_str(),
                              static_cast<int>(valStrings.size()));

        if (!list->Append(&codecInfo)) {
            return errFail;
        }
    }

    return errNone;
}

StatusCode FFmpegEncoder::GetEncoderSettings(HostPropertyCollectionRef* values, HostListRef* settingsList,
                                             const EncoderInfo& encoderInfo) {
    HostCodecConfigCommon commonProps;
    commonProps.Load(values);

    UISettingsController settings(commonProps, encoderInfo);
    settings.Load(values);

    return settings.Render(settingsList);
}

StatusCode FFmpegEncoder::DoOpen(HostBufferRef* p_pBuff) {
    commonProps.Load(p_pBuff);

    settings = std::make_unique<UISettingsController>(commonProps, encoderInfo);
    settings->Load(p_pBuff);

    int16_t colorMatrix{};
    int16_t colorPrimaries{};
    int16_t transferFunction{};
    uint8_t dataRange{};

    if (!p_pBuff->GetINT16(pIOColorMatrix, colorMatrix)) return errNoParam;
    if (!p_pBuff->GetINT16(pIOPropColorPrimaries, colorPrimaries)) return errNoParam;
    if (!p_pBuff->GetINT16(pIOTransferCharacteristics, transferFunction)) return errNoParam;
    if (!p_pBuff->GetUINT8(pIOPropDataRange, dataRange)) return errNoParam;

    constexpr uint8_t isMultiPass = 0;
    p_pBuff->SetProperty(pIOPropMultiPass, propTypeUInt8, &isMultiPass, 1);

    const EncoderFormat& format = encoderInfo.formats[formatIndex];

    width = static_cast<int>(commonProps.GetWidth());
    height = static_cast<int>(commonProps.GetHeight());
    frameRateNum = commonProps.GetFrameRateNum();
    frameRateDen = commonProps.GetFrameRateDen();
    pixelFormat = format.pixelFormat;
    srcPixelFormat = format.srcPixelFormat;
    useHwFrames = UsesHwFrames(encoderInfo.hwAcceleration);
    hwDeviceType = GetHwDeviceType(encoderInfo.hwAcceleration);
    hwPixelFormat = GetHwPixelFormat(encoderInfo.hwAcceleration);

    const AVCodec* codec = avcodec_find_encoder_by_name(encoderInfo.encoder);
    if (!codec) {
        const std::string message = "AMF encoder '" + std::string(encoderInfo.encoder) + "' not found in this FFmpeg build.";
        g_Log(logLevelError, "FFmpeg Plugin :: %s", message.c_str());
        return static_cast<StatusCode>(SetCustomError(p_pBuff, message, errNoCodec));
    }

    ctx = avcodec_alloc_context3(codec);
    if (!ctx) {
        const std::string message = "Failed to allocate codec context.";
        g_Log(logLevelError, "FFmpeg Plugin :: %s", message.c_str());
        return static_cast<StatusCode>(SetCustomError(p_pBuff, message, errFail));
    }

    ctx->pix_fmt = useHwFrames ? hwPixelFormat : pixelFormat;
    ctx->width = width;
    ctx->height = height;
    ctx->time_base = {static_cast<int>(frameRateDen), static_cast<int>(frameRateNum)};
    ctx->framerate = {static_cast<int>(frameRateNum), static_cast<int>(frameRateDen)};
    ctx->thread_count = 0;
    ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    ctx->colorspace = static_cast<AVColorSpace>(colorMatrix);
    ctx->color_primaries = static_cast<AVColorPrimaries>(colorPrimaries);
    ctx->color_trc = static_cast<AVColorTransferCharacteristic>(transferFunction);
    ctx->color_range = dataRange == 1 ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;

    if (const StatusCode err = ApplyOptions(ctx, *settings, p_pBuff); err != errNone) return err;

    if (useHwFrames) {
        const int createDeviceResult = av_hwdevice_ctx_create(&hwDeviceCtx, hwDeviceType, nullptr, nullptr, 0);
        if (createDeviceResult < 0) {
            const std::string message = "Failed to create " + std::string(GetBackendName(encoderInfo.hwAcceleration)) +
                                        " hardware device: " + av_err2string(createDeviceResult);
            g_Log(logLevelError, "FFmpeg Plugin :: %s", message.c_str());
            return static_cast<StatusCode>(SetCustomError(p_pBuff, message, errUnsupported));
        }

        hwFramesRef = av_hwframe_ctx_alloc(hwDeviceCtx);
        if (!hwFramesRef) {
            const std::string message =
                "Failed to create " + std::string(GetBackendName(encoderInfo.hwAcceleration)) +
                " hardware frames context.";
            g_Log(logLevelError, "FFmpeg Plugin :: %s", message.c_str());
            return static_cast<StatusCode>(SetCustomError(p_pBuff, message, errUnsupported));
        }

        auto* framesCtx = reinterpret_cast<AVHWFramesContext*>(hwFramesRef->data);
        framesCtx->format = hwPixelFormat;
        framesCtx->sw_format = pixelFormat;
        framesCtx->width = width;
        framesCtx->height = height;
        framesCtx->initial_pool_size = 20;

        const int initFramesResult = av_hwframe_ctx_init(hwFramesRef);
        if (initFramesResult < 0) {
            const std::string message =
                "Failed to initialize " + std::string(GetBackendName(encoderInfo.hwAcceleration)) +
                " hardware frames context: " + av_err2string(initFramesResult);
            g_Log(logLevelError, "FFmpeg Plugin :: %s", message.c_str());
            return static_cast<StatusCode>(SetCustomError(p_pBuff, message, errUnsupported));
        }

        ctx->hw_frames_ctx = av_buffer_ref(hwFramesRef);
        if (!ctx->hw_frames_ctx) {
            const std::string message =
                "Failed to attach " + std::string(GetBackendName(encoderInfo.hwAcceleration)) +
                " hardware frames context to codec.";
            g_Log(logLevelError, "FFmpeg Plugin :: %s", message.c_str());
            return static_cast<StatusCode>(SetCustomError(p_pBuff, message, errUnsupported));
        }
    }

    thread_local std::string logs;
    logs.clear();

    auto logCallback = [](void* ptr, int level, const char* fmt, va_list vl) {
        if (level <= AV_LOG_WARNING) {
            if (!strcmp(fmt, "Invalid value for %s: %s.\n") || !strcmp(fmt, "Error parsing option '%s = %s'.\n") ||
                !strcmp(fmt, "Error parsing option %s: %s.\n") || !strcmp(fmt, "Unknown option: %s.\n") ||
                level <= AV_LOG_ERROR) {
                va_list vl_copy;
                va_copy(vl_copy, vl);

                char line[1024];
                vsnprintf(line, sizeof(line), fmt, vl_copy);
                logs += line;

                va_end(vl_copy);
            }
        }
        av_log_default_callback(ptr, level, fmt, vl);
    };

    av_log_set_callback(logCallback);

    const int ret = avcodec_open2(ctx, codec, nullptr);

    av_log_set_callback(av_log_default_callback);

    if (ret < 0) {
        std::string message =
            "Failed to open AMF encoder '" + std::string(encoderInfo.encoder) + "': " + av_err2string(ret);
        g_Log(logLevelError, "FFmpeg Plugin :: %s", message.c_str());
        if (!logs.empty()) {
            message.append("\n");
            message.append(logs);
        }
        return static_cast<StatusCode>(SetCustomError(p_pBuff, message, errNoCodec));
    }

    if (!logs.empty()) {
        g_Log(logLevelError, "FFmpeg Plugin :: Invalid custom encoder params");
        p_pBuff->SetProperty(pIOCustomErrorString, propTypeString, logs.c_str(), static_cast<int>(logs.size()));
        return errInvalidParam;
    }

    pkt = av_packet_alloc();
    if (!pkt) {
        const std::string message = "Failed to allocate AVPacket.";
        g_Log(logLevelError, "FFmpeg Plugin :: %s", message.c_str());
        return static_cast<StatusCode>(SetCustomError(p_pBuff, message, errAlloc));
    }

    swFrame = av_frame_alloc();
    if (!swFrame) {
        const std::string message = "Failed to allocate AVFrame.";
        g_Log(logLevelError, "FFmpeg Plugin :: %s", message.c_str());
        return static_cast<StatusCode>(SetCustomError(p_pBuff, message, errAlloc));
    }
    swFrame->format = pixelFormat;
    swFrame->width = width;
    swFrame->height = height;

    if (av_image_fill_linesizes(swFrame->linesize, pixelFormat, width) < 0) {
        const std::string message = "Failed to initialize software frame linesizes.";
        g_Log(logLevelError, "FFmpeg Plugin :: %s", message.c_str());
        return static_cast<StatusCode>(SetCustomError(p_pBuff, message, errFail));
    }

    if (srcPixelFormat != AV_PIX_FMT_NONE) {
        swsCtx =
            sws_getContext(width, height, srcPixelFormat, width, height, pixelFormat, 0, nullptr, nullptr, nullptr);
        if (!swsCtx) {
            const std::string message = "Failed to create software scaler.";
            g_Log(logLevelError, "FFmpeg Plugin :: %s", message.c_str());
            return static_cast<StatusCode>(SetCustomError(p_pBuff, message, errFail));
        }
    }

    p_pBuff->SetProperty(pIOPropMagicCookie, propTypeUInt8, ctx->extradata, ctx->extradata_size);
    uint32_t magicCookieType = 0;
    if (encoderInfo.fourCC == 'avc1') {
        magicCookieType = 'avcC';
    } else if (encoderInfo.fourCC == 'hvc1') {
        magicCookieType = 'hvcC';
    } else if (encoderInfo.fourCC == 'av01') {
        magicCookieType = 'av1C';
    }
    p_pBuff->SetProperty(pIOPropMagicCookieType, propTypeUInt32, &magicCookieType, 1);

    const uint32_t temporal = ctx->has_b_frames;
    p_pBuff->SetProperty(pIOPropTemporalReordering, propTypeUInt32, &temporal, 1);

    return errNone;
}

StatusCode FFmpegEncoder::ApplyOptions(AVCodecContext* ctx, UISettingsController& settings, HostBufferRef* p_pBuff) {
    auto failAmfOption = [&](const std::string& optionName, const int err) -> StatusCode {
        const std::string message = "Failed to set AMF option '" + optionName + "': " + av_err2string(err);
        g_Log(logLevelError, "FFmpeg Plugin :: %s", message.c_str());
        SetCustomError(p_pBuff, message, errInvalidParam);
        return errInvalidParam;
    };

    const bool isAmf = encoderInfo.hwAcceleration == AMF;
    const bool hasAmfVideoOptions = HasAmfVideoEncodeOptions(encoderInfo);

    if (encoderInfo.qualityModes != 0) {
        switch (settings.GetQualityMode()) {
            case CQP:
                if (const int err = av_opt_set(ctx->priv_data, "rc", "cqp", 0); err < 0) {
                    return failAmfOption("rc", err);
                }
                if (const int err = av_opt_set_int(ctx->priv_data, "qp_i", settings.GetQP(), 0); err < 0) {
                    return failAmfOption("qp_i", err);
                }
                if (const int err = av_opt_set_int(ctx->priv_data, "qp_p", settings.GetQP(), 0); err < 0) {
                    return failAmfOption("qp_p", err);
                }
                if (encoderInfo.fourCC != 'hvc1') {
                    if (const int err = av_opt_set_int(ctx->priv_data, "qp_b", settings.GetQP(), 0); err < 0) {
                        return failAmfOption("qp_b", err);
                    }
                }
                break;
            case CRF:
                av_opt_set_int(ctx->priv_data, "crf", settings.GetQP(), 0);
                break;
            case VBR:
                if (const int err = av_opt_set(ctx->priv_data, "rc", "vbr_peak", 0); err < 0) {
                    return failAmfOption("rc", err);
                }
                ctx->bit_rate = settings.GetBitRate();
                ctx->rc_max_rate = settings.GetMaxBitRate();
                ctx->rc_buffer_size = settings.GetBufferSize();
                break;
            case CBR:
                if (const int err = av_opt_set(ctx->priv_data, "rc", "cbr", 0); err < 0) {
                    return failAmfOption("rc", err);
                }
                ctx->bit_rate = settings.GetBitRate();
                ctx->rc_max_rate = settings.GetBitRate();
                ctx->rc_buffer_size = settings.GetBufferSize();
                break;
        }
    }

    if (isAmf) {
        if (const int err = av_opt_set_int(ctx->priv_data, "async_depth", settings.GetAmfAsyncDepth(), 0); err < 0) {
            return failAmfOption("async_depth", err);
        }

        if (hasAmfVideoOptions) {
            const int bitDepth = encoderInfo.formats[formatIndex].bitDepth;
            if (bitDepth > 8 && encoderInfo.fourCC != 'avc1') {
                if (const int err = av_opt_set_int(ctx->priv_data, "bitdepth", bitDepth, 0); err < 0) {
                    return failAmfOption("bitdepth", err);
                }
            }

            if (const int err = av_opt_set(ctx->priv_data, "preset",
                                            GetAmfPresetName(encoderInfo.fourCC, settings.GetPreset()), 0);
                err < 0) {
                return failAmfOption("preset", err);
            }
            if (const int err =
                    av_opt_set(ctx->priv_data, "usage", GetAmfUsageName(encoderInfo.fourCC, settings.GetAmfUsage()), 0);
                err < 0) {
                return failAmfOption("usage", err);
            }
        }
    }

    if (encoderInfo.customParamsKey != nullptr && !settings.GetCustomParams().empty()) {
        if (av_opt_set(ctx->priv_data, encoderInfo.customParamsKey, settings.GetCustomParams().c_str(), 0) < 0) {
            const std::string msg =
                "Invalid format for encoder params (" + std::string(encoderInfo.customParamsKey) + ").";
            g_Log(logLevelError, "FFmpeg Plugin :: %s", msg.c_str());
            p_pBuff->SetProperty(pIOCustomErrorString, propTypeString, msg.c_str(), static_cast<int>(msg.size()));
            return errInvalidParam;
        }
    }

    return errNone;
}

StatusCode FFmpegEncoder::DoProcess(HostBufferRef* p_pBuff) {
    int ret = 0;

    if (p_pBuff == nullptr || !p_pBuff->IsValid()) {
        ret = avcodec_send_frame(ctx, nullptr);
    } else {
        char* pBuf = nullptr;
        size_t bufSize = 0;

        if (!p_pBuff->LockBuffer(&pBuf, &bufSize)) {
            g_Log(logLevelError, "FFmpeg Plugin :: Failed to lock the buffer");
            return errFail;
        }

        const auto unlockBuffer = [&]() { p_pBuff->UnlockBuffer(); };

        if (pBuf == nullptr || bufSize == 0) {
            g_Log(logLevelError, "FFmpeg Plugin :: No data to encode");
            unlockBuffer();
            return errUnsupported;
        }

        uint32_t frameWidth{};
        uint32_t frameHeight{};
        if (!p_pBuff->GetUINT32(pIOPropWidth, frameWidth) || !p_pBuff->GetUINT32(pIOPropHeight, frameHeight)) {
            g_Log(logLevelError, "FFmpeg Plugin :: Width/height not set when encoding the frame");
            unlockBuffer();
            return errNoParam;
        }

        if (srcPixelFormat != AV_PIX_FMT_NONE) {
            AVFrame* src = av_frame_alloc();
            if (src == nullptr) {
                unlockBuffer();
                return errAlloc;
            }

            src->format = srcPixelFormat;
            src->width = static_cast<int>(frameWidth);
            src->height = static_cast<int>(frameHeight);

            if (av_image_fill_linesizes(src->linesize, srcPixelFormat, src->width) < 0) {
                g_Log(logLevelError, "FFmpeg Plugin :: Failed to fill linesizes");
                av_frame_free(&src);
                unlockBuffer();
                return errFail;
            }

            if (av_image_fill_pointers(src->data, srcPixelFormat, src->height, reinterpret_cast<uint8_t*>(pBuf),
                                       src->linesize) < 0) {
                g_Log(logLevelError, "FFmpeg Plugin :: Failed to populate the frame");
                av_frame_free(&src);
                unlockBuffer();
                return errFail;
            }

            av_frame_unref(swFrame);
            swFrame->format = pixelFormat;
            swFrame->width = src->width;
            swFrame->height = src->height;

            if (av_frame_get_buffer(swFrame, 32) < 0) {
                g_Log(logLevelError, "FFmpeg Plugin :: Failed to access the frame buffer");
                av_frame_free(&src);
                unlockBuffer();
                return errFail;
            }

            sws_scale(swsCtx, src->data, src->linesize, 0, src->height, swFrame->data, swFrame->linesize);
            av_frame_free(&src);
        } else {
            av_frame_unref(swFrame);
            swFrame->format = pixelFormat;
            swFrame->width = static_cast<int>(frameWidth);
            swFrame->height = static_cast<int>(frameHeight);

            if (useHwFrames) {
                int srcLinesize[AV_NUM_DATA_POINTERS]{};
                uint8_t* srcData[AV_NUM_DATA_POINTERS]{};

                if (av_image_fill_linesizes(srcLinesize, pixelFormat, swFrame->width) < 0) {
                    g_Log(logLevelError, "FFmpeg Plugin :: Failed to fill source linesizes");
                    unlockBuffer();
                    return errFail;
                }

                if (av_image_fill_pointers(srcData, pixelFormat, static_cast<int>(frameHeight),
                                           reinterpret_cast<uint8_t*>(pBuf), srcLinesize) < 0) {
                    g_Log(logLevelError, "FFmpeg Plugin :: Failed to populate source frame");
                    unlockBuffer();
                    return errFail;
                }

                if (av_frame_get_buffer(swFrame, 32) < 0) {
                    g_Log(logLevelError, "FFmpeg Plugin :: Failed to allocate CPU staging frame");
                    unlockBuffer();
                    return errFail;
                }

                av_image_copy(swFrame->data, swFrame->linesize, const_cast<const uint8_t**>(srcData), srcLinesize,
                              pixelFormat, swFrame->width, swFrame->height);
            } else {
                if (av_image_fill_linesizes(swFrame->linesize, pixelFormat, swFrame->width) < 0) {
                    g_Log(logLevelError, "FFmpeg Plugin :: Failed to fill linesizes");
                    unlockBuffer();
                    return errFail;
                }

                if (av_image_fill_pointers(swFrame->data, pixelFormat, static_cast<int>(frameHeight),
                                           reinterpret_cast<uint8_t*>(pBuf), swFrame->linesize) < 0) {
                    g_Log(logLevelError, "FFmpeg Plugin :: Failed to populate the frame");
                    unlockBuffer();
                    return errFail;
                }
            }
        }

        int64_t pts{};
        if (!p_pBuff->GetINT64(pIOPropPTS, pts)) {
            g_Log(logLevelError, "FFmpeg Plugin :: PTS not set when encoding the frame");
            unlockBuffer();
            return errNoParam;
        }

        if (useHwFrames) {
            AVFrame* hwFrame = av_frame_alloc();
            if (hwFrame == nullptr) {
                unlockBuffer();
                return errAlloc;
            }

            if (const int err = av_hwframe_get_buffer(hwFramesRef, hwFrame, 0); err < 0) {
                g_Log(logLevelError, "FFmpeg Plugin :: Failed to allocate %s frame: %s",
                      GetBackendName(encoderInfo.hwAcceleration), av_err2str(err));
                av_frame_free(&hwFrame);
                unlockBuffer();
                return errAlloc;
            }

            if (const int err = av_hwframe_transfer_data(hwFrame, swFrame, 0); err < 0) {
                g_Log(logLevelError, "FFmpeg Plugin :: Failed to transfer CPU frame to %s frame: %s",
                      GetBackendName(encoderInfo.hwAcceleration), av_err2str(err));
                av_frame_free(&hwFrame);
                unlockBuffer();
                return errUnsupported;
            }

            unlockBuffer();

            hwFrame->pts = pts;
            ret = avcodec_send_frame(ctx, hwFrame);
            av_frame_free(&hwFrame);
        } else {
            swFrame->pts = pts;
            ret = avcodec_send_frame(ctx, swFrame);
            unlockBuffer();
        }
    }

    if (ret == AVERROR_EOF) {
        av_packet_unref(pkt);
        return errNone;
    }

    if (ret < 0) {
        g_Log(logLevelError, "FFmpeg Plugin :: Failed to encode frame. %s", av_err2str(ret));
        return errFail;
    }

    while (true) {
        ret = avcodec_receive_packet(ctx, pkt);

        if (ret == AVERROR(EAGAIN)) {
            return errMoreData;
        }

        if (ret == AVERROR_EOF) {
            av_packet_unref(pkt);
            return errNone;
        }

        if (ret < 0) {
            g_Log(logLevelError, "FFmpeg Plugin :: Failed to read encoded data. %s", av_err2str(ret));
            av_packet_unref(pkt);
            return errFail;
        }

        HostBufferRef outBuf(false);
        if (!outBuf.IsValid() || !outBuf.Resize(pkt->size)) {
            g_Log(logLevelError, "FFmpeg Plugin :: Failed to resize output buffer");
            av_packet_unref(pkt);
            return errAlloc;
        }

        char* outBufPtr = nullptr;
        size_t outBufSize = 0;

        if (!outBuf.LockBuffer(&outBufPtr, &outBufSize)) {
            g_Log(logLevelError, "FFmpeg Plugin :: Failed to lock the output buffer");
            av_packet_unref(pkt);
            return errAlloc;
        }

        memcpy(outBufPtr, pkt->data, pkt->size);

        outBuf.SetProperty(pIOPropPTS, propTypeInt64, &pkt->pts, 1);
        outBuf.SetProperty(pIOPropDTS, propTypeInt64, &pkt->dts, 1);

        const uint8_t isKeyFrame = pkt->flags & AV_PKT_FLAG_KEY ? 1 : 0;
        outBuf.SetProperty(pIOPropIsKeyFrame, propTypeUInt8, &isKeyFrame, 1);

        av_packet_unref(pkt);

        m_pCallback->SendOutput(&outBuf);
    }
}

void FFmpegEncoder::DoFlush() { DoProcess(nullptr); }

bool FFmpegEncoder::IsEncoderSupported(const EncoderInfo& encoderInfo, const int formatIndex) {
    bool isEncoderSupported = false;

    const int logLevel = av_log_get_level();
    av_log_set_level(AV_LOG_ERROR);

    AVCodecContext* ctx = nullptr;
    AVBufferRef* hwFramesRef = nullptr;
    AVBufferRef* hwDeviceCtx = nullptr;

    const AVPixelFormat pixelFormat = encoderInfo.formats[formatIndex].pixelFormat;
    const bool useHwFrames = UsesHwFrames(encoderInfo.hwAcceleration);
    const AVPixelFormat hwPixelFormat = GetHwPixelFormat(encoderInfo.hwAcceleration);
    const AVHWDeviceType hwDeviceType = GetHwDeviceType(encoderInfo.hwAcceleration);

    const AVCodec* codec = avcodec_find_encoder_by_name(encoderInfo.encoder);
    if (!codec) goto end;

    if (encoderInfo.hwAcceleration == None) {
        const void* configs = nullptr;
        int numConfigs = 0;
        avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, &configs, &numConfigs);
        if (configs) {
            const auto* pixFmts = static_cast<const AVPixelFormat*>(configs);
            for (int i = 0; i < numConfigs; ++i) {
                if (pixFmts[i] == pixelFormat) {
                    isEncoderSupported = true;
                    break;
                }
            }
        } else {
            isEncoderSupported = true;
        }
        goto end;
    }

    if (encoderInfo.hwAcceleration == AMF) {
        const void* configs = nullptr;
        int numConfigs = 0;
        avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, &configs, &numConfigs);
        if (configs) {
            const auto* pixFmts = static_cast<const AVPixelFormat*>(configs);
            for (int i = 0; i < numConfigs; ++i) {
                if (pixFmts[i] == pixelFormat) {
                    isEncoderSupported = true;
                    break;
                }
            }
        } else {
            isEncoderSupported = true;
        }
        goto end;
    }

    ctx = avcodec_alloc_context3(codec);
    if (!ctx) goto end;

    ctx->pix_fmt = useHwFrames ? hwPixelFormat : pixelFormat;
    ctx->time_base = {25, 1};
    ctx->width = 1920;
    ctx->height = 1080;

    if (useHwFrames) {
        if (av_hwdevice_ctx_create(&hwDeviceCtx, hwDeviceType, nullptr, nullptr, 0) < 0) goto end;
        if (!((hwFramesRef = av_hwframe_ctx_alloc(hwDeviceCtx)))) goto end;

        auto* framesCtx = reinterpret_cast<AVHWFramesContext*>(hwFramesRef->data);
        framesCtx->format = hwPixelFormat;
        framesCtx->sw_format = pixelFormat;
        framesCtx->width = ctx->width;
        framesCtx->height = ctx->height;
        framesCtx->initial_pool_size = 20;
        if (av_hwframe_ctx_init(hwFramesRef) < 0) goto end;

        ctx->hw_frames_ctx = av_buffer_ref(hwFramesRef);
        if (!ctx->hw_frames_ctx) goto end;
    }

    if (avcodec_open2(ctx, codec, nullptr) < 0) goto end;

    isEncoderSupported = true;

end:
    if (ctx != nullptr) avcodec_free_context(&ctx);
    if (hwDeviceCtx != nullptr) av_buffer_unref(&hwDeviceCtx);
    if (hwFramesRef != nullptr) av_buffer_unref(&hwFramesRef);

    av_log_set_level(logLevel);

    return isEncoderSupported;
}

FFmpegEncoder::FFmpegEncoder() = default;

FFmpegEncoder::~FFmpegEncoder() {
    if (ctx != nullptr) avcodec_free_context(&ctx);
    if (hwFramesRef != nullptr) av_buffer_unref(&hwFramesRef);
    if (hwDeviceCtx != nullptr) av_buffer_unref(&hwDeviceCtx);
    if (swsCtx != nullptr) sws_freeContext(swsCtx);
    if (pkt != nullptr) av_packet_free(&pkt);
    if (swFrame != nullptr) av_frame_free(&swFrame);
}

}  // namespace IOPlugin
