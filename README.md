# AMF Encoder Plugin Para DaVinci Resolve

## AVISO: ISSO FOI FEITO POR MEIO DE VIBE CODING.

Fork do projeto [`EdvinNilsson/ffmpeg_encoder_plugin`](https://github.com/EdvinNilsson/ffmpeg_encoder_plugin) focado em **AMF Video via FFmpeg**.

English version: [English.md](English.md)

Estado atual do repositório:
- só backend **AMF**
- sem CPU
- sem NVENC
- sem fallback automático

Encoders expostos:
- `h264_amf`
- `av1_amf`

## Objetivo

Usar o backend AMF do FFmpeg como backend próprio dentro do DaVinci Resolve.

Se AMF falhar:
- o plugin retorna erro claro
- o encoder para
- não troca para outro backend

## Requisitos

- Linux
- DaVinci Resolve
- FFmpeg com suporte a AMF
- driver com suporte real ao codec desejado
- AMF Video Encode para `h264_amf` e `av1_amf`
- CMake
- compilador C++

## Verificação do FFmpeg

Cheque se AMF existe:

```bash
ffmpeg -hide_banner -hwaccels | grep -i amf
```

Cheque se os encoders existem:

```bash
ffmpeg -hide_banner -encoders | grep -i amf
```

Cheque as opções de cada encoder:

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

Artefato principal:

```text
build/amf_encoder_plugin.dvcp
```

Bundle Linux:

```text
build/amf_encoder_plugin.dvcp.bundle/Contents/Linux-x86-64/
```

## Instalação

Copie o plugin para o diretório de plugins do Resolve.

Exemplo:

```bash
cp -av build/amf_encoder_plugin.dvcp \
  /opt/resolve/IOPlugins/amf_encoder_plugin.dvcp.bundle/Contents/Linux-x86-64/
```

Se você estiver usando o bundle completo:

```bash
cp -av build/amf_encoder_plugin.dvcp.bundle/Contents/Linux-x86-64/* \
  /opt/resolve/IOPlugins/amf_encoder_plugin.dvcp.bundle/Contents/Linux-x86-64/
```

## Encoders disponíveis no Resolve

Quando o plugin estiver carregado, o Resolve expõe:

- `H.264` -> `AMF 8-bit 4:2:0 (FFmpeg)`
- `AV1` -> `AMF 8-bit 4:2:0 (FFmpeg)`, `AMF 10-bit 4:2:0 (FFmpeg)`

## Opções AMF do plugin

O plugin expõe opções de controle para o backend AMF, incluindo:

- preset do encoder
- modo de qualidade (`CQP`, `VBR`, `CBR`)
- `Factor`
- `Bit Rate`
- `Max Bit Rate`
- `Buffer Size`
- `Async Depth`
- `Usage`

Observações:
- `Async Depth` está limitado na UI a `1..42`
- `Usage` muda por codec; o plugin usa os nomes do FFmpeg AMF

## Teste manual no FFmpeg

Antes de culpar o plugin, teste o encoder direto no FFmpeg.

Exemplo H.264:

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

Se isso falhar, o problema tende a estar em:
- FFmpeg
- driver
- suporte real da GPU ao codec

## Limitações conhecidas

- suporte AMF Video ainda varia por driver e GPU
- um sistema pode ter AMF disponível e mesmo assim não suportar encode para todos os codecs
- `H.265/HEVC AMF` foi removido da oferta do Resolve neste plugin
- `preset`, `usage` e `async_depth` podem ter limites diferentes por driver
- presets agressivos podem causar instabilidade dependendo do stack AMF

## Sem fallback

O comportamento esperado é:

- selecionou AMF -> usa AMF
- AMF falhou -> erro explícito
- sem fallback silencioso

## Créditos

Crédito da base original:

- **EdvinNilsson**
- https://github.com/EdvinNilsson/ffmpeg_encoder_plugin

## Licença

Consulte [LICENSE](LICENSE).
