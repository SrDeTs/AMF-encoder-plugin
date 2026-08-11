#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <AMF/components/VideoEncoderAV1.h>
#include <AMF/components/VideoEncoderHEVC.h>
#include <AMF/components/VideoEncoderVCE.h>
#include <AMF/core/Factory.h>
#include <AMF/core/Surface.h>

#include "amf_settings.h"
#include "encoder_info.h"
#include "wrapper/plugin_api.h"

namespace IOPlugin {

class AMFEncoder : public IPluginCodecRef, private amf::AMFSurfaceObserver {
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
    struct EncodedPacket {
        std::vector<uint8_t> bytes;
        int64_t pts{};
        bool keyFrame{};
    };

    struct SurfaceSlot {
        std::vector<uint8_t> bytes;
        amf::AMFSurface* activeSurface{nullptr};
        bool available{true};
    };

    StatusCode ConfigureEncoder(HostBufferRef* buffer, uint32_t frameRateNum, uint32_t frameRateDen);
    StatusCode CopyInputFrame(HostBufferRef* buffer, amf::AMFSurfacePtr& surface);
    StatusCode InitializeSurfacePool(HostBufferRef* errorBuffer);
    StatusCode AcquireSurface(HostBufferRef* errorBuffer, amf::AMFSurfacePtr& surface);
    void ReleaseSurface(amf::AMFSurface* surface);
    void ClearSurfacePool();
    void AMF_STD_CALL OnSurfaceDataRelease(amf::AMFSurface* surface) override;

    StatusCode StartOutputThread(HostBufferRef* errorBuffer);
    void StopOutputThread();
    void PollOutput();
    void SetOutputError(const std::string& message);
    std::string GetOutputError() const;
    StatusCode QueueOutputPacket(amf::AMFData* data);
    StatusCode EmitPendingPackets(HostBufferRef* errorBuffer);
    StatusCode FinishDrain(HostBufferRef* errorBuffer);

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
    std::mutex surfaceMutex{};
    std::condition_variable surfaceAvailable{};
    std::vector<SurfaceSlot> surfacePool{};
    int surfacePitch{0};

    std::thread outputThread{};
    std::atomic_bool stopOutput{false};
    std::atomic_bool outputFailed{false};
    mutable std::mutex outputMutex{};
    std::condition_variable outputAvailable{};
    std::deque<EncodedPacket> pendingPackets{};
    std::string outputError{};
    bool outputEof{false};

    bool drainRequested{false};
    bool inputLayoutLogged{false};
    bool inputAlignmentKnown{false};
    bool shift10BitSamples{false};
    int width{0};
    int height{0};
};

}
