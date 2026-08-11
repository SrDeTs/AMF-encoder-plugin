# AMF Encoder Plugin for DaVinci Resolve

DaVinci Resolve video plugin built on the official Blackmagic Codec Plugin SDK and AMD Advanced Media Framework (AMF).

Portuguese version: [README.md](README.md)

## Encoders

- H.264 AMF, 8-bit 4:2:0
- AV1 AMF, 8-bit and 10-bit 4:2:0

HEVC/H.265 AMF is not registered by this plugin.

## Backend

The plugin links directly to `libamfrt64.so.1`. AMD AMF creates its own Linux graphics context; the plugin does not link Vulkan, FFmpeg, `libavcodec`, `libavutil`, NVENC, x264, x265, or a CPU fallback.

Official AMD AMF MIT headers live in `third_party/AMF`; the preserved license is in `third_party/AMF-MIT-LICENSE.txt`.

## Requirements

- Linux x86-64
- DaVinci Resolve
- AMD GPU supporting the selected codec
- AMD AMF runtime: `libamfrt64.so.1`
- working AMD driver with AMF support
- CMake and a C++20 compiler

## Build

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
```

The generated plugin is `build/amf_encoder_plugin.dvcp`.

## Limits

- actual codec availability depends on the AMD GPU and driver
- AMF errors stop the render with the runtime error; there is no silent fallback
- MP4, MOV, and MKV are Resolve container choices; AMF produces encoded video bitstream
