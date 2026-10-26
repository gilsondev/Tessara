# Tessera Logo Kit — v1

Reconstrução vetorial inspirada na referência de marca fornecida (não é tracing perfeito do raster original). Nome oficial: **Tessera**, sem acento.

## Arquivos
- `tessera_symbol_light.svg`: símbolo colorido para fundos claros.
- `tessera_symbol_dark.svg`: símbolo colorido para fundos escuros.
- `tessera_wordmark_*.svg`: assinatura horizontal, com caracteres convertidos em caminhos vetoriais (não depende de fontes instaladas).
- `mono_dark` = monocromático escuro sobre fundo claro; `mono_light` = branco sobre fundo escuro.
- PNGs de símbolo em 16, 24, 32, 48, 64, 96, 128, 256 e 512 px e assinatura nos tamanhos 256–1530 px, sempre com transparência.

## Cores
- Slate `#1F2937`
- Teal `#2CA58D`
- Sand `#D4A95B`
- Sky `#60A5FA` (cor de apoio, não presente no logo)
- Off White `#F8F9FA`
- Stone `#E5E7EB`

## ESP32 / LVGL
O SVG é o arquivo mestre. Para firmware, prefira PNG transparente pré-renderizado em tamanho final; converter com LVGL Image Converter ou usar decodificador PNG configurado conforme orçamento de flash/PSRAM. Para máximo desempenho, converter em formato C array/indexado ou RGB565 + alpha conforme suporte do projeto. Para boot logo, considere `tessera_symbol_*_128px.png`, redimensionando antes da conversão caso necessário. Transparência e formatos de cor devem ser validados na tela real.

## Licenciamento
Este pacote foi criado para o Tessera a partir da referência fornecida. Recomenda-se verificar marcas e semelhanças visuais antes de registro/uso comercial.
