#include "h264_config.h"

#include <limits>

namespace IOPlugin {

namespace {

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

uint32_t ReadU32(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) | static_cast<uint32_t>(data[3]);
}

bool IsLengthPrefixedSample(const uint8_t* data, const size_t size) {
    size_t offset = 0;
    while (offset < size) {
        if (size - offset < 4) return false;
        const uint32_t naluSize = ReadU32(data + offset);
        offset += 4;
        if (naluSize == 0 || naluSize > size - offset) return false;
        offset += naluSize;
    }
    return offset == size;
}

}  // namespace

bool BuildH264DecoderConfigurationRecord(const uint8_t* data, const size_t size, std::vector<uint8_t>& output,
                                         std::string& error) {
    output.clear();
    error.clear();
    if (data == nullptr || size == 0) {
        error = "H.264 codec header is empty";
        return false;
    }

    if (size >= 7 && data[0] == 1) {
        output.assign(data, data + size);
        return true;
    }

    std::vector<std::vector<uint8_t>> sequenceParameterSets;
    std::vector<std::vector<uint8_t>> pictureParameterSets;
    size_t prefixSize = 0;
    size_t start = FindStartCode(data, size, 0, prefixSize);
    if (start != 0) {
        error = "H.264 codec header is not Annex B or avcC";
        return false;
    }

    while (start != size) {
        const size_t naluStart = start + prefixSize;
        size_t nextPrefixSize = 0;
        const size_t next = FindStartCode(data, size, naluStart, nextPrefixSize);
        size_t naluEnd = next;
        while (naluEnd > naluStart && data[naluEnd - 1] == 0) --naluEnd;
        if (naluStart < naluEnd) {
            const uint8_t type = data[naluStart] & 0x1f;
            if (type == 7) {
                sequenceParameterSets.emplace_back(data + naluStart, data + naluEnd);
            } else if (type == 8) {
                pictureParameterSets.emplace_back(data + naluStart, data + naluEnd);
            }
        }
        start = next;
        prefixSize = nextPrefixSize;
    }

    if (sequenceParameterSets.empty() || pictureParameterSets.empty() ||
        sequenceParameterSets.front().size() < 4) {
        error = "H.264 codec header does not contain valid SPS and PPS units";
        return false;
    }
    if (sequenceParameterSets.size() > 31 || pictureParameterSets.size() > 255) {
        error = "H.264 codec header contains too many parameter sets";
        return false;
    }

    const std::vector<uint8_t>& firstSps = sequenceParameterSets.front();
    output.reserve(size + 8);
    output.push_back(1);
    output.push_back(firstSps[1]);
    output.push_back(firstSps[2]);
    output.push_back(firstSps[3]);
    output.push_back(0xff);  // reserved + four-byte NAL length
    output.push_back(static_cast<uint8_t>(0xe0 | sequenceParameterSets.size()));

    for (const std::vector<uint8_t>& nalu : sequenceParameterSets) {
        if (nalu.size() > std::numeric_limits<uint16_t>::max()) {
            output.clear();
            error = "H.264 SPS is too large";
            return false;
        }
        AppendU16(output, static_cast<uint16_t>(nalu.size()));
        output.insert(output.end(), nalu.begin(), nalu.end());
    }

    output.push_back(static_cast<uint8_t>(pictureParameterSets.size()));
    for (const std::vector<uint8_t>& nalu : pictureParameterSets) {
        if (nalu.size() > std::numeric_limits<uint16_t>::max()) {
            output.clear();
            error = "H.264 PPS is too large";
            return false;
        }
        AppendU16(output, static_cast<uint16_t>(nalu.size()));
        output.insert(output.end(), nalu.begin(), nalu.end());
    }
    return true;
}

bool ConvertH264SampleToLengthPrefixed(const uint8_t* data, const size_t size, std::vector<uint8_t>& output,
                                      std::string& error) {
    output.clear();
    error.clear();
    if (data == nullptr || size == 0) {
        error = "H.264 encoded packet is empty";
        return false;
    }

    size_t prefixSize = 0;
    size_t start = FindStartCode(data, size, 0, prefixSize);
    if (start != 0) {
        if (!IsLengthPrefixedSample(data, size)) {
            error = "H.264 encoded packet is neither Annex B nor length-prefixed";
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
                error = "H.264 encoded NAL unit is too large";
                return false;
            }
            AppendU32(output, static_cast<uint32_t>(naluSize));
            output.insert(output.end(), data + naluStart, data + next);
        }
        start = next;
        prefixSize = nextPrefixSize;
    }

    if (output.empty()) {
        error = "H.264 Annex B packet contains no NAL units";
        return false;
    }
    return true;
}

bool H264SampleContainsIdr(const uint8_t* data, const size_t size) {
    if (data == nullptr || size == 0) return false;

    size_t prefixSize = 0;
    size_t start = FindStartCode(data, size, 0, prefixSize);
    if (start == 0) {
        while (start != size) {
            const size_t naluStart = start + prefixSize;
            size_t nextPrefixSize = 0;
            const size_t next = FindStartCode(data, size, naluStart, nextPrefixSize);
            if (naluStart < next && (data[naluStart] & 0x1f) == 5) return true;
            start = next;
            prefixSize = nextPrefixSize;
        }
        return false;
    }

    size_t offset = 0;
    while (offset < size) {
        if (size - offset < 4) return false;
        const uint32_t naluSize = ReadU32(data + offset);
        offset += 4;
        if (naluSize == 0 || naluSize > size - offset) return false;
        if ((data[offset] & 0x1f) == 5) return true;
        offset += naluSize;
    }
    return false;
}

}
