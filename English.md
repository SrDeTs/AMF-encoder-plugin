# AMF Encoder Plugin for DaVinci Resolve

## WARNING: THIS WAS MADE USING VIBE CODING.

Fork of [`EdvinNilsson/ffmpeg_encoder_plugin`](https://github.com/EdvinNilsson/ffmpeg_encoder_plugin) focused on **AMF Video via FFmpeg**.

Portuguese version: [README.md](README.md)

Current repository state:
- **AMF-only** backend
- no CPU encoders
- no NVENC encoders
- no automatic fallback

Exposed encoders:
- `h264_amf`
- `av1_amf`

## Goal

Use FFmpeg's AMF backend as a real backend inside DaVinci Resolve.

If AMF fails:
- the plugin returns a clear error
- the encoder stops
- it does not switch to another backend

## Requirements

- Linux
- DaVinci Resolve
- FFmpeg with AMF support
- driver with real support for the target codec
- AMF Video Encode for `h264_amf` and `av1_amf`
- CMake
- a C++ compiler

## FFmpeg checks

Check whether AMF is available:

```bash
ffmpeg -hide_banner -hwaccels | grep -i amf
```

Check whether AMF encoders exist:

```bash
ffmpeg -hide_banner -encoders | grep -i amf
```

Inspect encoder options:

```bash
ffmpeg -hide_banner -h encoder=h264_amf
ffmpeg -hide_banner -h encoder=av1_amf
```

## Build

```bash
mkdir -p build
cd build
cmake ..
make -j$(nproc)
```

Main artifact:

```text
build/amf_encoder_plugin.dvcp
```

Linux bundle:

```text
build/amf_encoder_plugin.dvcp.bundle/Contents/Linux-x86-64/
```

## Installation

Copy the plugin to Resolve's plugin directory.

Example:

```bash
cp -av build/amf_encoder_plugin.dvcp \
  /opt/resolve/IOPlugins/amf_encoder_plugin.dvcp.bundle/Contents/Linux-x86-64/
```

If you want the full bundle:

```bash
cp -av build/amf_encoder_plugin.dvcp.bundle/Contents/Linux-x86-64/* \
  /opt/resolve/IOPlugins/amf_encoder_plugin.dvcp.bundle/Contents/Linux-x86-64/
```

## Encoders exposed in Resolve

Once loaded, Resolve exposes:

- `H.264` -> `AMF 8-bit 4:2:0 (FFmpeg)`
- `AV1` -> `AMF 8-bit 4:2:0 (FFmpeg)`, `AMF 10-bit 4:2:0 (FFmpeg)`

## AMF plugin options

The plugin exposes AMF controls such as:

- encoder preset
- quality mode (`CQP`, `VBR`, `CBR`)
- `Factor`
- `Bit Rate`
- `Max Bit Rate`
- `Buffer Size`
- `Async Depth`
- `Usage`

Notes:
- `Async Depth` is limited in the UI to `1..42`
- `Usage` varies by codec; the plugin uses the FFmpeg AMF option names

## Manual FFmpeg test

Before blaming the plugin, test the encoder directly in FFmpeg.

Example for H.264:

```bash
ffmpeg -hide_banner \
  -f lavfi -i testsrc2=size=1280x720:rate=30 \
  -t 10 \
  -vf "format=nv12" \
  -c:v h264_amf \
  -rc cqp \
  -qp_i 20 \
  -qp_p 20 \
  -preset balanced \
  -usage transcoding \
  out.mp4
```

If this fails, the problem is likely in:
- FFmpeg
- the driver
- actual GPU codec support

## Known limitations

- AMF Video support still varies by driver and GPU
- a system may have AMF available but still not support encode for every codec
- `H.265/HEVC AMF` has been removed from the Resolve-facing codec list in this plugin
- `preset`, `usage`, and `async_depth` may have driver-specific limits
- aggressive presets may become unstable depending on the AMF stack

## No fallback

Expected behavior:

- select AMF -> use AMF
- AMF fails -> explicit error
- no silent fallback

## Credits

Original project:

- **EdvinNilsson**
- https://github.com/EdvinNilsson/ffmpeg_encoder_plugin

## License

See [LICENSE](LICENSE).
