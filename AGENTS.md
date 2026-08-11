# Repository Guidelines

## Project Structure & Module Organization

The plugin is a C++20 DaVinci Resolve Codec Plugin that calls AMD AMF directly.
Codec registration lives in `h264_amf_encoder.*`, `h265_amf_encoder.*`, and
`av1_amf_encoder.*`. Shared encoding behavior is implemented in
`amf_encoder.*`, while `amf_settings.*` defines Resolve-facing controls.
HEVC bitstream handling and RGB16-to-P010 conversion are isolated in
`hevc_config.*` and `rgb16_to_p010.*`. Blackmagic API declarations are under
`include/`; the local host wrapper is under `wrapper/`; AMF headers are under
`third_party/AMF/`. Tests live in `tests/`. Treat `build/` as generated output.

## Build, Test, and Development Commands

```bash
cmake -S . -B build
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Configuration defaults to `Release`, requires CMake 3.20+, and locates the
system `libamfrt64.so.1`. The build produces `build/amf_encoder_plugin.dvcp`
and a complete bundle under `build/amf_encoder_plugin.dvcp.bundle/`.
Use `git diff --check` before submitting changes to catch whitespace errors.

## Coding Style & Naming Conventions

Follow the existing C++ style: four-space indentation, braces on the same line,
and short comments only where behavior is non-obvious. Use `snake_case` for
files and free functions, `PascalCase` for types, and existing AMF/Blackmagic
naming when wrapping SDK symbols. Keep codec-specific policy in its codec
module and reusable transformations in focused helpers. The compiler enforces
`-Wall -Wextra -Wpedantic`; no separate formatter is configured.

## Testing Guidelines

Tests are standalone C++ executables registered with CTest. Name files
`tests/<feature>_test.cpp` and test targets `<feature>_test`. Add focused tests
for packet conversion, color conversion, and configuration parsing. Passing
CTest proves local logic only: codec/container changes must also be rendered in
DaVinci Resolve and inspected for video, audio, color, and bit depth before they
are documented as functional.

## Commit & Pull Request Guidelines

Recent history uses short imperative summaries such as `H265 fix` and
`Prepare 0.1.0 release`. Prefer a concise subject that names the affected codec
or component. Pull requests should explain behavior changes, list tested
codec/container/bit-depth combinations, include the build and CTest results,
and attach Resolve logs or screenshots when fixing runtime failures.

## Dependency & Licensing Constraints

Do not add FFmpeg linkage or copy GPL-licensed implementation code. Preserve
the direct AMF architecture and dynamic use of the system AMF runtime. Never
commit generated binaries, Resolve logs, rendered media, or machine-specific
installation paths.
