#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace IOPlugin {

bool BuildH264DecoderConfigurationRecord(const uint8_t* data, size_t size, std::vector<uint8_t>& output,
                                         std::string& error);
bool ConvertH264SampleToLengthPrefixed(const uint8_t* data, size_t size, std::vector<uint8_t>& output,
                                      std::string& error);
bool H264SampleContainsIdr(const uint8_t* data, size_t size);

}
