# Clima

**Depende de:** a etapa 8 de `docs/superpowers/plans/2026-10-23-home-widgets.md`. Glossário: `GLOSSARY.md`. ADR: `docs/adr/0001-clima-previsao-diaria-open-meteo.md`.

**Fonte:** Open-Meteo, sem chave. Atribuição "Open-Meteo" na Tela do Clima, em texto pequeno.

## O que a pessoa vê

- **Cartão do Clima (home).** Ícone da condição + temperatura em °C e frase curta ("céu limpo", "chuva"). Sem Local: ícone genérico + "definir local". Carregando: genérico + "…". Cache velho: último valor + "desatualizado". Sem nenhum cache e falha de rede: genérico + "indisponível". Toque abre a Tela do Clima.
- **Tela do Clima.** Bloco central: ícone grande, min/max do dia, condição atual. Abaixo: grade com os **próximos 6 dias**. Voltar retorna à home. O cache permanece. Sem previsão horária nesta versão.
- **Ajustes no aparelho.** Linha Clima: o Local se define na página do celular; oferece QR. Sem teclado de cidade no painel (sem GPS no hardware).
- **Página do celular.** Campo cidade **e** botão de geolocalização do navegador. Gravamos nome + lat/lon; a resposta da página nunca devolve coordenada além do nome do Local já escolhido. Com geo: reverse geocode se der, senão rótulo "Minha localização".

## Dados

O Local gravado é nome, latitude e longitude. Busca de cidade: `https://geocoding-api.open-meteo.com/v1/search` com `count=1`, `language=pt`. Previsão: `https://api.open-meteo.com/v1/forecast` com `current=temperature_2m,weather_code`, `daily=weather_code,temperature_2m_max,temperature_2m_min`, `forecast_days=7`, `timezone=America/Sao_Paulo`.

O código WMO vira uma destas frases: céu limpo, quase limpo, parcial, nublado, neblina, garoa, chuva, neve, pancadas, tempestade. Código desconhecido vira "tempo". Cada frase tem um bitmap de ícone no painel; estado vazio usa bitmap genérico.

## Cache e rede

- Sem Local, zero pedidos.
- Com Local, uma busca a cada 30 minutos, em segundo plano, também com a Tela do Clima fechada.
- O cartão só redesenha quando temperatura, condição, ícone ou marca de desatualizado/indisponível mudam.
- Falha de rede guarda o último valor e marca desatualizado. Não apaga o cache.
- Resposta que chega depois de uma troca de Local é descartada pela geração do pedido.

## Testes no computador

- "São Paulo" no JSON de geocodificação vira nome e coordenadas conhecidas.
- Um JSON de previsão com código 61 vira "chuva", temperatura atual e 7 dias com min/max.
- Sem Local, o plano de busca fica vazio.
- Cache com 31 minutos fica desatualizado. Cache com 10 minutos fica fresco.
- Geração antiga não substitui o cache do Local novo.
