#include "amf_settings.h"

#include <algorithm>
#include <string>
#include <vector>

namespace IOPlugin {

namespace {

constexpr int32_t ValueOf(const RateControl mode) {
    return static_cast<int32_t>(mode);
}

int32_t FirstRateControl(const int32_t available) {
    if ((available & ValueOf(RateControl::CQP)) != 0) return ValueOf(RateControl::CQP);
    if ((available & ValueOf(RateControl::VBR)) != 0) return ValueOf(RateControl::VBR);
    if ((available & ValueOf(RateControl::CBR)) != 0) return ValueOf(RateControl::CBR);
    return 0;
}

}  // namespace

AmfSettings::AmfSettings(const EncoderDescriptor& descriptor) : descriptor(descriptor) {
    idPrefix = std::string("amf_") + descriptor.settingsId + "_";
    rateControlId = idPrefix + "rate_control";
    qualityFactorId = idPrefix + "quality_factor";
    bitRateId = idPrefix + "bitrate";
    maxBitRateId = idPrefix + "max_bitrate";
    bufferSizeId = idPrefix + "buffer_size";
    presetId = idPrefix + "preset";
    usageId = idPrefix + "usage";
    Reset();
}

void AmfSettings::Reset() {
    rateControl = static_cast<RateControl>(FirstRateControl(descriptor.rateControls));
    qualityFactor = descriptor.qualityDefault;
    bitRateKbps = 6000;
    maxBitRateKbps = bitRateKbps;
    bufferSizeKbps = bitRateKbps * 2;
    preset = descriptor.defaultPreset;
    usage = 0;
    Normalize();
}

void AmfSettings::Normalize() {
    const int32_t selectedMode = static_cast<int32_t>(rateControl);
    if ((descriptor.rateControls & selectedMode) == 0) {
        rateControl = static_cast<RateControl>(FirstRateControl(descriptor.rateControls));
    }

    qualityFactor = std::clamp(qualityFactor, descriptor.qualityMin, descriptor.qualityMax);
    bitRateKbps = std::clamp(bitRateKbps, 100, 100000);
    maxBitRateKbps = std::clamp(maxBitRateKbps, bitRateKbps, 100000);
    bufferSizeKbps = std::clamp(bufferSizeKbps, 100, 200000);
    usage = std::clamp(usage, 0, 5);

    bool knownPreset = false;
    for (const PresetOption& option : descriptor.presets) {
        if (option.value == preset) {
            knownPreset = true;
            break;
        }
    }
    if (!knownPreset && !descriptor.presets.empty()) {
        preset = descriptor.presets.front().value;
        for (const PresetOption& option : descriptor.presets) {
            if (option.value == descriptor.defaultPreset) {
                preset = option.value;
                break;
            }
        }
    }
}

void AmfSettings::Load(IPropertyProvider* values) {
    if (values == nullptr) return;

    uint8_t resetRequested = 0;
    if (values->GetUINT8("amf_reset", resetRequested) && resetRequested != 0) {
        Reset();
        return;
    }

    int32_t rawValue = 0;
    if (values->GetINT32(rateControlId.c_str(), rawValue)) {
        rateControl = static_cast<RateControl>(rawValue);
    }
    if (values->GetINT32(qualityFactorId.c_str(), rawValue)) qualityFactor = rawValue;
    if (values->GetINT32(bitRateId.c_str(), rawValue)) bitRateKbps = rawValue;
    if (values->GetINT32(maxBitRateId.c_str(), rawValue)) maxBitRateKbps = rawValue;
    if (values->GetINT32(bufferSizeId.c_str(), rawValue)) bufferSizeKbps = rawValue;
    if (values->GetINT32(presetId.c_str(), rawValue)) preset = rawValue;
    if (values->GetINT32(usageId.c_str(), rawValue)) usage = rawValue;

    Normalize();
}

StatusCode AmfSettings::AppendEntry(HostListRef* settingsList, HostUIConfigEntryRef& entry,
                                     const char* context) const {
    if (!entry.IsSuccess() || settingsList == nullptr || !settingsList->Append(&entry)) {
        g_Log(logLevelError, "AMF settings: failed to add %s", context);
        return errFail;
    }
    return errNone;
}

StatusCode AmfSettings::AppendTo(HostListRef* settingsList) const {
    if (settingsList == nullptr || !settingsList->IsValid()) return errInvalidParam;
    const bool isAv1 = descriptor.fourCC == MakeFourCC('a', 'v', '0', '1');

    if (!descriptor.presets.empty()) {
        HostUIConfigEntryRef entry(presetId);
        std::vector<std::string> labels;
        std::vector<int32_t> values;
        for (const PresetOption& option : descriptor.presets) {
            labels.emplace_back(option.label);
            values.push_back(option.value);
        }
        entry.MakeComboBox("Encoder Preset", labels, values, preset);
        entry.SetTriggersUpdate(true);
        if (const StatusCode status = AppendEntry(settingsList, entry, "preset"); status != errNone) return status;
    }

    {
        HostUIConfigEntryRef entry(rateControlId);
        std::vector<std::string> labels;
        std::vector<int32_t> values;
        if ((descriptor.rateControls & ValueOf(RateControl::CQP)) != 0) {
            labels.emplace_back("Constant QP");
            values.push_back(ValueOf(RateControl::CQP));
        }
        if ((descriptor.rateControls & ValueOf(RateControl::VBR)) != 0) {
            labels.emplace_back("Variable Bitrate");
            values.push_back(ValueOf(RateControl::VBR));
        }
        if ((descriptor.rateControls & ValueOf(RateControl::CBR)) != 0) {
            labels.emplace_back("Constant Bitrate");
            values.push_back(ValueOf(RateControl::CBR));
        }
        entry.MakeRadioBox("Rate Control", labels, values, static_cast<int32_t>(rateControl));
        entry.SetTriggersUpdate(true);
        if (const StatusCode status = AppendEntry(settingsList, entry, "rate control"); status != errNone) return status;
    }

    {
        HostUIConfigEntryRef entry(qualityFactorId);
        entry.MakeSlider(isAv1 ? "Q Index" : "QP", "lower is better", qualityFactor,
                         descriptor.qualityMin, descriptor.qualityMax,
                         descriptor.qualityDefault);
        entry.SetTriggersUpdate(true);
        entry.SetHidden(rateControl != RateControl::CQP);
        if (const StatusCode status = AppendEntry(settingsList, entry, "quality factor"); status != errNone) return status;
    }

    {
        HostUIConfigEntryRef entry(bitRateId);
        entry.MakeSlider("Bit Rate", "kb/s", bitRateKbps, 100, 100000, 6000);
        entry.SetHidden(rateControl == RateControl::CQP);
        if (const StatusCode status = AppendEntry(settingsList, entry, "bit rate"); status != errNone) return status;
    }

    {
        HostUIConfigEntryRef entry(maxBitRateId);
        entry.MakeSlider("Max Bit Rate", "kb/s", maxBitRateKbps, 100, 100000, bitRateKbps);
        entry.SetHidden(rateControl != RateControl::VBR);
        if (const StatusCode status = AppendEntry(settingsList, entry, "maximum bit rate"); status != errNone) return status;
    }

    {
        HostUIConfigEntryRef entry(bufferSizeId);
        entry.MakeSlider("Buffer Size", "kbit", bufferSizeKbps, 100, 200000, bitRateKbps * 2);
        entry.SetHidden(rateControl == RateControl::CQP);
        if (const StatusCode status = AppendEntry(settingsList, entry, "buffer size"); status != errNone) return status;
    }

    {
        HostUIConfigEntryRef separator("amf_separator");
        separator.MakeSeparator();
        if (const StatusCode status = AppendEntry(settingsList, separator, "AMF separator"); status != errNone) return status;
    }

    {
        HostUIConfigEntryRef label("amf_label");
        label.MakeLabel("AMF");
        if (const StatusCode status = AppendEntry(settingsList, label, "AMF label"); status != errNone) return status;
    }

    {
        HostUIConfigEntryRef entry(usageId);
        const std::vector<std::string> labels = isAv1
                                                   ? std::vector<std::string>{"Transcoding", "Low Latency",
                                                                              "Ultra Low Latency", "Webcam",
                                                                              "High Quality", "Low Latency High Quality"}
                                                   : std::vector<std::string>{"Transcoding", "Ultra Low Latency",
                                                                              "Low Latency", "Webcam", "High Quality",
                                                                              "Low Latency High Quality"};
        entry.MakeComboBox("Usage", labels, {0, 1, 2, 3, 4, 5}, usage);
        if (const StatusCode status = AppendEntry(settingsList, entry, "usage"); status != errNone) return status;
    }

    {
        HostUIConfigEntryRef reset("amf_reset");
        reset.MakeButton("Reset");
        reset.SetTriggersUpdate(true);
        if (const StatusCode status = AppendEntry(settingsList, reset, "reset button"); status != errNone) return status;
    }

    return errNone;
}

RateControl AmfSettings::GetRateControl() const { return rateControl; }

int32_t AmfSettings::GetQualityFactor() const { return qualityFactor; }

int32_t AmfSettings::GetBitRate() const { return bitRateKbps * 1000; }

int32_t AmfSettings::GetMaxBitRate() const { return maxBitRateKbps * 1000; }

int32_t AmfSettings::GetBufferSize() const { return bufferSizeKbps * 1000; }

int32_t AmfSettings::GetPreset() const { return preset; }

int32_t AmfSettings::GetUsage() const { return usage; }

}
