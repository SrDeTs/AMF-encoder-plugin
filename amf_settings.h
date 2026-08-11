#pragma once

#include <cstdint>
#include <string>

#include "encoder_info.h"
#include "wrapper/plugin_api.h"

namespace IOPlugin {

class AmfSettings final {
   public:
    explicit AmfSettings(const EncoderDescriptor& descriptor);

    void Load(IPropertyProvider* values);
    StatusCode AppendTo(HostListRef* settingsList) const;

    RateControl GetRateControl() const;
    int32_t GetQualityFactor() const;
    int32_t GetBitRate() const;
    int32_t GetMaxBitRate() const;
    int32_t GetBufferSize() const;
    int32_t GetPreset() const;
    int32_t GetUsage() const;

   private:
    void Reset();
    void Normalize();
    StatusCode AppendEntry(HostListRef* settingsList, HostUIConfigEntryRef& entry, const char* context) const;

    const EncoderDescriptor& descriptor;
    RateControl rateControl{RateControl::CQP};
    int32_t qualityFactor{20};
    int32_t bitRateKbps{6000};
    int32_t maxBitRateKbps{6000};
    int32_t bufferSizeKbps{12000};
    int32_t preset{0};
    int32_t usage{0};

    std::string idPrefix;
    std::string rateControlId;
    std::string qualityFactorId;
    std::string bitRateId;
    std::string maxBitRateId;
    std::string bufferSizeId;
    std::string presetId;
    std::string usageId;
};

}
