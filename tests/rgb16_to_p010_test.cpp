#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>

#include "rgb16_to_p010.h"

namespace {

void WritePixel(uint8_t* destination, const uint16_t r, const uint16_t g, const uint16_t b) {
    const uint16_t components[] = {
        static_cast<uint16_t>(r << 6),
        static_cast<uint16_t>(g << 6),
        static_cast<uint16_t>(b << 6),
    };
    std::memcpy(destination, components, sizeof(components));
}

uint16_t ReadCode(const uint8_t* source) {
    uint16_t value = 0;
    std::memcpy(&value, source, sizeof(value));
    return value >> 6;
}

bool CheckUniformFrame(const uint16_t r, const uint16_t g, const uint16_t b, const bool fullRange,
                       const uint16_t expectedY, const uint16_t expectedU, const uint16_t expectedV) {
    constexpr size_t sourceStride = 16;
    constexpr size_t yStride = 8;
    constexpr size_t uvStride = 8;
    std::array<uint8_t, sourceStride * 2> source{};
    std::array<uint8_t, yStride * 2> y{};
    std::array<uint8_t, uvStride> uv{};

    for (size_t row = 0; row < 2; ++row) {
        WritePixel(source.data() + row * sourceStride, r, g, b);
        WritePixel(source.data() + row * sourceStride + 6, r, g, b);
    }

    IOPlugin::ConvertRgb16ToP010(source.data(), sourceStride, y.data(), yStride, uv.data(), uvStride, 2, 2,
                                 fullRange);

    const bool valid = ReadCode(y.data()) == expectedY && ReadCode(y.data() + 2) == expectedY &&
                       ReadCode(y.data() + yStride) == expectedY &&
                       ReadCode(y.data() + yStride + 2) == expectedY && ReadCode(uv.data()) == expectedU &&
                       ReadCode(uv.data() + 2) == expectedV;
    if (!valid) {
        std::cerr << "RGB(" << r << ", " << g << ", " << b << ") produced YUV(" << ReadCode(y.data())
                  << ", " << ReadCode(uv.data()) << ", " << ReadCode(uv.data() + 2) << ")\n";
    }
    return valid;
}

}

int main() {
    bool valid = true;
    valid &= CheckUniformFrame(64, 64, 64, false, 64, 512, 512);
    valid &= CheckUniformFrame(940, 940, 940, false, 940, 512, 512);
    valid &= CheckUniformFrame(940, 64, 64, false, 250, 409, 960);
    valid &= CheckUniformFrame(1023, 0, 0, true, 217, 395, 1023);
    return valid ? 0 : 1;
}
