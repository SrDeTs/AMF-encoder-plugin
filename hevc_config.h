#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace IOPlugin {

bool BuildHevcDecoderConfigurationRecord(const uint8_t* data, size_t size, std::vector<uint8_t>& output,
                                         std::string& error);
bool ConvertHevcSampleToLengthPrefixed(const uint8_t* data, size_t size, std::vector<uint8_t>& output,
                                       std::string& error);

}
