## So bringst du die Firmware aufs Newsboard

### Welche Datei brauchst du?

| Datei | Wofür |
|-------|-------|
| `newsboard-full.bin` | **Erstinstallation per USB** – Standard-Display (2,8″, ST7789, 320×240) |
| `newsboard-legacy-full.bin` | **Erstinstallation per USB** – altes 2″-Panel („Posteule“) |
| `newsboard.bin` | **Update per WLAN** – Standard-Display |
| `newsboard-legacy.bin` | **Update per WLAN** – altes 2″-Panel |

Nimm immer die Variante, die zu deinem Display passt. Nicht sicher? Das
aktuelle Board hat ein 2,8″-Display, das alte („Posteule“) ein deutlich
kleineres mit 2″. Eine falsche Datei
führt zu einem falsch angesteuerten Display (Farben, Ausrichtung).

### Erstinstallation per USB (einmalig)

Nötig, wenn dein Board noch keine Firmware hat oder noch mit einer Version
vor der OTA-Funktion läuft (neue Partitionstabelle). Deine Einstellungen
(WLAN, Module, Kalender) bleiben dabei erhalten.

1. Board per USB-C an den PC anschließen. Wird es nicht erkannt: **BOOT-Taste
   gedrückt halten**, während du das Kabel einsteckst.
2. `newsboard-full.bin` (bzw. `newsboard-legacy-full.bin`) herunterladen.
3. Flashen – eine der beiden Möglichkeiten:
   - **Im Browser (Chrome/Edge)**: auf <https://espressif.github.io/esptool-js/>
     das Board verbinden, als Adresse `0x0` und die Datei auswählen, dann
     „Program“.
   - **Mit esptool** (`pip install esptool`):
     ```sh
     esptool.py --chip esp32c6 -b 460800 write_flash 0x0 newsboard-full.bin
     ```
     Unter Windows ggf. `-p COM12` ergänzen.
4. Board kurz vom USB trennen und wieder anstecken.

### Danach: Updates ohne Kabel

1. `newsboard.bin` (bzw. `newsboard-legacy.bin`) herunterladen.
2. Weboberfläche des Boards öffnen, mit dem Code vom Display koppeln.
3. Bereich **Firmware** → Datei auswählen → **Update installieren**.
4. Das Board startet von selbst neu. Startet die neue Version nicht sauber,
   fällt es automatisch auf die alte zurück.

### Erster Start

Ist noch kein WLAN gespeichert, öffnet das Board den Hotspot `NEWSBOARD-XXXX`
(Name und Passwort stehen auf dem Display). Mit dem Handy verbinden, im
Browser `http://192.168.4.1` öffnen und den Kopplungscode vom Display
eingeben. Mehr dazu in der [README](https://github.com/Joni9993/newsapp#inbetriebnahme).

---
