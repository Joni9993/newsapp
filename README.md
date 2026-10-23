# Nachrichtenbote (Newsboard)

Ein kleines Info-Display auf Basis eines **Seeed XIAO ESP32-C6** mit einem
**ST7789-TFT (320×240, quer)**. Eine animierte Posteule (oder wahlweise ein
Roboter in einer zerfallenen Stadt) trägt Nachrichten, Wetter und Fakten Zeile
für Zeile ins Bild. Alternativ gibt es ein Terminal-Dashboard im Retro-Look.

Eingerichtet und bedient wird das Board über eine eingebaute Weboberfläche –
ein Handy im selben WLAN genügt.

## Funktionen

- **17 Module**, einzeln an-/abschaltbar und frei sortierbar:
  Tagesschau, Wetter (Open-Meteo), Heute in der Geschichte, Geburtstage,
  Wikipedia-Zufall, Sonnenzeiten, Mond, Kalenderwoche, Ökostrom,
  Luftqualität, Erdbeben, Feiertage, Menschen im Weltall, Bitcoin,
  Euro/US-Dollar, Meine Termine, Uhrzeit
- **Kalender**: bis zu 3 iCal-Kalender (z. B. Google-Privatadresse `.ics`),
  mit eigenem Parser inkl. Wiederholungen, Ausnahmen und Zeitzonen
- **Push-Nachrichten vom Handy**: eine Automatisierungs-App (z. B. Automate
  oder MacroDroid) leitet Benachrichtigungen an das Board weiter
- **Darstellung**: Posteule/Roboter (animiert) oder Terminal-Dashboard,
  Helligkeit mit Nachtabsenkung, Anzeigedauer pro Karte
- **Weboberfläche** (direkt in die Firmware eingebettet) mit Kopplung per
  6-stelligem Code, der auf dem Display erscheint

## Hardware

| Display-Pin | XIAO-Pin | GPIO |
|-------------|----------|------|
| SCK         | D8       | 19   |
| MOSI (SDA)  | D10      | 18   |
| CS          | D3       | 21   |
| DC          | D1       | 1    |
| RST         | D2       | 2    |
| BL (Licht)  | D0       | 0    |

Beim alten Board („Posteule“, 2″-Panel) sind DC und RST vertauscht und die
Farbumkehr ist an – dafür gibt es den Legacy-Build (siehe unten). Die Pins
stehen in [main/lcd.h](main/lcd.h).

Die **BOOT-Taste (GPIO 9)** ca. 5 Sekunden gedrückt halten startet den
Einrichtungsmodus neu und zeigt den Kopplungscode an.

> Der ESP32-C6 unterstützt nur **2,4-GHz-WLAN**.

## Projektstruktur

```
main/            Firmware (C, ESP-IDF-Komponente)
  main.c         Einstieg: NVS, Konfiguration, Display, WLAN, Quellen, Webserver
  sources.c      Holt Daten aus dem Netz und erzeugt Karten (eigene Task)
  courier.c      Animation des Boten (Eule/Roboter), scene.c / sprites.c Grafik
  tty.c          Terminal-Dashboard-Ansicht
  web.c          HTTP-Server + REST-API (/api/v1/...)
  index.html, app.js, style.css   Weboberfläche (wird eingebettet)
  ical.c, calendar.c              iCal-Parser und Kalender-Modul
  notify.c       Push-Nachrichten
  config.c       Einstellungen (im NVS gespeichert)
tests/           Host-Tests für den iCal-Parser (Python + ctypes)
tools/           Build-Skript, serieller Monitor, UI-Vorschau
partitions.csv   Partitionstabelle (3 MB App)
sdkconfig.defaults
```

## Einrichtung der Entwicklungsumgebung

1. **ESP-IDF v5.3** installieren (z. B. mit dem
   [ESP-IDF Installer](https://docs.espressif.com/projects/esp-idf/en/v5.3/esp32c6/get-started/)).
   Das Build-Skript erwartet ESP-IDF unter `%USERPROFILE%\esp\esp-idf`.
2. Optional: **Node.js** – das Build-Skript prüft damit `main/app.js` auf
   Syntaxfehler.
3. Optional für Tools/Tests: **Python 3** mit `pyserial`
   (`pip install pyserial`).

## Bauen und Flashen

Unter Windows (PowerShell) mit dem mitgelieferten Skript:

```powershell
# nur bauen
.\tools\build.ps1

# bauen und flashen (Port anpassen)
.\tools\build.ps1 -Flash -Port COM12
```

Oder direkt mit `idf.py` in einer ESP-IDF-Shell:

```sh
idf.py set-target esp32c6     # nur beim ersten Mal
idf.py build
idf.py -p COM12 flash monitor
```

### Build-Varianten

```sh
# altes 2"-Panel (DC/RST vertauscht, andere Ausrichtung/Farbumkehr)
idf.py -B build-legacy -DNEWSBOARD_LEGACY_PANEL=1 build

# reiner Display-Test (Farbbalken, ohne WLAN/Webserver)
idf.py -B build-test -DNEWSBOARD_DISPLAY_TEST=1 build
```

## Inbetriebnahme

1. **Erster Start**: Ist noch kein WLAN gespeichert, öffnet das Board einen
   eigenen Hotspot `NEWSBOARD-XXXX`. Name, Passwort und ein QR-Code zum
   Verbinden stehen auf dem Display.
2. Mit dem Handy verbinden und im Browser **http://192.168.4.1** öffnen.
3. Den **Kopplungscode** vom Display im Browser eingeben.
4. Heim-WLAN auswählen und Passwort eintragen. Danach ist das Board unter
   seiner IP-Adresse im Heimnetz erreichbar (wird auf dem Display angezeigt).
5. In der Weboberfläche Ort/Bundesland, Module, Kalender, Darstellung und
   Helligkeit einstellen und speichern.

Alle Einstellungen liegen im NVS-Flash und bleiben auch nach einem Update
erhalten.

### Kalender

In der Weboberfläche bis zu drei Kalender eintragen. Bei Google Kalender die
**„Privatadresse im iCal-Format“** (endet auf `.ics`) verwenden, nicht die
Kalender-ID. Links müssen mit `https://` beginnen.

### Push-Nachrichten

In der Weboberfläche lassen sich Push-Nachrichten aktivieren; dort steht auch
der Token. Eine App sendet dann einen HTTP-Request an:

```
POST http://<board-ip>/api/v1/notify?token=<token>
Content-Type: application/json

{"app": "Signal", "title": "Anna", "text": "Bin gleich da"}
```

Die Felder `app`, `title`, `text` und `token` dürfen auch als Query-Parameter
oder Formulardaten kommen. Für **LlamaLab Automate** kann die Weboberfläche
einen fertigen Flow herunterladen, in dem IP und Token schon eingetragen sind.

## Entwicklung

### Weboberfläche lokal testen

```sh
python tools/preview.py
```

Startet einen Vorschau-Server unter http://127.0.0.1:8765 mit Beispieldaten
(Kopplungscode: `123456`). Die Dateien werden direkt aus `main/` geladen –
Änderungen an HTML/CSS/JS sind nach einem Neuladen sichtbar.

### Serielles Log mitschneiden

```sh
python tools/monitor.py --port COM12 --seconds 30
```

Speichert die Ausgabe in `build/runtime.log` und zeigt wichtige Zeilen
(Fehler, IP, Neustarts, Heap) an.

### Tests

Die Tests in [tests/test_ical.py](tests/test_ical.py) prüfen den echten
iCal-Parser der Firmware auf dem PC. Dazu muss `main/ical.c` zuerst als
Bibliothek `build/ical-test.dll` gebaut werden, z. B. mit MinGW-GCC:

```sh
gcc -shared -O2 -o build/ical-test.dll main/ical.c
python tests/test_ical.py
```
