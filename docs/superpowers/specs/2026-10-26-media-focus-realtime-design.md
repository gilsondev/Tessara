# Media + Focus em tempo quase real (agentd)

**Status:** approved (opção A)  
**Data:** 2026-10-26

## Problema

O agentd faz poll AppleScript/plist a cada 2 s. Troca de faixa e Focus chegam atrasados; `MEDIA_COVER` é stub; Focus pode listar modos do fallback hardcoded que o Mac não tem.

## Abordagem (A)

Eventos públicos do macOS + poll só como rede de segurança.

| Domínio | Fonte rápida | Fallback |
|---------|--------------|----------|
| Music | `com.apple.Music.playerInfo` (DNC) | poll AppleScript 8 s |
| Spotify | `com.spotify.client.PlaybackStateChanged` | idem |
| Focus on/off | `_NSDoNotDisturbEnabled/DisabledNotification` + mtime de `Assertions.json` / `ModeConfigurations.json` | poll 8 s |
| Capa | no wake de faixa: baixar/redimensionar → `MEDIA_COVER` RGB565 chunked | sem capa se falhar |
| Lista Focus | só `ModeConfigurations` do Mac | lista vazia (sem fallback CC) |

## Wire

- `MEDIA\tv1\t…` inalterado (push imediato no wake).
- `MEDIA_COVER\tv1\t1\t<key>\t<w>\t<h>\trgb565le\t<chunks>\t<i>\t<hex>` — host converte JPEG→RGB565 little-endian; firmware monta `lv_image_dsc_t`.
- `FOCUS\tv1\t…` inalterado; `n` só com modos reais.

## Fora

- MediaRemote privado / `media-control`.
- Players sem DNC (YouTube) — só fallback lento.
- Cover no card home (só página Mac neste corte, se couber no layout).
