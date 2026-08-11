# AMF Encoder Plugin for DaVinci Resolve

AMD hardware video encoding plugin for DaVinci Resolve on Linux. The
implementation uses Blackmagic Design's official Codec Plugin API and accesses
AMD Advanced Media Framework (AMF) directly, without using FFmpeg as a backend.

Portuguese version: [README.md](README.md)

## Current status

| Resolve codec | Pixel path | Advertised containers |
| --- | --- | --- |
| H.264 AMF 8-bit 4:2:0 | NV12 to NV12 | MP4, MOV, MKV |
| H.265 AMF 8-bit 4:2:0 | UYVY 4:2:2 to NV12 4:2:0 | MP4, MOV |
| H.265 AMF 10-bit 4:2:0 | RGB16 or 10-bit YUV to P010 | MP4, MOV |
| AV1 AMF 8-bit 4:2:0 | NV12 to NV12 | MP4, MOV, MKV |
| AV1 AMF 10-bit 4:2:0 | 10-bit input to P010 | MP4, MOV, MKV |

The plugin advertises the list above. Final availability of each combination
depends on Resolve's muxer; for example, Resolve may hide AV1 in MOV. HEVC in
MKV remains disabled because that path has not been validated.

HEVC Main 8-bit and Main 10 have been validated with MP4 and MOV.

This is a video-only plugin. AAC, FLAC, and PCM audio are handled by Resolve or
by a separate audio plugin.

## Architecture

The plugin uses AMD AMF directly. It initializes an AMF Vulkan context and
sends NV12 or P010 surfaces to the GPU encoder. For HEVC, the plugin also builds
the `hvcC` record and converts packets to the format expected by MP4/QuickTime
muxers.

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
- AMF headers included under `third_party/AMF`
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

All codecs offer High Quality, Quality, Balanced, and Speed. The default is
Balanced for H.264/HEVC and Quality for AV1.

### Rate control

- Constant QP: uses QP for H.264/HEVC and Q Index for AV1
- Variable Bitrate: uses target bitrate, maximum bitrate, and buffer size
- Constant Bitrate: uses target bitrate and buffer size

The default is Constant QP: 20 for H.264, 22 for HEVC, and 100 for AV1. Lower
values produce higher quality and larger files.

- H.264/HEVC QP: 0 to 51
- AV1 Q Index: 1 to 255
- target and maximum bitrate: 100 to 100000 kb/s; default 6000 kb/s
- buffer: 100 to 200000 kbit; default 12000 kbit

Bitrate and buffer size are shown only when applicable to the selected rate
control mode.

### Usage and reset

H.264/HEVC use the native order: Transcoding, Ultra Low Latency, Low Latency,
Webcam, High Quality, and Low Latency High Quality. AV1 swaps the Low Latency
and Ultra Low Latency positions. The default is Transcoding.

The values are native AMF enums. Some combinations can depend on the GPU,
runtime version, and driver. Reset restores all defaults. The plugin does not
expose Async Depth; submission and draining are managed internally.

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
- HEVC/H.265 appears with the MP4 and MOV containers.
- HEVC/H.265 in MKV is unavailable.
- H.264 10-bit is unavailable.
- Actual HEVC, AV1, and P010 support depends on the AMD hardware and runtime.
- The plugin encodes video and cannot control Resolve audio or muxer bugs.

## Contributing

Read [AGENTS.md](AGENTS.md) before changing the code. It documents the module
layout, build and test commands, coding conventions, and DaVinci Resolve
validation criteria.

Codec, pixel-format, or container changes require local tests and an actual
Resolve render. The project uses AMF directly; do not add FFmpeg as a dependency
or incorporate GPL code.
