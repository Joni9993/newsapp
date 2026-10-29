<p align="center"><img src="docs/logo.svg" alt="Logo: pixel owl" width="160"></p>

# Newsboard

A small info display built on a **Seeed XIAO ESP32-C6** with an
**ST7789 TFT (320×240, landscape)**. An animated courier owl (or, if you prefer,
a robot in a ruined city) carries news, weather and facts into the picture line
by line. Alternatively there is a retro-style terminal dashboard.

The board is set up and operated through a built-in web interface – a phone on
the same Wi-Fi is all you need. The display and the web interface are in
German.

## Features

- **18 modules**, each can be switched on or off and freely reordered:
  Tagesschau (news), weather (Open-Meteo), today in history, birthdays,
  random Wikipedia article, sun times, moon, calendar week, green electricity,
  air quality, earthquakes, public holidays, people in space, Bitcoin,
  euro/US dollar, my appointments, clock, JW.ORG news
- **Calendars**: up to 6 iCal calendars (e.g. the Google private address
  `.ics`), with its own parser including recurrences, exceptions and time zones
- **Push notifications from your phone**: an automation app (e.g. Automate or
  MacroDroid) forwards notifications to the board
- **Appearance**: owl/robot (animated) or terminal dashboard, brightness with
  a night dimming mode, display time per card
- **Web interface** (embedded directly in the firmware) with pairing by a
  6-digit code that appears on the display

## Hardware

| Display pin | XIAO pin | GPIO |
|-------------|----------|------|
| SCK         | D8       | 19   |
| MOSI (SDA)  | D10      | 18   |
| CS          | D3       | 21   |
| DC          | D1       | 1    |
| RST         | D2       | 2    |
| BL (light)  | D0       | 0    |

On the old board ("Posteule", 2″ panel) DC and RST are swapped and color
inversion is on – there is a legacy build for that (see below). The pins are
defined in [main/lcd.h](main/lcd.h).

Holding the **BOOT button (GPIO 9)** for about 5 seconds restarts setup mode
and shows the pairing code.

> The ESP32-C6 only supports **2.4 GHz Wi-Fi**.

## Project structure

```
main/            Firmware (C, ESP-IDF component)
  main.c         Entry point: NVS, configuration, display, Wi-Fi, sources, web server
  sources.c      Fetches data from the network and creates cards (own task)
  courier.c      Courier animation (owl/robot), scene.c / sprites.c graphics
  tty.c          Terminal dashboard view
  web.c          HTTP server + REST API (/api/v1/...)
  index.html, app.js, style.css   Web interface (embedded in the firmware)
  ical.c, calendar.c              iCal parser and calendar module
  rss.c          RSS/Atom reader (JW.ORG news)
  notify.c       Push notifications
  ota.c          Firmware update by upload (OTA) + rollback
  config.c       Settings (stored in NVS)
tests/           Host tests for the iCal parser (Python + ctypes)
tools/           Build script, serial monitor, UI preview
partitions.csv   Partition table (2 × 1.875 MB OTA app)
.github/         CI: builds the firmware (.bin) on every push
sdkconfig.defaults
```

## Setting up the development environment

1. Install **ESP-IDF v5.3** (e.g. with the
   [ESP-IDF Installer](https://docs.espressif.com/projects/esp-idf/en/v5.3/esp32c6/get-started/)).
   The build script expects ESP-IDF at `%USERPROFILE%\esp\esp-idf`.
2. Optional: **Node.js** – the build script uses it to check `main/app.js` for
   syntax errors.
3. Optional, for tools/tests: **Python 3** with `pyserial`
   (`pip install pyserial`).

## Building and flashing

On Windows (PowerShell) with the included script:

```powershell
# build only
.\tools\build.ps1

# build and flash (adjust the port)
.\tools\build.ps1 -Flash -Port COM12
```

Or directly with `idf.py` in an ESP-IDF shell:

```sh
idf.py set-target esp32c6     # only the first time
idf.py build
idf.py -p COM12 flash monitor
```

### Build variants

```sh
# old 2" panel (DC/RST swapped, different orientation/color inversion)
idf.py -B build-legacy -DNEWSBOARD_LEGACY_PANEL=1 build

# display test only (color bars, no Wi-Fi/web server)
idf.py -B build-test -DNEWSBOARD_DISPLAY_TEST=1 build
```

## Getting started

1. **First start**: if no Wi-Fi is saved yet, the board opens its own hotspot
   `NEWSBOARD-XXXX`. Name, password and a QR code for connecting are shown on
   the display.
2. Connect with your phone and open **http://192.168.4.1** in the browser.
3. Enter the **pairing code** from the display in the browser.
4. Choose your home Wi-Fi and enter the password. After that the board can be
   reached on your home network at its IP address (shown on the display).
5. In the web interface, set location/federal state, modules, calendars,
   appearance and brightness, and save.

All settings are kept in NVS flash and survive an update (see
[Update over Wi-Fi](#update-over-wi-fi-ota)).

### Calendars

Enter up to six calendars in the web interface (more with the + button). For
Google Calendar use the **"Privatadresse im iCal-Format"** (secret address in
iCal format, ends in `.ics`), not the calendar ID. Links must start with
`https://`.

### Push notifications

Push notifications can be switched on in the web interface; the token is shown
there too. An app then sends an HTTP request to:

```
POST http://<board-ip>/api/v1/notify?token=<token>
Content-Type: application/json

{"app": "Signal", "title": "Anna", "text": "Bin gleich da"}
```

The fields `app`, `title`, `text` and `token` may also come as query
parameters or form data. For **LlamaLab Automate** the web interface can
download a ready-made flow with the IP and token already filled in.

## Update over Wi-Fi (OTA)

New firmware can be installed without a cable. The board does not fetch
anything from the internet (the GitHub repo is private); it receives the `.bin`
by upload in the web interface. It is built automatically by GitHub Actions –
no local ESP-IDF is needed.

**Once over USB:** the partition table has changed (two OTA partitions instead
of one big app partition). So the board has to be flashed over USB once
(`idf.py flash` or the `…-full.bin` from the build, from address 0). Your
settings are kept: the NVS area stays unchanged at `0x9000`. After that
everything works over Wi-Fi.

**Installing an update:**

1. On GitHub, download the file **`newsboard.bin`** under **Actions** (artifact
   `newsboard-firmware`) or **Releases**. For the old 2″ panel take
   **`newsboard-legacy.bin`** instead.
2. Open the board's web interface → tab **fw** (firmware).
3. Choose the file → **Update installieren** ("install update") → confirm.
4. The progress bar runs, the board restarts (about 30 s) and the page shows
   the new version.

Only images with a matching project name are accepted. The firmware is checked
for size and checksum during upload.

**Variant protection:** the version of legacy builds ends in `-legacy` (in CI
and locally with `NEWSBOARD_LEGACY_PANEL`). The upload rejects a file for the
wrong display ("Falsche Display-Variante: …", wrong display variant). The
checkbox **Variante nicht prüfen** ("don't check variant") next to the upload
appends `?force=1` to the request and skips this check – for special cases
only. The online update picks the right file by itself.

**Online update:** in the tab **fw**, tap **Nach Update suchen** ("check for
updates"). The board asks GitHub for the newest release of
`Joni9993/newsapp` and shows the version. With **Installieren** ("install") it
downloads the matching file itself (`newsboard.bin`, automatically
`newsboard-legacy.bin` on the old panel), flashes it and restarts.
Requirements: the repo is **public** (as long as it is private, the board
reports "Kein öffentliches Release gefunden", no public release found), the
board has internet access and a synchronized clock. The manual upload remains
as an alternative. During upload and installation the data sources pause so
that memory and network stay free; the search runs in the background (result
via `GET /api/v1/ota/status`).

**Terminal header:** in the tab **disp** (only visible with the "terminal"
view) you can choose (JSON field `ttyHeader`, saved with `:w save`): `0` = IP
and status (default), `1` = location on the left, on the right the time,
weather symbol and temperature as in the owl view.
The clock colon is steady by default; `clockBlink` = `1` makes it blink.

**Automatic updates:** selectable in the tab **fw** (JSON field `autoUpdate`,
saved with `:w save`):

| Value | Mode | Behavior |
| --- | --- | --- |
| 0 | Off ("Aus") | no automatic search |
| 1 | Check and notify (default) | first search about 3 min after start (once network and clock are available), then every 24 h; a newer release shows a small arrow symbol in the header bar (owl and terminal, when there is room) and "Update verfügbar" (update available) in the tab **fw** |
| 2 | Install at night | like 1, plus a search in the window 03:00–04:59 local time and automatic installation. If the board is busy or memory is low, the next attempt only comes the following night. Each version is tried only once per night (no endless retries after a rollback), and developer builds (e.g. `v1.5-3-gabc123`) are never overwritten automatically |

**Rollback:** a newly installed firmware has to confirm itself after starting
(as soon as the web server is running). If it does not start or crashes
before that, the bootloader automatically switches back to the old version on
the next restart.

**Versions:** the version comes from `git describe` (e.g. `v1.2.0` or
`v1.2.0-5-gabc1234`). A release with both files is created automatically when
a tag `v*` is pushed.

## Development

### Testing the web interface locally

```sh
python tools/preview.py
```

Starts a preview server at http://127.0.0.1:8765 with sample data (pairing
code: `123456`). The files are loaded directly from `main/` – changes to
HTML/CSS/JS are visible after a reload.

### Capturing the serial log

```sh
python tools/monitor.py --port COM12 --seconds 30
```

Saves the output to `build/runtime.log` and shows important lines (errors, IP,
restarts, heap).

### Tests

The tests in [tests/test_ical.py](tests/test_ical.py) check the firmware's real
iCal parser on a PC. For this, `main/ical.c` must first be built as the library
`build/ical-test.dll`, e.g. with MinGW GCC (on Linux additionally `-fPIC`):

```sh
gcc -shared -O2 -o build/ical-test.dll main/ical.c
python tests/test_ical.py
```

The GitHub build runs these tests automatically on every push.
