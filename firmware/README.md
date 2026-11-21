# Firmware do Controle Fisico

Controle retro (SNES escuro) para o VLC: ESP32-C3 Super Mini + tela ST7789
de 2.4" (320x240) + encoder EC11. Ele conversa com o servidor deste repo
(`/api/status`, `/api/control`) por Wi-Fi, nunca direto com o VLC, entao a
senha do VLC fica so no computador.

![Tela inicial](../docs/assets/knob/home.png)

Ligacoes: [../docs/wiring/esp32c3-tft-ec11.png](../docs/wiring/esp32c3-tft-ec11.png)
(alimente o modulo pelo 3V3, nao pelo 5V).

## Controles

| Controle | Tela inicial | Modo seek | Menu |
| --- | --- | --- | --- |
| Girar o knob | Volume (2%, girar rapido da passos maiores) | Pula 10/30/60 s | Navega |
| Clicar o knob | Entra no modo seek | Volta ao volume | Seleciona |
| Segurar o knob | Abre o menu | Abre o menu | Fecha |
| Botao K0 | Play/pause | Play/pause | Volta |
| Segurar K0 | Mudo/volta o som | Mudo/volta o som | Fecha |

O modo seek volta para o volume depois de 4 s parado e o menu fecha depois
de 8 s, entao o knob nunca faz seek sem querer. O menu tem as listas de
faixas de AUDIO e LEGENDAS. A tela escurece depois de 2 minutos sem uso.

## Compilar e Gravar

```bash
cp src/config.example.h src/config.h   # Wi-Fi, IP do servidor e PORT do ../.env
pio run -t upload
```

Ajustes em `config.h`: `SCREEN_ROTATION` (3 = knob a direita, 1 = invertido),
`ENC_DIR` (inverte o sentido do knob), `VOL_STEP`, `VOL_MAX`, `DIM_AFTER_MS`.

O servidor precisa do action `seekBy` (incluido em `src/vlcClient.js`).

## Telas de Erro

- **NO WIFI**: nenhuma rede do `config.h` respondeu.
- **NO SERVER**: o Wi-Fi esta ok, mas o `npm start` nao esta rodando em
  `SERVER_HOST:SERVER_PORT` (ou um firewall bloqueia a porta).
- **VLC CLOSED**: o servidor respondeu 502; abra o VLC com
  `./scripts/start-vlc.sh`.

## Capturas de Tela

As imagens do README sao capturas reais do framebuffer, enviadas pela USB:

```bash
~/.local/share/pipx/venvs/platformio/bin/python firmware/tools/screenshot.py docs/assets/knob
```

O script coloca o firmware em modo demo (dados ficticios, nada e enviado ao
VLC), simula o knob e os botoes, salva cada tela e volta ao modo normal. Os
comandos do console serial estao em `src/console.h`.

## Estrutura

```text
src/main.cpp      Inicializacao da tela, SPI e tarefas
src/ui.h          Telas e controles
src/net.h         Tarefa de rede: Wi-Fi, polling e comandos agrupados
src/gfx.h         Desenho retro em faixas de 320x40 (sem flicker, 25 KB de RAM)
src/input.h       Encoder EC11 e botoes
src/console.h     Console serial (modo demo e capturas)
src/theme.h       Paleta SNES escura (16 cores)
src/pixelfont.h   Fontes Press Start 2P e GLCD 5x7
tools/            screenshot.py
```
