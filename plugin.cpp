#include "plugin.h"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>

#include "av1_amf_encoder.h"
#include "h264_amf_encoder.h"
#include "h265_amf_encoder.h"

using namespace IOPlugin;

namespace {

constexpr std::array<uint8_t, 16> kPluginUuid = {0x7c, 0x12, 0x4d, 0x6a, 0x8e, 0x99, 0x45, 0x36,
                                                   0x91, 0x70, 0x31, 0x21, 0xe2, 0x4f, 0xb8, 0x5c};

template <typename Encoder>
bool MatchCodecUuid(const unsigned char* uuid, uint32_t& formatIndex) {
    if (uuid == nullptr) return false;

    for (size_t index = 0; index < Encoder::descriptor.formats.size(); ++index) {
        std::array<uint8_t, 16> candidate = Encoder::descriptor.uuid;
        candidate[15] = static_cast<uint8_t>(candidate[15] + index);
        if (std::memcmp(uuid, candidate.data(), candidate.size()) == 0) {
            formatIndex = static_cast<uint32_t>(index);
            return true;
        }
    }
    return false;
}

}  // namespace

StatusCode g_HandleGetInfo(HostPropertyCollectionRef* properties) {
    if (properties == nullptr) return errInvalidParam;

    const StatusCode uuidStatus =
        properties->SetProperty(pIOPropUUID, propTypeUInt8, kPluginUuid.data(), static_cast<int>(kPluginUuid.size()));
    if (uuidStatus != errNone) return uuidStatus;

    constexpr char kPluginName[] = "AMF Encoder";
    return properties->SetProperty(pIOPropName, propTypeString, kPluginName, sizeof(kPluginName) - 1);
}

StatusCode g_HandleCreateObj(unsigned char* uuid, ObjectRef* object) {
    if (uuid == nullptr || object == nullptr) return errInvalidParam;

    uint32_t formatIndex = 0;
    if (MatchCodecUuid<H264AMFEncoder>(uuid, formatIndex)) {
        *object = new H264AMFEncoder(formatIndex);
        return errNone;
    }
    if (MatchCodecUuid<H265AMFEncoder>(uuid, formatIndex)) {
        *object = new H265AMFEncoder(formatIndex);
        return errNone;
    }
    if (MatchCodecUuid<Av1AMFEncoder>(uuid, formatIndex)) {
        *object = new Av1AMFEncoder(formatIndex);
        return errNone;
    }
    return errNoCodec;
}

StatusCode g_HandlePluginStart() {
#if defined(__linux__)
    if (setenv("DISABLE_LSFG", "1", 0) != 0) {
        g_Log(logLevelError, "AMF encoder: cannot set DISABLE_LSFG (errno=%d)", errno);
        return errFail;
    }
#endif
    return errNone;
}

StatusCode g_HandlePluginTerminate() { return errNone; }

StatusCode g_ListCodecs(HostListRef* list) {
    if (const StatusCode status = H264AMFEncoder::RegisterCodecs(list); status != errNone) return status;
    if (const StatusCode status = H265AMFEncoder::RegisterCodecs(list); status != errNone) return status;
    return Av1AMFEncoder::RegisterCodecs(list);
}

StatusCode g_ListContainers(HostListRef* /*list*/) { return errNone; }

StatusCode g_GetEncoderSettings(unsigned char* uuid, HostPropertyCollectionRef* values, HostListRef* settingsList) {
    uint32_t ignoredIndex = 0;
    if (MatchCodecUuid<H264AMFEncoder>(uuid, ignoredIndex)) {
        return H264AMFEncoder::GetEncoderSettings(values, settingsList);
    }
    if (MatchCodecUuid<H265AMFEncoder>(uuid, ignoredIndex)) {
        return H265AMFEncoder::GetEncoderSettings(values, settingsList);
    }
    if (MatchCodecUuid<Av1AMFEncoder>(uuid, ignoredIndex)) {
        return Av1AMFEncoder::GetEncoderSettings(values, settingsList);
    }
    return errNoCodec;
}
