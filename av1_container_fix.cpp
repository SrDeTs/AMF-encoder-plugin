#include "av1_container_fix.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <string_view>

namespace amf_plugin {
namespace {

constexpr std::uint64_t kEbml = 0x1A45DFA3;
constexpr std::uint64_t kSegment = 0x18538067;
constexpr std::uint64_t kSeekHead = 0x114D9B74;
constexpr std::uint64_t kSeek = 0x4DBB;
constexpr std::uint64_t kSeekId = 0x53AB;
constexpr std::uint64_t kSeekPosition = 0x53AC;
constexpr std::uint64_t kVoid = 0xEC;
constexpr std::uint64_t kTracks = 0x1654AE6B;
constexpr std::uint64_t kTrackEntry = 0xAE;
constexpr std::uint64_t kCodecId = 0x86;
constexpr std::uint64_t kVideo = 0xE0;
constexpr std::uint64_t kPixelHeight = 0xBA;
constexpr std::uint64_t kPixelCropBottom = 0x54AA;
constexpr std::uint64_t kDisplayUnit = 0x54B2;
constexpr std::uint64_t kDisplayHeight = 0x54BA;

struct Vint {
    std::uint64_t value = 0;
    std::size_t width = 0;
    bool unknown = false;
};

struct Element {
    std::uint64_t id = 0;
    std::size_t offset = 0;
    std::size_t id_width = 0;
    std::size_t size_width = 0;
    std::size_t header_size = 0;
    std::size_t data_offset = 0;
    std::size_t data_size = 0;
    std::size_t end = 0;
};

std::optional<Vint> ReadVint(const std::vector<std::uint8_t>& data,
                             std::size_t offset,
                             std::size_t max_width,
                             bool strip_marker) {
    if (offset >= data.size() || data[offset] == 0) {
        return std::nullopt;
    }

    const std::uint8_t first = data[offset];
    std::size_t width = 1;
    std::uint8_t marker = 0x80;
    while ((first & marker) == 0) {
        marker >>= 1;
        ++width;
    }
    if (width > max_width || offset + width > data.size()) {
        return std::nullopt;
    }

    std::uint64_t value = strip_marker ? first & (marker - 1) : first;
    for (std::size_t i = 1; i < width; ++i) {
        value = (value << 8) | data[offset + i];
    }

    bool unknown = false;
    if (strip_marker) {
        const std::uint64_t maximum = width == 8
                                          ? (std::uint64_t{1} << 56) - 1
                                          : (std::uint64_t{1} << (width * 7)) - 1;
        unknown = value == maximum;
    }
    return Vint{value, width, unknown};
}

std::optional<Element> ReadElement(const std::vector<std::uint8_t>& data,
                                   std::size_t offset,
                                   std::size_t parent_end) {
    const auto id = ReadVint(data, offset, 4, false);
    if (!id) {
        return std::nullopt;
    }
    const auto size = ReadVint(data, offset + id->width, 8, true);
    if (!size) {
        return std::nullopt;
    }

    const std::size_t header_size = id->width + size->width;
    const std::size_t data_offset = offset + header_size;
    if (data_offset > parent_end) {
        return std::nullopt;
    }

    std::size_t data_size = parent_end - data_offset;
    if (!size->unknown) {
        if (size->value > std::numeric_limits<std::size_t>::max() ||
            size->value > parent_end - data_offset) {
            return std::nullopt;
        }
        data_size = static_cast<std::size_t>(size->value);
    }

    return Element{id->value, offset, id->width, size->width, header_size,
                   data_offset, data_size, data_offset + data_size};
}

std::optional<Element> FindChild(const std::vector<std::uint8_t>& data,
                                 std::size_t begin,
                                 std::size_t end,
                                 std::uint64_t wanted_id) {
    std::size_t cursor = begin;
    while (cursor < end) {
        const auto element = ReadElement(data, cursor, end);
        if (!element || element->end <= cursor) {
            return std::nullopt;
        }
        if (element->id == wanted_id) {
            return element;
        }
        cursor = element->end;
    }
    return std::nullopt;
}

std::optional<std::uint64_t> ReadUnsigned(const std::vector<std::uint8_t>& data,
                                          const Element& element) {
    if (element.data_size == 0 || element.data_size > 8) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for (std::size_t i = element.data_offset; i < element.end; ++i) {
        value = (value << 8) | data[i];
    }
    return value;
}

bool WriteUnsigned(std::vector<std::uint8_t>& data,
                   const Element& element,
                   std::uint64_t value) {
    if (element.data_size == 0 || element.data_size > 8 ||
        (element.data_size < 8 && value >= (std::uint64_t{1} << (element.data_size * 8)))) {
        return false;
    }
    for (std::size_t i = 0; i < element.data_size; ++i) {
        data[element.end - 1 - i] = static_cast<std::uint8_t>(value & 0xFF);
        value >>= 8;
    }
    return true;
}

bool WriteElementSize(std::vector<std::uint8_t>& data,
                      const Element& element,
                      std::size_t value) {
    const std::size_t bits = element.size_width * 7;
    const std::uint64_t maximum = bits == 56
                                      ? (std::uint64_t{1} << 56) - 2
                                      : (std::uint64_t{1} << bits) - 2;
    if (value > maximum) {
        return false;
    }

    std::uint64_t encoded = value;
    for (std::size_t i = 0; i < element.size_width; ++i) {
        data[element.offset + element.id_width + element.size_width - 1 - i] =
            static_cast<std::uint8_t>(encoded & 0xFF);
        encoded >>= 8;
    }
    data[element.offset + element.id_width] |=
        static_cast<std::uint8_t>(1U << (8 - element.size_width));
    return true;
}

std::optional<std::uint64_t> ReadElementIdValue(
    const std::vector<std::uint8_t>& data, const Element& element) {
    if (element.data_size == 0 || element.data_size > 4) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for (std::size_t i = element.data_offset; i < element.end; ++i) {
        value = (value << 8) | data[i];
    }
    return value;
}

bool AdjustSeekHead(std::vector<std::uint8_t>& data,
                    const Element& segment,
                    const Element& void_element,
                    const Element& video) {
    const auto seek_head = FindChild(data, segment.data_offset, segment.end, kSeekHead);
    if (!seek_head) {
        return true;
    }

    std::size_t cursor = seek_head->data_offset;
    while (cursor < seek_head->end) {
        const auto seek = ReadElement(data, cursor, seek_head->end);
        if (!seek || seek->end <= cursor) {
            return false;
        }
        if (seek->id == kSeek) {
            const auto seek_id = FindChild(data, seek->data_offset, seek->end, kSeekId);
            const auto seek_position =
                FindChild(data, seek->data_offset, seek->end, kSeekPosition);
            if (!seek_id || !seek_position || !ReadElementIdValue(data, *seek_id)) {
                return false;
            }
            const auto position = ReadUnsigned(data, *seek_position);
            if (!position || *position > std::numeric_limits<std::size_t>::max() -
                                           segment.data_offset) {
                return false;
            }
            const std::size_t target = segment.data_offset +
                                       static_cast<std::size_t>(*position);
            if (target >= void_element.end && target < video.end) {
                if (*position < 5 || !WriteUnsigned(data, *seek_position, *position - 5)) {
                    return false;
                }
            }
        }
        cursor = seek->end;
    }
    return true;
}

bool IsAv1Track(const std::vector<std::uint8_t>& data, const Element& track) {
    const auto codec_id = FindChild(data, track.data_offset, track.end, kCodecId);
    constexpr std::string_view expected = "V_AV1";
    return codec_id && codec_id->data_size == expected.size() &&
           std::equal(expected.begin(), expected.end(), data.begin() + codec_id->data_offset);
}

Av1ContainerFixResult FixTrack(std::vector<std::uint8_t>& data,
                               const Element& segment,
                               const Element& tracks,
                               const Element& track,
                               const std::optional<Element>& void_element) {
    const auto video = FindChild(data, track.data_offset, track.end, kVideo);
    if (!video) {
        return {Av1ContainerFixStatus::Invalid, "AV1 track has no Video element"};
    }

    const auto height = FindChild(data, video->data_offset, video->end, kPixelHeight);
    if (!height) {
        return {Av1ContainerFixStatus::Invalid, "AV1 track has no PixelHeight"};
    }
    const auto height_value = ReadUnsigned(data, *height);
    if (!height_value) {
        return {Av1ContainerFixStatus::Invalid, "invalid PixelHeight"};
    }

    const auto crop = FindChild(data, video->data_offset, video->end, kPixelCropBottom);
    if (crop) {
        const auto crop_value = ReadUnsigned(data, *crop);
        const auto display_height =
            FindChild(data, video->data_offset, video->end, kDisplayHeight);
        const auto display_height_value = display_height
                                              ? ReadUnsigned(data, *display_height)
                                              : std::nullopt;
        if (height_value == 1082 && crop_value && *crop_value == 2 &&
            display_height_value && *display_height_value == 1080) {
            return {Av1ContainerFixStatus::AlreadyFixed, "AV1 crop metadata already present"};
        }
        return {Av1ContainerFixStatus::Unsupported,
                "AV1 track already has different crop metadata"};
    }

    if (*height_value != 1080 || height->data_size != 2) {
        return {Av1ContainerFixStatus::Unsupported,
                "only the AMF 1920x1080 to 1920x1082 case is supported"};
    }

    const auto display_unit = FindChild(data, video->data_offset, video->end, kDisplayUnit);
    if (!display_unit || display_unit->header_size != 3 || display_unit->data_size != 1 ||
        ReadUnsigned(data, *display_unit) != 4) {
        return {Av1ContainerFixStatus::Unsupported,
                "no replaceable Resolve DisplayUnit element found"};
    }

    if (!void_element || void_element->end > tracks.offset || void_element->data_size < 5) {
        return {Av1ContainerFixStatus::Unsupported,
                "no usable Matroska Void before Tracks"};
    }

    if (!AdjustSeekHead(data, segment, *void_element, *video) ||
        !WriteElementSize(data, *void_element, void_element->data_size - 5) ||
        !WriteElementSize(data, tracks, tracks.data_size + 5) ||
        !WriteElementSize(data, track, track.data_size + 5) ||
        !WriteElementSize(data, *video, video->data_size + 5)) {
        return {Av1ContainerFixStatus::Unsupported,
                "Matroska element sizes cannot hold crop metadata"};
    }

    // Reuse DisplayUnit for crop. Five bytes taken from an earlier Void add
    // DisplayHeight while preserving all Cluster and Cues offsets.
    data[height->data_offset] = 0x04;
    data[height->data_offset + 1] = 0x3A;
    const std::uint8_t replacement[] = {0x54, 0xAA, 0x81, 0x02};
    std::copy(std::begin(replacement), std::end(replacement),
              data.begin() + display_unit->offset);

    data.erase(data.begin() + static_cast<std::ptrdiff_t>(void_element->end - 5),
               data.begin() + static_cast<std::ptrdiff_t>(void_element->end));
    const std::uint8_t display_height[] = {0x54, 0xBA, 0x82, 0x04, 0x38};
    const std::size_t insertion = video->end - 5;
    data.insert(data.begin() + static_cast<std::ptrdiff_t>(insertion),
                std::begin(display_height), std::end(display_height));

    return {Av1ContainerFixStatus::Fixed,
            "set coded height=1082, crop bottom=2, and display height=1080"};
}

}  // namespace

Av1ContainerFixResult FixAv1Mkv1080(std::vector<std::uint8_t>& data) {
    if (data.size() < 16) {
        return {Av1ContainerFixStatus::Invalid, "file is too small"};
    }

    const auto ebml = ReadElement(data, 0, data.size());
    if (!ebml || ebml->id != kEbml) {
        return {Av1ContainerFixStatus::Invalid, "not an EBML/Matroska file"};
    }

    const auto segment = ReadElement(data, ebml->end, data.size());
    if (!segment || segment->id != kSegment) {
        return {Av1ContainerFixStatus::Invalid, "Matroska Segment not found"};
    }
    const auto tracks = FindChild(data, segment->data_offset, segment->end, kTracks);
    if (!tracks) {
        return {Av1ContainerFixStatus::Invalid, "Matroska Tracks not found"};
    }

    std::optional<Element> void_element;
    std::size_t top_cursor = segment->data_offset;
    while (top_cursor < tracks->offset) {
        const auto element = ReadElement(data, top_cursor, segment->end);
        if (!element || element->end <= top_cursor) {
            return {Av1ContainerFixStatus::Invalid, "invalid Matroska Segment data"};
        }
        if (element->id == kVoid && element->end <= tracks->offset &&
            element->data_size >= 5) {
            void_element = element;
        }
        top_cursor = element->end;
    }
    std::size_t cursor = tracks->data_offset;
    while (cursor < tracks->end) {
        const auto element = ReadElement(data, cursor, tracks->end);
        if (!element || element->end <= cursor) {
            return {Av1ContainerFixStatus::Invalid, "invalid Matroska Tracks data"};
        }
        if (element->id == kTrackEntry && IsAv1Track(data, *element)) {
            return FixTrack(data, *segment, *tracks, *element, void_element);
        }
        cursor = element->end;
    }

    return {Av1ContainerFixStatus::Unsupported, "AV1 video track not found"};
}

}  // namespace amf_plugin
