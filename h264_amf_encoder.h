#pragma once

#include "amf_encoder.h"

namespace IOPlugin {

class H264AMFEncoder final : public AMFEncoder {
   public:
    static const EncoderDescriptor descriptor;

    explicit H264AMFEncoder(uint32_t formatIndex);

    static StatusCode RegisterCodecs(HostListRef* list);
    static StatusCode GetEncoderSettings(HostPropertyCollectionRef* values, HostListRef* settingsList);
};

}
