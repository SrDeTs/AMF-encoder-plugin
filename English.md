# AMF Encoder Plugin for DaVinci Resolve

AMD hardware video encoding plugin for DaVinci Resolve on Linux. The
implementation uses Blackmagic Design's official Codec Plugin API and accesses
AMD Advanced Media Framework (AMF) directly, without using FFmpeg as a backend.

## Current status

| Codec | Input format | Bit depth | Acceleration |
| --- | --- | --- | --- |
| H.264/AVC | NV12 4:2:0 | 8-bit | AMD AMF |
| HEVC/H.265 Main | UYVY 4:2:2 | 8-bit | AMD AMF |
| HEVC/H.265 Main 10 | RGB16 converted to P010 4:2:0 | 10-bit | AMD AMF |
| AV1 | NV12 4:2:0 | 8-bit | AMD AMF |
| AV1 | P010 4:2:0 | 10-bit | AMD AMF |

H.264 and AV1 advertise MP4, MOV, and MKV. HEVC advertises MP4 only. MOV and
MKV remain hidden for HEVC until their muxing path is validated. Final
availability of the other combinations still depends on Resolve's muxer. For
example, Resolve may not offer AV1 in MOV even when the codec is installed.

HEVC Main 8-bit and Main 10 have been validated with MP4.

This is a video-only plugin. AAC, FLAC, and PCM audio are handled by Resolve or
by a separate audio plugin.

## Architecture

The plugin uses AMD AMF directly. On Linux, the AMF runtime creates the
graphics context required to access the GPU encoder.

The plugin does not use or link FFmpeg.

The project does not include a GPL license and does not incorporate FFmpeg.
This repository also does not automatically grant a general license for files
that are subject to the Blackmagic Design SDK terms.

## Requirements

- Linux x86-64
- DaVinci Resolve Studio
- AMD GPU with hardware support for the selected codec
- working AMD/Mesa driver
- AMD AMF runtime providing `libamfrt64.so.1`
- CMake 3.20 or newer
- C++20 compiler

Use a package compatible with your distribution that provides the AMF runtime.

Confirm that the runtime is visible:

```bash
ldconfig -p | grep libamfrt64.so.1
```

The output should include a path similar to:

```text
libamfrt64.so.1 => /usr/lib/libamfrt64.so.1
```

## Build

```bash
cmake -S . -B build
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Main artifact:

```text
build/amf_encoder_plugin.dvcp
```

The build also generates:

```text
build/amf_encoder_plugin.dvcp.bundle/Contents/Linux-x86-64/amf_encoder_plugin.dvcp
```

## Installation

Create the bundle and install the binary:

```bash
sudo install -d /opt/resolve/IOPlugins/amf_encoder_plugin.dvcp.bundle/Contents/Linux-x86-64
sudo install -m 755 build/amf_encoder_plugin.dvcp \
  /opt/resolve/IOPlugins/amf_encoder_plugin.dvcp.bundle/Contents/Linux-x86-64/amf_encoder_plugin.dvcp
```

### Removal

```bash
sudo rm -rf /opt/resolve/IOPlugins/amf_encoder_plugin.dvcp.bundle
```

## Exposed settings

### Preset

H.264 and HEVC:

- High Quality
- Quality
- Balanced
- Speed

AV1:

- High Quality
- Quality
- Balanced
- Speed

### Rate control

- Constant QP: uses QP for H.264/HEVC and Q Index for AV1
- Variable Bitrate: uses target bitrate, maximum bitrate, and buffer size
- Constant Bitrate: uses target bitrate and buffer size

Lower QP/Q Index values produce higher quality and larger files.

- H.264/HEVC QP: 0 to 51
- AV1 Q Index: 1 to 255

Bitrate and buffer size are shown only when applicable to the selected rate
control mode.

### Usage

- Transcoding
- Low Latency
- Ultra Low Latency
- Webcam
- High Quality
- Low Latency High Quality

The values are native AMF enums. Some combinations can depend on the GPU,
runtime version, and driver.

## Linux behavior

The plugin defines `DISABLE_LSFG=1` before initializing AMF. This prevents the
runtime's internal LSFG layer from interfering with Vulkan context creation.

The warning below is emitted by Mesa and does not indicate an encoding failure:

```text
radv: RADV_PERFTEST=video_decode is deprecated
```

### Encoder does not appear

1. Confirm the `.dvcp` path and permissions.
2. Confirm `libamfrt64.so.1` with `ldconfig` and `ldd`.
3. Fully restart Resolve.
4. Check `~/.local/share/DaVinciResolve/logs/ResolveDebug.txt` for a plugin
   loading failure.

## Limitations

- No CPU fallback exists.
- HEVC/H.265 appears only with the MP4 container.
- HEVC/H.265 in MOV and MKV is unavailable.
- H.264 10-bit is unavailable.
- Actual HEVC, AV1, and P010 support depends on the AMD hardware and runtime.
- The plugin encodes video and cannot control Resolve audio or muxer bugs.
