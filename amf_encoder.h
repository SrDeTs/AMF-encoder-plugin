#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <AMF/components/VideoEncoderAV1.h>
#include <AMF/components/VideoEncoderVCE.h>
#include <AMF/core/Factory.h>

#include "amf_settings.h"
#include "encoder_info.h"
#include "wrapper/plugin_api.h"

namespace IOPlugin {

class AMFEncoder : public IPluginCodecRef {
   protected:
    AMFEncoder(const EncoderDescriptor& descriptor, uint32_t formatIndex);
    ~AMFEncoder() override;

    StatusCode DoInit(HostPropertyCollectionRef* properties) override;
    StatusCode DoOpen(HostBufferRef* buffer) override;
    StatusCode DoProcess(HostBufferRef* buffer) override;
    void DoFlush() override;

    static StatusCode RegisterCodecs(HostListRef* list, const EncoderDescriptor& descriptor);
    static StatusCode GetEncoderSettings(HostPropertyCollectionRef* values, HostListRef* settingsList,
                                         const EncoderDescriptor& descriptor);

   private:
    StatusCode ConfigureEncoder(HostBufferRef* buffer, uint32_t frameRateNum, uint32_t frameRateDen);
    StatusCode CopyInputFrame(HostBufferRef* buffer, amf::AMFSurfacePtr& surface);
    StatusCode EmitPackets(HostBufferRef* errorBuffer);
    StatusCode SetMagicCookie(HostBufferRef* buffer);
    StatusCode SetError(HostBufferRef* buffer, StatusCode status, const std::string& message) const;
    void ReleaseResources();

    const EncoderDescriptor& descriptor;
    uint32_t formatIndex;
    HostCodecConfigCommon commonConfig{};
    std::unique_ptr<AmfSettings> settings{};

    amf::AMFFactory* factory{nullptr};
    amf::AMFContext* context{nullptr};
    amf::AMFComponent* encoder{nullptr};
    std::mutex processMutex{};
    bool drainRequested{false};
    int width{0};
    int height{0};
};

}
