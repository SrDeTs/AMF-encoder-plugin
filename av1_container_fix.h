#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace amf_plugin {

enum class Av1ContainerFixStatus {
    Fixed,
    AlreadyFixed,
    Unsupported,
    Invalid,
};

struct Av1ContainerFixResult {
    Av1ContainerFixStatus status = Av1ContainerFixStatus::Invalid;
    std::string message;
};

Av1ContainerFixResult FixAv1Mkv1080(std::vector<std::uint8_t>& data);

}  // namespace amf_plugin
