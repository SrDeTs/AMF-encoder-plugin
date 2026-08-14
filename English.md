# AMF Encoder Plugin for DaVinci Resolve

AMD hardware video encoding plugin for DaVinci Resolve on Linux. The
implementation uses Blackmagic Design's official Codec Plugin API and accesses
AMD Advanced Media Framework (AMF) directly, without using FFmpeg as a backend.

## Current status

| Resolve codec | Pixel path | Advertised containers |
| --- | --- | --- |
| H.264 AMF 8-bit 4:2:0 | NV12 to NV12 | MP4, MOV, MKV |
| H.265 AMF 8-bit 4:2:0 | UYVY 4:2:2 to NV12 4:2:0 | MP4, MOV |
| H.265 AMF 10-bit 4:2:0 | RGB16 or 10-bit YUV to P010 | MP4, MOV |
| AV1 AMF 8-bit 4:2:0 | NV12 to NV12 | MP4, MOV, MKV |
| AV1 AMF 10-bit 4:2:0 | 10-bit input to P010 | MP4, MOV, MKV |

This is a video-only plugin. AAC, FLAC, and PCM audio are handled by Resolve or
by a separate audio plugin.

## Architecture

The plugin uses AMD AMF directly. It initializes an AMF Vulkan context and
sends NV12 or P010 surfaces to the GPU encoder. For HEVC, the plugin also builds
the `hvcC` record and converts packets to the format expected by MP4/QuickTime
muxers.

The plugin does not use or link FFmpeg.

## License

Original code by SrDeTs uses a permissive MIT-based license with third-party
exceptions. See [LICENSE](LICENSE) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

AMD AMF headers used during compilation retain their MIT notices and are
obtained from the system. Files originating from or derived from the Blackmagic
Design SDK remain subject to the applicable SDK terms. The project does not
incorporate or use GPL-licensed FFmpeg code.

## Requirements

- Linux x86-64
- DaVinci Resolve Studio
- AMD GPU with hardware support for the selected codec
- working AMD/Mesa driver
- AMD AMF runtime providing `libamfrt64.so.1`
- `amf-headers` development package
- CMake 3.20 or newer
- C++20 compiler

On Arch Linux, CachyOS, and derivatives, install the AMF runtime from the AUR:

```bash
sudo pacman -S amf-headers
yay -S amf-amdgpu-pro
sudo ldconfig
```

The `amf-headers` package is required to build the plugin. The
`amf-amdgpu-pro` package provides the runtime required to load and use the
plugin in Resolve. Neither package replaces the other.

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
4. Check `~/.local/share/DaVinciResolve/logs/ResolveDebug.txt` for a plugin loading failure.

## Limitations

- No CPU fallback exists.
- HEVC/H.265 appears with the MP4 and MOV containers.
- HEVC/H.265 in MKV is unavailable.
- H.264 10-bit is unavailable.
- Actual HEVC, AV1, and P010 support depends on the AMD hardware and runtime.
- The plugin encodes video and cannot control Resolve audio or muxer bugs.

## Support the project

Development can be supported through [SrDeTs on Ko-fi](https://ko-fi.com/srdets).

Donations are entirely voluntary. They do not constitute a purchase and do not
grant an additional license, warranty, support, development priority, or
exclusive access to features. Use and availability of the project do not
depend on a donation.
