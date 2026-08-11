#include "hevc_config.h"

#include <array>
#include <limits>

namespace IOPlugin {

namespace {

class BitReader final {
   public:
    explicit BitReader(const std::vector<uint8_t>& bytes) : bytes(bytes) {}

    bool ReadBits(const unsigned count, uint64_t& value) {
        if (count > 64 || bitOffset > bytes.size() * 8 || count > bytes.size() * 8 - bitOffset) return false;
        value = 0;
        for (unsigned index = 0; index < count; ++index) {
            value = (value << 1) | ((bytes[bitOffset / 8] >> (7 - bitOffset % 8)) & 1U);
            ++bitOffset;
        }
        return true;
    }

    bool SkipBits(const size_t count) {
        if (bitOffset > bytes.size() * 8 || count > bytes.size() * 8 - bitOffset) return false;
        bitOffset += count;
        return true;
    }

    bool ReadUnsignedExpGolomb(uint32_t& value) {
        unsigned leadingZeros = 0;
        uint64_t bit = 0;
        while (true) {
            if (!ReadBits(1, bit)) return false;
            if (bit != 0) break;
            if (++leadingZeros > 31) return false;
        }

        uint64_t suffix = 0;
        if (leadingZeros != 0 && !ReadBits(leadingZeros, suffix)) return false;
        const uint64_t decoded = ((uint64_t{1} << leadingZeros) - 1) + suffix;
        if (decoded > std::numeric_limits<uint32_t>::max()) return false;
        value = static_cast<uint32_t>(decoded);
        return true;
    }

   private:
    const std::vector<uint8_t>& bytes;
    size_t bitOffset{0};
};

struct HevcProfileTierLevel {
    uint8_t profileSpace{0};
    uint8_t tierFlag{0};
    uint8_t profileIdc{0};
    uint32_t compatibilityFlags{0};
    std::array<uint8_t, 6> constraintFlags{};
    uint8_t levelIdc{0};
};

struct HevcSpsInfo {
    HevcProfileTierLevel profile{};
    uint8_t maxSubLayersMinus1{0};
    uint8_t temporalIdNested{0};
    uint8_t chromaFormat{1};
    uint8_t bitDepthLumaMinus8{0};
    uint8_t bitDepthChromaMinus8{0};
};

struct NaluArray {
    uint8_t type{0};
    std::vector<std::vector<uint8_t>> nalus{};
};

size_t FindStartCode(const uint8_t* data, const size_t size, const size_t begin, size_t& prefixSize) {
    for (size_t offset = begin; offset + 3 <= size; ++offset) {
        if (offset + 4 <= size && data[offset] == 0 && data[offset + 1] == 0 && data[offset + 2] == 0 &&
            data[offset + 3] == 1) {
            prefixSize = 4;
            return offset;
        }
        if (data[offset] == 0 && data[offset + 1] == 0 && data[offset + 2] == 1) {
            prefixSize = 3;
            return offset;
        }
    }
    return size;
}

bool SplitAnnexB(const uint8_t* data, const size_t size, std::array<NaluArray, 3>& arrays) {
    arrays[0].type = 32;
    arrays[1].type = 33;
    arrays[2].type = 34;

    size_t prefixSize = 0;
    size_t start = FindStartCode(data, size, 0, prefixSize);
    while (start != size) {
        const size_t naluStart = start + prefixSize;
        size_t nextPrefixSize = 0;
        const size_t next = FindStartCode(data, size, naluStart, nextPrefixSize);
        size_t naluEnd = next;
        while (naluEnd > naluStart && data[naluEnd - 1] == 0) --naluEnd;

        if (naluEnd >= naluStart + 2) {
            const uint8_t type = static_cast<uint8_t>((data[naluStart] >> 1) & 0x3f);
            for (NaluArray& array : arrays) {
                if (array.type == type) {
                    array.nalus.emplace_back(data + naluStart, data + naluEnd);
                    break;
                }
            }
        }

        start = next;
        prefixSize = nextPrefixSize;
    }

    return !arrays[0].nalus.empty() && !arrays[1].nalus.empty() && !arrays[2].nalus.empty();
}

std::vector<uint8_t> ToRbsp(const std::vector<uint8_t>& nalu) {
    std::vector<uint8_t> rbsp;
    if (nalu.size() <= 2) return rbsp;
    rbsp.reserve(nalu.size() - 2);

    unsigned zeroCount = 0;
    for (size_t index = 2; index < nalu.size(); ++index) {
        const uint8_t byte = nalu[index];
        if (zeroCount >= 2 && byte == 3) {
            zeroCount = 0;
            continue;
        }
        rbsp.push_back(byte);
        zeroCount = byte == 0 ? zeroCount + 1 : 0;
    }
    return rbsp;
}

bool ReadProfileTierLevel(BitReader& reader, const uint8_t maxSubLayersMinus1, HevcProfileTierLevel& profile) {
    uint64_t value = 0;
    if (!reader.ReadBits(2, value)) return false;
    profile.profileSpace = static_cast<uint8_t>(value);
    if (!reader.ReadBits(1, value)) return false;
    profile.tierFlag = static_cast<uint8_t>(value);
    if (!reader.ReadBits(5, value)) return false;
    profile.profileIdc = static_cast<uint8_t>(value);
    if (!reader.ReadBits(32, value)) return false;
    profile.compatibilityFlags = static_cast<uint32_t>(value);
    for (uint8_t& byte : profile.constraintFlags) {
        if (!reader.ReadBits(8, value)) return false;
        byte = static_cast<uint8_t>(value);
    }
    if (!reader.ReadBits(8, value)) return false;
    profile.levelIdc = static_cast<uint8_t>(value);

    std::array<uint8_t, 7> profilePresent{};
    std::array<uint8_t, 7> levelPresent{};
    for (uint8_t layer = 0; layer < maxSubLayersMinus1; ++layer) {
        if (!reader.ReadBits(1, value)) return false;
        profilePresent[layer] = static_cast<uint8_t>(value);
        if (!reader.ReadBits(1, value)) return false;
        levelPresent[layer] = static_cast<uint8_t>(value);
    }
    if (maxSubLayersMinus1 > 0 && !reader.SkipBits((8 - maxSubLayersMinus1) * 2)) return false;

    for (uint8_t layer = 0; layer < maxSubLayersMinus1; ++layer) {
        if (profilePresent[layer] != 0 && !reader.SkipBits(88)) return false;
        if (levelPresent[layer] != 0 && !reader.SkipBits(8)) return false;
    }
    return true;
}

bool ParseSps(const std::vector<uint8_t>& sps, HevcSpsInfo& info) {
    const std::vector<uint8_t> rbsp = ToRbsp(sps);
    if (rbsp.empty()) return false;

    BitReader reader(rbsp);
    uint64_t value = 0;
    if (!reader.SkipBits(4) || !reader.ReadBits(3, value)) return false;
    info.maxSubLayersMinus1 = static_cast<uint8_t>(value);
    if (!reader.ReadBits(1, value)) return false;
    info.temporalIdNested = static_cast<uint8_t>(value);
    if (!ReadProfileTierLevel(reader, info.maxSubLayersMinus1, info.profile)) return false;

    uint32_t unsignedValue = 0;
    if (!reader.ReadUnsignedExpGolomb(unsignedValue)) return false;  // sps_seq_parameter_set_id
    if (!reader.ReadUnsignedExpGolomb(unsignedValue) || unsignedValue > 3) return false;
    info.chromaFormat = static_cast<uint8_t>(unsignedValue);
    if (info.chromaFormat == 3 && !reader.SkipBits(1)) return false;
    if (!reader.ReadUnsignedExpGolomb(unsignedValue)) return false;  // pic_width_in_luma_samples
    if (!reader.ReadUnsignedExpGolomb(unsignedValue)) return false;  // pic_height_in_luma_samples
    if (!reader.ReadBits(1, value)) return false;
    if (value != 0) {
        for (unsigned index = 0; index < 4; ++index) {
            if (!reader.ReadUnsignedExpGolomb(unsignedValue)) return false;
        }
    }
    if (!reader.ReadUnsignedExpGolomb(unsignedValue) || unsignedValue > 7) return false;
    info.bitDepthLumaMinus8 = static_cast<uint8_t>(unsignedValue);
    if (!reader.ReadUnsignedExpGolomb(unsignedValue) || unsignedValue > 7) return false;
    info.bitDepthChromaMinus8 = static_cast<uint8_t>(unsignedValue);
    return true;
}

void AppendU16(std::vector<uint8_t>& output, const uint16_t value) {
    output.push_back(static_cast<uint8_t>(value >> 8));
    output.push_back(static_cast<uint8_t>(value));
}

void AppendU32(std::vector<uint8_t>& output, const uint32_t value) {
    output.push_back(static_cast<uint8_t>(value >> 24));
    output.push_back(static_cast<uint8_t>(value >> 16));
    output.push_back(static_cast<uint8_t>(value >> 8));
    output.push_back(static_cast<uint8_t>(value));
}

bool IsLengthPrefixedSample(const uint8_t* data, const size_t size) {
    size_t offset = 0;
    while (offset < size) {
        if (size - offset < 4) return false;
        const uint32_t naluSize = (static_cast<uint32_t>(data[offset]) << 24) |
                                  (static_cast<uint32_t>(data[offset + 1]) << 16) |
                                  (static_cast<uint32_t>(data[offset + 2]) << 8) |
                                  static_cast<uint32_t>(data[offset + 3]);
        offset += 4;
        if (naluSize == 0 || naluSize > size - offset) return false;
        offset += naluSize;
    }
    return offset == size;
}

}  // namespace

bool BuildHevcDecoderConfigurationRecord(const uint8_t* data, const size_t size, std::vector<uint8_t>& output,
                                         std::string& error) {
    output.clear();
    error.clear();
    if (data == nullptr || size == 0) {
        error = "HEVC codec header is empty";
        return false;
    }

    if (size >= 23 && data[0] == 1) {
        output.assign(data, data + size);
        return true;
    }

    std::array<NaluArray, 3> arrays{};
    if (!SplitAnnexB(data, size, arrays)) {
        error = "HEVC codec header does not contain VPS, SPS, and PPS";
        return false;
    }

    HevcSpsInfo info;
    if (!ParseSps(arrays[1].nalus.front(), info)) {
        error = "Could not parse HEVC SPS";
        return false;
    }

    output.reserve(size + 32);
    output.push_back(1);
    output.push_back(static_cast<uint8_t>((info.profile.profileSpace << 6) | (info.profile.tierFlag << 5) |
                                          info.profile.profileIdc));
    AppendU32(output, info.profile.compatibilityFlags);
    output.insert(output.end(), info.profile.constraintFlags.begin(), info.profile.constraintFlags.end());
    output.push_back(info.profile.levelIdc);
    AppendU16(output, 0xf000);  // reserved + min_spatial_segmentation_idc
    output.push_back(0xfc);     // reserved + parallelismType unknown
    output.push_back(static_cast<uint8_t>(0xfc | info.chromaFormat));
    output.push_back(static_cast<uint8_t>(0xf8 | info.bitDepthLumaMinus8));
    output.push_back(static_cast<uint8_t>(0xf8 | info.bitDepthChromaMinus8));
    AppendU16(output, 0);  // avgFrameRate unknown
    const uint8_t temporalLayers = static_cast<uint8_t>(info.maxSubLayersMinus1 + 1);
    output.push_back(static_cast<uint8_t>((temporalLayers << 3) | (info.temporalIdNested << 2) | 3));
    output.push_back(static_cast<uint8_t>(arrays.size()));

    for (const NaluArray& array : arrays) {
        output.push_back(static_cast<uint8_t>(0x80 | array.type));
        AppendU16(output, static_cast<uint16_t>(array.nalus.size()));
        for (const std::vector<uint8_t>& nalu : array.nalus) {
            if (nalu.size() > std::numeric_limits<uint16_t>::max()) {
                output.clear();
                error = "HEVC parameter set is too large";
                return false;
            }
            AppendU16(output, static_cast<uint16_t>(nalu.size()));
            output.insert(output.end(), nalu.begin(), nalu.end());
        }
    }
    return true;
}

bool ConvertHevcSampleToLengthPrefixed(const uint8_t* data, const size_t size, std::vector<uint8_t>& output,
                                       std::string& error) {
    output.clear();
    error.clear();
    if (data == nullptr || size == 0) {
        error = "HEVC encoded packet is empty";
        return false;
    }

    size_t prefixSize = 0;
    size_t start = FindStartCode(data, size, 0, prefixSize);
    if (start != 0) {
        if (!IsLengthPrefixedSample(data, size)) {
            error = "HEVC encoded packet is neither Annex B nor length-prefixed";
            return false;
        }
        output.assign(data, data + size);
        return true;
    }

    output.reserve(size);
    while (start != size) {
        const size_t naluStart = start + prefixSize;
        size_t nextPrefixSize = 0;
        const size_t next = FindStartCode(data, size, naluStart, nextPrefixSize);
        if (naluStart < next) {
            const size_t naluSize = next - naluStart;
            if (naluSize > std::numeric_limits<uint32_t>::max()) {
                output.clear();
                error = "HEVC encoded NAL unit is too large";
                return false;
            }
            AppendU32(output, static_cast<uint32_t>(naluSize));
            output.insert(output.end(), data + naluStart, data + next);
        }
        start = next;
        prefixSize = nextPrefixSize;
    }

    if (output.empty()) {
        error = "HEVC Annex B packet contains no NAL units";
        return false;
    }
    return true;
}

}
