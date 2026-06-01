#include "ffmpeg_encoder.h"

#include <algorithm>

namespace {

int GetDefaultAMFAsyncDepth(const uint32_t fourCC, const int preset) {
    if (fourCC == 'av01') {
        switch (preset) {
            case 0:
            case 1:
                return 2;
            case 2:
            case 3:
                return 4;
            default:
                return 2;
        }
    }

    switch (preset) {
        case 0:
            return 2;
        case 1:
            return 3;
        case 2:
        case 3:
            return 4;
        default:
            return 3;
    }
}

int GetMaxAMFAsyncDepth(const uint32_t /*fourCC*/) { return 42; }

}  // namespace

UISettingsController::UISettingsController(const EncoderInfo& encoderInfo) : encoderInfo(encoderInfo) {
    InitDefaults();
}

UISettingsController::UISettingsController(const HostCodecConfigCommon& commonProps, const EncoderInfo& encoderInfo)
    : commonProps(commonProps), encoderInfo(encoderInfo) {
    InitDefaults();
}

void UISettingsController::Load(IPropertyProvider* values) {
    uint8_t val8 = 0;
    values->GetUINT8("ffmpeg_reset", val8);
    if (val8 != 0) {
        *this = UISettingsController(commonProps, encoderInfo);
        return;
    }

    int32_t val32 = 0;
    values->GetINT32(qualityModeId.c_str(), val32);
    qualityMode = static_cast<QualityMode>(val32);
    SetFirstSupportedQualityMode();

    values->GetINT32(qpId.c_str(), qp);
    values->GetINT32(bitrateId.c_str(), bitRate);
    values->GetINT32(presetId.c_str(), preset);
    values->GetINT32(maxBitrateId.c_str(), maxBitRate);
    values->GetINT32(bufferSizeId.c_str(), bufferSize);
    values->GetINT32(amfAsyncDepthId.c_str(), amfAsyncDepth);
    values->GetINT32(amfUsageId.c_str(), amfUsage);

    std::string customParamsStr;
    if (values->GetString(customParamsId.c_str(), customParamsStr)) {
        customParams = customParamsStr;
    }
}

StatusCode UISettingsController::Render(HostListRef* settingsList) const {
    StatusCode err = RenderQuality(settingsList);
    if (err != errNone) {
        return err;
    }

    {
        HostUIConfigEntryRef item("ffmpeg_reset");
        item.MakeButton("Reset");
        item.SetTriggersUpdate(true);
        if (!item.IsSuccess() || !settingsList->Append(&item)) {
            g_Log(logLevelError, "FFmpeg Plugin :: Failed to populate the button entry");
            return errFail;
        }
    }

    return errNone;
}

void UISettingsController::InitDefaults() {
    qualityMode = CRF;
    SetFirstSupportedQualityMode();

    qp = encoderInfo.qp[1];
    bitRate = 6000;
    preset = encoderInfo.defaultPreset;
    maxBitRate = bitRate;
    bufferSize = bitRate * 2;
    amfAsyncDepth = GetDefaultAMFAsyncDepth(encoderInfo.fourCC, preset);
    amfUsage = 0;

    const std::string prefix = std::string("ffmpeg_") + encoderInfo.encoder + "_";
    qualityModeId = prefix + "q_mode";
    qpId = prefix + "qp";
    bitrateId = prefix + "bitrate";
    presetId = prefix + "preset";
    maxBitrateId = prefix + "max_bitrate";
    bufferSizeId = prefix + "buffer_size";
    amfAsyncDepthId = prefix + "amf_async_depth";
    amfUsageId = prefix + "amf_usage";
    customParamsId = prefix + "custom_params";
}

void UISettingsController::SetFirstSupportedQualityMode() {
    if (encoderInfo.qualityModes == 0) {
        qualityMode = static_cast<QualityMode>(0);
        return;
    }

    if (!(qualityMode & encoderInfo.qualityModes)) {
        if (encoderInfo.qualityModes & CRF)
            qualityMode = CRF;
        else if (encoderInfo.qualityModes & CQP)
            qualityMode = CQP;
        else if (encoderInfo.qualityModes & VBR)
            qualityMode = VBR;
        else if (encoderInfo.qualityModes & CBR)
            qualityMode = CBR;
    }
}

StatusCode UISettingsController::RenderQuality(HostListRef* settingsList) const {
    const bool isAmf = encoderInfo.hwAcceleration == AMF;
    const bool hasQualityModes = encoderInfo.qualityModes != 0;
    const bool hasAmfVideoOptions = encoderInfo.fourCC == 'avc1' || encoderInfo.fourCC == 'hvc1' || encoderInfo.fourCC == 'av01';

    if (!encoderInfo.presets.empty())
    {
        HostUIConfigEntryRef item(presetId);

        std::vector<std::string> textsVec;
        std::vector<int> valuesVec;

        for (const auto& [key, value] : encoderInfo.presets) {
            valuesVec.push_back(key);
            textsVec.emplace_back(value);
        }

        item.MakeComboBox("Encoder Preset", textsVec, valuesVec, preset);
        item.SetTriggersUpdate(true);
        if (!item.IsSuccess() || !settingsList->Append(&item)) {
            g_Log(logLevelError, "FFmpeg Plugin :: Failed to populate encoder preset UI entry");
            return errFail;
        }
    }

    if (hasQualityModes)
    {
        HostUIConfigEntryRef item(qualityModeId);

        std::vector<std::string> textsVec;
        std::vector<int> valuesVec;

        if (encoderInfo.qualityModes & CRF) {
            textsVec.emplace_back("Constant Rate Factor");
            valuesVec.push_back(CRF);
        }

        if (encoderInfo.qualityModes & CQP) {
            textsVec.emplace_back("Constant Quality");
            valuesVec.push_back(CQP);
        }

        if (encoderInfo.qualityModes & VBR) {
            textsVec.emplace_back("Variable Rate");
            valuesVec.push_back(VBR);
        }

        if (encoderInfo.qualityModes & CBR) {
            textsVec.emplace_back("Constant Bitrate");
            valuesVec.push_back(CBR);
        }

        item.MakeRadioBox("Quality Control", textsVec, valuesVec, qualityMode);
        item.SetTriggersUpdate(true);

        if (!item.IsSuccess() || !settingsList->Append(&item)) {
            g_Log(logLevelError, "FFmpeg Plugin :: Failed to populate quality UI entry");
            return errFail;
        }
    }

    if (hasQualityModes)
    {
        HostUIConfigEntryRef item(qpId);
        const char* pLabel = nullptr;
        if (qp < encoderInfo.qp[2] / 3) {
            pLabel = "(high)";
        } else if (qp < encoderInfo.qp[2] * 2 / 3) {
            pLabel = "(medium)";
        } else {
            pLabel = "(low)";
        }
        item.MakeSlider("Factor", pLabel, qp, encoderInfo.qp[0], encoderInfo.qp[2], encoderInfo.qp[1]);
        item.SetTriggersUpdate(true);
        item.SetHidden(qualityMode == VBR || qualityMode == CBR);
        if (!item.IsSuccess() || !settingsList->Append(&item)) {
            g_Log(logLevelError, "FFmpeg Plugin :: Failed to populate qp slider UI entry");
            return errFail;
        }
    }

    if (hasQualityModes)
    {
        HostUIConfigEntryRef item(bitrateId);
        item.MakeSlider("Bit Rate", "kb/s", bitRate, 100, 100000, 1);
        item.SetHidden(qualityMode != VBR && qualityMode != CBR);

        if (!item.IsSuccess() || !settingsList->Append(&item)) {
            g_Log(logLevelError, "FFmpeg Plugin :: Failed to populate bitrate slider UI entry");
            return errFail;
        }
    }

    if (hasQualityModes)
    {
        HostUIConfigEntryRef item(maxBitrateId);
        item.MakeSlider("Max Bit Rate", "kb/s", maxBitRate, 100, 100000, bitRate);
        item.SetHidden(qualityMode != VBR);
        if (!item.IsSuccess() || !settingsList->Append(&item)) {
            g_Log(logLevelError, "FFmpeg Plugin :: Failed to populate max bitrate UI entry");
            return errFail;
        }
    }

    if (hasQualityModes)
    {
        HostUIConfigEntryRef item(bufferSizeId);
        item.MakeSlider("Buffer Size", "kb", bufferSize, 100, 200000, bitRate * 2);
        item.SetHidden(qualityMode != VBR && qualityMode != CBR);
        if (!item.IsSuccess() || !settingsList->Append(&item)) {
            g_Log(logLevelError, "FFmpeg Plugin :: Failed to populate buffer size UI entry");
            return errFail;
        }
    }

    if (isAmf) {
        {
            HostUIConfigEntryRef item("ffmpeg_amf_separator");
            item.MakeSeparator();
            if (!item.IsSuccess() || !settingsList->Append(&item)) {
                g_Log(logLevelError, "FFmpeg Plugin :: Failed to populate AMF separator");
                return errFail;
            }
        }

        {
            HostUIConfigEntryRef item("ffmpeg_amf_label");
            item.MakeLabel("AMF");
            if (!item.IsSuccess() || !settingsList->Append(&item)) {
                g_Log(logLevelError, "FFmpeg Plugin :: Failed to populate AMF label");
                return errFail;
            }
        }

        {
            HostUIConfigEntryRef item(amfAsyncDepthId);
            item.MakeSlider("Async Depth", "", amfAsyncDepth, 1, 42, 16);
            if (!item.IsSuccess() || !settingsList->Append(&item)) {
                g_Log(logLevelError, "FFmpeg Plugin :: Failed to populate AMF async depth UI entry");
                return errFail;
            }
        }

        if (hasAmfVideoOptions)
        {
            if (encoderInfo.fourCC == 'av01') {
                HostUIConfigEntryRef item(amfUsageId);
                item.MakeComboBox("Usage",
                                  {"Transcoding", "Low Latency", "Ultra Low Latency", "Webcam", "High Quality",
                                   "Low Latency High Quality"},
                                  {0, 1, 2, 3, 4, 5}, amfUsage);
                if (!item.IsSuccess() || !settingsList->Append(&item)) {
                    g_Log(logLevelError, "FFmpeg Plugin :: Failed to populate AMF usage UI entry");
                    return errFail;
                }
            } else {
                HostUIConfigEntryRef item(amfUsageId);
                item.MakeComboBox("Usage",
                                  {"Transcoding", "Ultra Low Latency", "Low Latency", "Webcam", "High Quality",
                                   "Low Latency High Quality"},
                                  {0, 1, 2, 3, 4, 5}, amfUsage);
                if (!item.IsSuccess() || !settingsList->Append(&item)) {
                    g_Log(logLevelError, "FFmpeg Plugin :: Failed to populate AMF usage UI entry");
                    return errFail;
                }
            }
        }
    }

    if (encoderInfo.customParamsKey != nullptr) {
        {
            HostUIConfigEntryRef item("separator");
            item.MakeSeparator();
            if (!item.IsSuccess() || !settingsList->Append(&item)) {
                g_Log(logLevelError, "FFmpeg Plugin :: Failed to populate separator");
                return errFail;
            }
        }

        HostUIConfigEntryRef item(customParamsId);
        item.MakeTextBox("Encoder Params", customParams, "");
        if (!item.IsSuccess() || !settingsList->Append(&item)) {
            g_Log(logLevelError, "FFmpeg Plugin :: Failed to populate custom params UI entry");
            return errFail;
        }
    }

    return errNone;
}

QualityMode UISettingsController::GetQualityMode() const { return qualityMode; }

int32_t UISettingsController::GetQP() const { return std::max<int>(0, qp); }

int32_t UISettingsController::GetBitRate() const { return bitRate * 1000; }

int32_t UISettingsController::GetPreset() const { return preset; }

int32_t UISettingsController::GetMaxBitRate() const { return std::max<int>(0, maxBitRate) * 1000; }

int32_t UISettingsController::GetBufferSize() const { return std::max<int>(0, bufferSize) * 1000; }

int32_t UISettingsController::GetAmfAsyncDepth() const {
    return std::clamp(amfAsyncDepth, 1, GetMaxAMFAsyncDepth(encoderInfo.fourCC));
}

int32_t UISettingsController::GetAmfUsage() const { return amfUsage; }

const std::string& UISettingsController::GetCustomParams() const { return customParams; }
