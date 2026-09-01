# I2S Echo

A Zephyr application that echoes audio in real time: it continuously reads
blocks from an I2S RX stream and writes them back out on an I2S TX stream,
with no processing in between. It's used to validate the I2S RX/TX
capability of a board (and, where present, an external audio codec) before
building anything more complex on top.

Audio format:
- 16-bit samples
- 44.1 kHz sample rate
- 2 channels (stereo)
- Block size: 256 samples/channel (`SAMPLES_PER_BLOCK` in
  [src/main.c](src/main.c)), 8 blocks per direction

## How it works

[src/main.c](src/main.c) looks up two I2S devices from devicetree via the
`i2s-tx` / `i2s-rx` aliases (or node labels `i2s_tx` / `i2s_rx`):

1. TX is configured as the I2S bit-clock/frame-clock **controller**
   (master); RX is configured as the **target** (slave), running
   synchronously off the same clock.
2. If `CONFIG_AUDIO_CODEC` is enabled, an external codec device
   (`audio_codec` node label) is also configured for simultaneous
   playback + capture over I2S.
3. Four empty TX blocks are queued to prime the DMA/FIFO, then RX and TX
   streams are triggered.
4. The main loop repeatedly calls `i2s_read()`, copies the received block
   into a TX memory slab, frees the RX block, and calls `i2s_write()` -
   i.e. RX audio comes straight back out on TX.

## Supported boards

Board-specific devicetree overlays and Kconfig fragments live under
[boards/](boards/).

| Board target | RX | TX | External codec |
|---|---|---|---|
| `nucleo_h563zi` | SAI1_B (target/slave) | SAI1_A (controller/master) | none - pure SAI loopback |
| `stm32n6570_dk/stm32n657xx/fsbl` | SAI1_B (target, sync) | SAI1_A (controller/master) | Wolfson WM8904 over I2C2 |
| `zal_smart_endpoint/mimxrt1064/aam` | SAI RX | SAI TX | Wolfson WM8904 over I2C |

## Building and flashing

Use [flash.sh](flash.sh) to build and flash:

```bash
./flash.sh
```

It prompts you to pick a board, runs `west build -p -b <board>`, and then
`west flash` on success.

## Adding a new board

PRs adding support for new boards are welcome. To add one:

1. Add a `boards/<board>.overlay` and/or `boards/<board>.conf`
2. Add the board target string to the `boards` array in
   [flash.sh](flash.sh) so it shows up in the selection prompt.
3. Add a row for it in the [Supported boards](#supported-boards) table
   above.

