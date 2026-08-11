# AMF Encoder Plugin para DaVinci Resolve

Plugin de codificacao de video por hardware AMD para DaVinci Resolve no Linux.
A implementacao usa a API oficial Codec Plugin da Blackmagic Design e acessa o
AMD Advanced Media Framework (AMF) diretamente, sem usar FFmpeg como backend.

Versao em ingles: [English.md](English.md)

## Estado atual

| Codec | Formato de entrada | Profundidade | Aceleracao |
| --- | --- | --- | --- |
| H.264/AVC | NV12 4:2:0 | 8-bit | AMD AMF |
| AV1 | NV12 4:2:0 | 8-bit | AMD AMF |
| AV1 | P010 4:2:0 | 10-bit | AMD AMF |

HEVC/H.265 nao e registrado e nao aparece no Resolve.

O plugin anuncia MP4, MOV e MKV ao Resolve. A disponibilidade final de cada
combinacao ainda depende do muxer do Resolve. Por exemplo, o Resolve pode nao
oferecer AV1 em MOV mesmo que o codec esteja instalado.

Este e somente um plugin de video. Audio AAC, FLAC ou PCM e tratado pelo
Resolve ou por outro plugin de audio.

## Arquitetura

O plugin usa AMD AMF diretamente. No Linux, o proprio runtime cria o contexto
grafico necessario para acessar o encoder da GPU.

O binario nao linka:

- FFmpeg.

O projeto nao contem uma licenca GPL e nao incorpora FFmpeg. Este repositorio
tambem nao concede automaticamente uma licenca geral para arquivos que estejam
sujeitos aos termos do SDK da Blackmagic Design.

## Requisitos

- Linux x86-64
- DaVinci Resolve Studio
- GPU AMD com suporte de hardware ao codec selecionado
- driver AMD/Mesa funcional
- runtime AMD AMF fornecendo `libamfrt64.so.1`
- CMake 3.20 ou mais recente
- compilador com C++20

Use um pacote compativel com a distribuicao que forneca o
runtime AMF.

Confirme que o runtime esta visivel:

```bash
ldconfig -p | grep libamfrt64.so.1
```

Resultado esperado inclui um caminho como:

```text
libamfrt64.so.1 => /usr/lib/libamfrt64.so.1
```

## Compilacao

```bash
cmake -S . -B build
cmake --build build -j"$(nproc)"
```

Artefato principal:

```text
build/amf_encoder_plugin.dvcp
```

O build tambem gera a estrutura:

```text
build/amf_encoder_plugin.dvcp.bundle/Contents/Linux-x86-64/amf_encoder_plugin.dvcp
```

## Instalacao

Crie o bundle e instale o binario:

```bash
sudo install -d /opt/resolve/IOPlugins/amf_encoder_plugin.dvcp.bundle/Contents/Linux-x86-64
sudo install -m 755 build/amf_encoder_plugin.dvcp \
  /opt/resolve/IOPlugins/amf_encoder_plugin.dvcp.bundle/Contents/Linux-x86-64/amf_encoder_plugin.dvcp
```

### Remocao

```bash
sudo rm -rf /opt/resolve/IOPlugins/amf_encoder_plugin.dvcp.bundle
```

## Configuracoes expostas

### Preset

H.264:

- High Quality
- Quality
- Balanced
- Speed

AV1:

- High Quality
- Quality
- Balanced
- Speed

### Controle de taxa

- Constant QP: usa QP no H.264 e Q Index no AV1
- Variable Bitrate: usa bitrate alvo, bitrate maximo e tamanho do buffer
- Constant Bitrate: usa bitrate alvo e tamanho do buffer

Valores menores de QP/Q Index produzem maior qualidade e arquivos maiores.

- H.264 QP: 0 a 51
- AV1 Q Index: 1 a 255

Bitrate e tamanho de buffer sao mostrados apenas quando aplicaveis ao modo de
controle de taxa escolhido.

### Usage

- Transcoding
- Low Latency
- Ultra Low Latency
- Webcam
- High Quality
- Low Latency High Quality

Os valores sao enums nativos AMF. Algumas combinacoes podem depender da GPU,
da versao do runtime e do driver.

## Comportamento no Linux

O plugin define `DISABLE_LSFG=1` antes de inicializar AMF. Isso evita que a
camada LSFG interna do runtime interfira na criacao do contexto Vulkan.

O aviso abaixo vem do Mesa e nao indica falha de encode:

```text
radv: RADV_PERFTEST=video_decode is deprecated
```

### Encoder nao aparece

1. Confirme o caminho e permissao do `.dvcp`.
2. Confirme `libamfrt64.so.1` com `ldconfig` e `ldd`.
3. Reinicie completamente o Resolve.
4. Verifique `~/.local/share/DaVinciResolve/logs/ResolveDebug.txt` por falha de carregamento do plugin.

## Limitacoes

- Nao existe fallback por CPU.
- HEVC/H.265 nao esta disponivel.
- H.264 10-bit nao esta disponivel.
- Suporte real a AV1 e P010 depende do hardware e runtime AMD.
- O plugin codifica video; ele nao controla bugs de audio ou do muxer do
  Resolve.
