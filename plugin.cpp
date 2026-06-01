#include "plugin.h"

#include <cstring>

#include "av1_amf_encoder.h"
#include "ffmpeg_encoder.h"
#include "h264_amf_encoder.h"
#include "uisettings_controller.h"

static const uint8_t UUID[] = {0xda, 0xb6, 0x8f, 0x7a, 0xf1, 0xea, 0x42, 0x45,
                               0x94, 0xcf, 0xd9, 0x8a, 0xe0, 0xea, 0x20, 0x39};

StatusCode g_HandleGetInfo(HostPropertyCollectionRef* p_pProps) {
    StatusCode err = p_pProps->SetProperty(pIOPropUUID, propTypeUInt8, UUID, 16);
    if (err == errNone) {
        const char* name = "AMF Plugin";
        err = p_pProps->SetProperty(pIOPropName, propTypeString, name, static_cast<int>(strlen(name)));
    }

    return err;
}

StatusCode g_HandleCreateObj(unsigned char* p_pUUID, ObjectRef* p_ppObj) {
    if (memcmp(p_pUUID, H264AMFEncoder::encoderInfo.UUID, 15) == 0) {
        const uint8_t formatIndex = p_pUUID[15] - H264AMFEncoder::encoderInfo.UUID[15];
        *p_ppObj = new H264AMFEncoder(formatIndex);
        return errNone;
    }

    if (memcmp(p_pUUID, Av1AMFEncoder::encoderInfo.UUID, 15) == 0) {
        const uint8_t formatIndex = p_pUUID[15] - Av1AMFEncoder::encoderInfo.UUID[15];
        *p_ppObj = new Av1AMFEncoder(formatIndex);
        return errNone;
    }

    return errUnsupported;
}

StatusCode g_HandlePluginStart() { return errNone; }

StatusCode g_HandlePluginTerminate() { return errNone; }

StatusCode g_ListCodecs(HostListRef* p_pList) {
    StatusCode err = H264AMFEncoder::RegisterCodecs(p_pList);
    if (err != errNone) return err;

    err = Av1AMFEncoder::RegisterCodecs(p_pList);
    if (err != errNone) return err;

    return errNone;
}

StatusCode g_ListContainers(HostListRef* p_pList) { return errNone; }

StatusCode g_GetEncoderSettings(unsigned char* p_pUUID, HostPropertyCollectionRef* p_pValues,
                                HostListRef* p_pSettingsList) {
    if (memcmp(p_pUUID, H264AMFEncoder::encoderInfo.UUID, 15) == 0) {
        return H264AMFEncoder::GetEncoderSettings(p_pValues, p_pSettingsList);
    }

    if (memcmp(p_pUUID, Av1AMFEncoder::encoderInfo.UUID, 15) == 0) {
        return Av1AMFEncoder::GetEncoderSettings(p_pValues, p_pSettingsList);
    }

    return errNoCodec;
}
