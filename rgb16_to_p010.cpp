#include "rgb16_to_p010.h"

#include <algorithm>
#include <cstring>

namespace IOPlugin {

namespace {

struct Rgb10 {
    int32_t r;
    int32_t g;
    int32_t b;
};

Rgb10 ReadRgb10(const uint8_t* pixel, const bool fullRange) {
    uint16_t components[3]{};
    std::memcpy(components, pixel, sizeof(components));
    const int32_t minimum = fullRange ? 0 : 64;
    const int32_t maximum = fullRange ? 1023 : 940;
    return {
        .r = std::clamp<int32_t>(components[0] >> 6, minimum, maximum),
        .g = std::clamp<int32_t>(components[1] >> 6, minimum, maximum),
        .b = std::clamp<int32_t>(components[2] >> 6, minimum, maximum),
    };
}

int32_t RgbToLuma10(const Rgb10& rgb) {
    return (13933 * rgb.r + 46871 * rgb.g + 4732 * rgb.b + 32768) >> 16;
}

int32_t ScaleSigned(const int32_t value, const int32_t scale) {
    const int64_t product = int64_t(value) * scale;
    return product >= 0 ? static_cast<int32_t>((product + 32768) >> 16)
                        : -static_cast<int32_t>((-product + 32768) >> 16);
}

uint16_t RgbToP010Luma(const Rgb10& rgb, const bool fullRange) {
    const int32_t minimum = fullRange ? 0 : 64;
    const int32_t maximum = fullRange ? 1023 : 940;
    return static_cast<uint16_t>(std::clamp(RgbToLuma10(rgb), minimum, maximum) << 6);
}

void RgbToP010Chroma(const Rgb10& rgb, const bool fullRange, uint16_t& u, uint16_t& v) {
    const int32_t luma10 = RgbToLuma10(rgb);
    const int32_t uScale = fullRange ? 35318 : 36124;
    const int32_t vScale = fullRange ? 41615 : 42566;
    const int32_t minimum = fullRange ? 0 : 64;
    const int32_t maximum = fullRange ? 1023 : 960;
    const int32_t uCode = std::clamp(512 + ScaleSigned(rgb.b - luma10, uScale), minimum, maximum);
    const int32_t vCode = std::clamp(512 + ScaleSigned(rgb.r - luma10, vScale), minimum, maximum);
    u = static_cast<uint16_t>(uCode << 6);
    v = static_cast<uint16_t>(vCode << 6);
}

}

void ConvertRgb16ToP010(const uint8_t* source, const size_t sourceStride, uint8_t* destinationY,
                        const size_t destinationYStride, uint8_t* destinationUv,
                        const size_t destinationUvStride, const int width, const int height, const bool fullRange) {
    for (int row = 0; row < height / 2; ++row) {
        uint8_t* destinationYTop = destinationY + static_cast<size_t>(row * 2) * destinationYStride;
        uint8_t* destinationYBottom = destinationYTop + destinationYStride;
        uint8_t* destinationUvRow = destinationUv + static_cast<size_t>(row) * destinationUvStride;
        const uint8_t* sourceTop = source + static_cast<size_t>(row * 2) * sourceStride;
        const uint8_t* sourceBottom = sourceTop + sourceStride;
        for (int column = 0; column < width / 2; ++column) {
            const size_t sourceOffset = static_cast<size_t>(column * 2) * 6;
            const size_t yOffset = static_cast<size_t>(column * 2) * 2;
            const Rgb10 topLeft = ReadRgb10(sourceTop + sourceOffset, fullRange);
            const Rgb10 topRight = ReadRgb10(sourceTop + sourceOffset + 6, fullRange);
            const Rgb10 bottomLeft = ReadRgb10(sourceBottom + sourceOffset, fullRange);
            const Rgb10 bottomRight = ReadRgb10(sourceBottom + sourceOffset + 6, fullRange);
            const uint16_t yTopLeft = RgbToP010Luma(topLeft, fullRange);
            const uint16_t yTopRight = RgbToP010Luma(topRight, fullRange);
            const uint16_t yBottomLeft = RgbToP010Luma(bottomLeft, fullRange);
            const uint16_t yBottomRight = RgbToP010Luma(bottomRight, fullRange);
            std::memcpy(destinationYTop + yOffset, &yTopLeft, sizeof(yTopLeft));
            std::memcpy(destinationYTop + yOffset + 2, &yTopRight, sizeof(yTopRight));
            std::memcpy(destinationYBottom + yOffset, &yBottomLeft, sizeof(yBottomLeft));
            std::memcpy(destinationYBottom + yOffset + 2, &yBottomRight, sizeof(yBottomRight));

            const Rgb10 average{
                .r = (topLeft.r + topRight.r + bottomLeft.r + bottomRight.r + 2) / 4,
                .g = (topLeft.g + topRight.g + bottomLeft.g + bottomRight.g + 2) / 4,
                .b = (topLeft.b + topRight.b + bottomLeft.b + bottomRight.b + 2) / 4,
            };
            uint16_t u = 0;
            uint16_t v = 0;
            RgbToP010Chroma(average, fullRange, u, v);
            std::memcpy(destinationUvRow + static_cast<size_t>(column) * 4, &u, sizeof(u));
            std::memcpy(destinationUvRow + static_cast<size_t>(column) * 4 + 2, &v, sizeof(v));
        }
    }
}

}
