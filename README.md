# AMF Encoder Plugin para DaVinci Resolve

Plugin de codificacao de video por hardware AMD para DaVinci Resolve no Linux.
A implementacao usa a API oficial Codec Plugin da Blackmagic Design e acessa o
AMD Advanced Media Framework (AMF) diretamente, sem usar FFmpeg como backend.

Versao em ingles: [English.md](English.md)

## Estado atual

| Codec no Resolve | Caminho de pixels | Containers anunciados |
| --- | --- | --- |
| H.264 AMF 8-bit 4:2:0 | NV12 para NV12 | MP4, MOV, MKV |
| H.265 AMF 8-bit 4:2:0 | UYVY 4:2:2 para NV12 4:2:0 | MP4, MOV |
| H.265 AMF 10-bit 4:2:0 | RGB16 ou YUV 10-bit para P010 | MP4, MOV |
| AV1 AMF 8-bit 4:2:0 | NV12 para NV12 | MP4, MOV, MKV |
| AV1 AMF 10-bit 4:2:0 | entrada 10-bit para P010 | MP4, MOV, MKV |

A lista acima e anunciada pelo plugin. A exibicao final de cada combinacao
depende do muxer do Resolve; por exemplo, o Resolve pode ocultar AV1 em MOV.
HEVC em MKV permanece desativado porque esse caminho ainda nao foi validado.

HEVC Main 8-bit e Main 10 foram validados com MP4 e MOV.

Este e somente um plugin de video. Audio AAC, FLAC ou PCM e tratado pelo
Resolve ou por outro plugin de audio.

## Arquitetura

O plugin usa AMD AMF diretamente. Ele inicializa um contexto AMF Vulkan e envia
surfaces NV12 ou P010 ao encoder da GPU. No HEVC, o plugin tambem gera o registro
`hvcC` e converte os pacotes para o formato esperado pelos muxers MP4/QuickTime.

O plugin nao usa nem linka FFmpeg.

O projeto nao contem uma licenca GPL e nao incorpora FFmpeg. Este repositorio
tambem nao concede automaticamente uma licenca geral para arquivos que estejam
sujeitos aos termos do SDK da Blackmagic Design.

## Requisitos

- Linux x86-64
- DaVinci Resolve Studio
- GPU AMD com suporte de hardware ao codec selecionado
- driver AMD/Mesa funcional
- runtime AMD AMF fornecendo `libamfrt64.so.1`
- headers AMF incluidos em `third_party/AMF`
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
ctest --test-dir build --output-on-failure
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

Todos os codecs oferecem High Quality, Quality, Balanced e Speed. O padrao e
Balanced para H.264/HEVC e Quality para AV1.

### Controle de taxa

- Constant QP: usa QP no H.264/HEVC e Q Index no AV1
- Variable Bitrate: usa bitrate alvo, bitrate maximo e tamanho do buffer
- Constant Bitrate: usa bitrate alvo e tamanho do buffer

O padrao e Constant QP: 20 no H.264, 22 no HEVC e 100 no AV1. Valores menores
produzem maior qualidade e arquivos maiores.

- H.264/HEVC QP: 0 a 51
- AV1 Q Index: 1 a 255
- bitrate alvo e maximo: 100 a 100000 kb/s; padrao 6000 kb/s
- buffer: 100 a 200000 kbit; padrao 12000 kbit

Bitrate e tamanho de buffer sao mostrados apenas quando aplicaveis ao modo de
controle de taxa escolhido.

### Usage e reset

H.264/HEVC seguem a ordem nativa: Transcoding, Ultra Low Latency, Low Latency,
Webcam, High Quality e Low Latency High Quality. AV1 troca a ordem de Low
Latency e Ultra Low Latency. O padrao e Transcoding.

Os valores sao enums nativos AMF. Algumas combinacoes podem depender da GPU,
da versao do runtime e do driver. O botao Reset restaura todos os padroes. O
plugin nao expoe Async Depth; o fluxo de envio e drenagem e gerenciado
internamente.

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
- HEVC/H.265 aparece com os containers MP4 e MOV.
- HEVC/H.265 em MKV nao esta disponivel.
- H.264 10-bit nao esta disponivel.
- Suporte real a HEVC, AV1 e P010 depende do hardware e runtime AMD.
- O plugin codifica video; ele nao controla bugs de audio ou do muxer do
  Resolve.

## Contribuicao

Antes de alterar o codigo, consulte [AGENTS.md](AGENTS.md). O guia descreve a
estrutura dos modulos, comandos de build e teste, convencoes de codigo e os
criterios de validacao no DaVinci Resolve.

Mudancas de codec, formato de pixel ou container devem incluir testes locais e
um render real no Resolve. O projeto usa AMF diretamente; nao adicione FFmpeg
como dependencia nem incorpore codigo GPL.
