#pragma once

#include "amf_encoder.h"

namespace IOPlugin {

class H265AMFEncoder final : public AMFEncoder {
   public:
    static const EncoderDescriptor descriptor;

    explicit H265AMFEncoder(uint32_t formatIndex);

    static StatusCode RegisterCodecs(HostListRef* list);
    static StatusCode GetEncoderSettings(HostPropertyCollectionRef* values, HostListRef* settingsList);
};

}
