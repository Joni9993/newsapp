## How to get the firmware onto the Newsboard

### Which file do you need?

| File | Used for |
|------|----------|
| `newsboard-full.bin` | **First install over USB** – standard display (2.8″, ST7789, 320×240) |
| `newsboard-legacy-full.bin` | **First install over USB** – old 2″ panel ("Posteule") |
| `newsboard.bin` | **Update over Wi-Fi** – standard display |
| `newsboard-legacy.bin` | **Update over Wi-Fi** – old 2″ panel |

Always take the variant that matches your display. Not sure? The current board
has a 2.8″ display, the old one ("Posteule") a much smaller 2″ one. A wrong
file leads to a wrongly driven display (colors, orientation).

### First install over USB (one time)

Needed if your board has no firmware yet or still runs a version from before
the OTA feature (new partition table). Your settings (Wi-Fi, modules,
calendars) are kept.

1. Connect the board to the PC with USB-C. If it is not detected: **hold the
   BOOT button** while you plug in the cable.
2. Download `newsboard-full.bin` (or `newsboard-legacy-full.bin`).
3. Flash – one of two ways:
   - **In the browser (Chrome/Edge)**: on <https://espressif.github.io/esptool-js/>
     connect the board, enter `0x0` as the address and choose the file, then
     "Program".
   - **With esptool** (`pip install esptool`):
     ```sh
     esptool.py --chip esp32c6 -b 460800 write_flash 0x0 newsboard-full.bin
     ```
     On Windows add `-p COM12` if needed.
4. Unplug the board from USB briefly and plug it in again.

### After that: updates without a cable

1. Download `newsboard.bin` (or `newsboard-legacy.bin`).
2. Open the board's web interface and pair it with the code from the display.
3. Section **Firmware** → choose the file → **Update installieren** ("install
   update").
4. The board restarts by itself. If the new version does not start cleanly, it
   falls back to the old one automatically.

### First start

If no Wi-Fi is saved yet, the board opens the hotspot `NEWSBOARD-XXXX` (name
and password are shown on the display). Connect with your phone, open
`http://192.168.4.1` in the browser and enter the pairing code from the
display. More in the [README](https://github.com/Joni9993/newsapp#getting-started).

---
