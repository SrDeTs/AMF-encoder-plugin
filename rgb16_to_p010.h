#pragma once

#include <cstddef>
#include <cstdint>

namespace IOPlugin {

void ConvertRgb16ToP010(const uint8_t* source, size_t sourceStride, uint8_t* destinationY,
                        size_t destinationYStride, uint8_t* destinationUv, size_t destinationUvStride,
                        int width, int height, bool fullRange);

}
