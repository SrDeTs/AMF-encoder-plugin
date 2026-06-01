#pragma once

#include "encoder_info.h"
#include "wrapper/plugin_api.h"

namespace IOPlugin {

enum QualityMode : int32_t {
    CQP = 1 << 0,
    CRF = 1 << 1,
    VBR = 1 << 2,
    CBR = 1 << 3,
};

class UISettingsController final {
   public:
    explicit UISettingsController(const EncoderInfo& encoderInfo);
    UISettingsController(const HostCodecConfigCommon& commonProps, const EncoderInfo& encoderInfo);
    void Load(IPropertyProvider* values);
    StatusCode Render(HostListRef* settingsList) const;

   private:
    void InitDefaults();
    void SetFirstSupportedQualityMode();
    StatusCode RenderQuality(HostListRef* settingsList) const;

   public:
    QualityMode GetQualityMode() const;
    int32_t GetQP() const;
    int32_t GetBitRate() const;
    int32_t GetPreset() const;
    int32_t GetMaxBitRate() const;
    int32_t GetBufferSize() const;
    int32_t GetAmfAsyncDepth() const;
    int32_t GetAmfUsage() const;
    const std::string& GetCustomParams() const;

   private:
    HostCodecConfigCommon commonProps;
    EncoderInfo encoderInfo;

    QualityMode qualityMode{};
    int32_t qp{};
    int32_t bitRate{};
    int32_t preset{};
    int32_t maxBitRate{};
    int32_t bufferSize{};
    int32_t amfAsyncDepth{};
    int32_t amfUsage{};
    std::string customParams;

    std::string qualityModeId;
    std::string qpId;
    std::string bitrateId;
    std::string presetId;
    std::string maxBitrateId;
    std::string bufferSizeId;
    std::string amfAsyncDepthId;
    std::string amfUsageId;
    std::string customParamsId;
};

}
