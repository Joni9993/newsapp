# Hinweise für KI-Assistenten

Diese Datei lesen alle KI-Werkzeuge, die in diesem Repository arbeiten (Claude Code liest sie über
`CLAUDE.md` mit). Allgemeines zum Projekt steht in der [README](README.md).

<!-- owner-rules v2 (angepasst für newsapp): general (AGENTS.md) - begin -->

## Allgemeine Regeln

### Wer mit dir arbeitet

Am Projekt arbeiten zwei Personen:

- **Der Ersteller des Repositorys** programmiert selbst und kennt den Code.
- **Ein Mitnutzer** programmiert **nicht**. Er bringt Wünsche ein, testet am echten Board und spielt Updates
  ein.

Finde am Anfang der Sitzung heraus, mit wem du sprichst, und richte dich danach. Mit dem Mitnutzer gilt:

- Sprich **Deutsch**, in einfachen Worten, ohne Fachjargon. Sag, was sich für ihn ändert, nicht welche Datei.
- Wenn er nach Ideen fragt oder etwas unklar ist: **Varianten anbieten und wählen lassen, bevor du baust**,
  mit einer Empfehlung. Baue keinen Vorschlag, den niemand ausgewählt hat.
- Sichtbare Änderungen **zeigen statt beschreiben** (siehe „Sichtbare Änderungen zeigen“).

Für beide gilt: ehrlich berichten. Ein fehlgeschlagener Build, ein übersprungener Schritt, ein Fix, der sich
als falsch herausgestellt hat: sagen, und sagen, was es für sie bedeutet.

### Tests und Prüfungen

- **Host-Tests** gibt es für Logik, die auch am PC läuft (bisher der iCal-Parser, `tests/test_ical.py`; Aufruf
  siehe README, Abschnitt „Tests“). Neue Logik dieser Art (Parser, Textumbruch, Prüfung von Einstellungen)
  bekommt einen Test **und eine Sabotage-Prüfung**: das Verhalten absichtlich kaputt machen (es muss noch
  kompilieren), Test laufen lassen, er muss fehlschlagen; Code zurücksetzen und mit `cmp` prüfen, dass die
  Datei Byte für Byte wieder dieselbe ist.
- **Anzeige, Animation, WLAN, Update-Funktion** lassen sich am PC nicht automatisch testen. Dafür baut man
  keine Testsuite, sondern prüft so: der GitHub-Build muss grün sein, dann eine Kontrolle auf dem echten Board.
  Was nur kompiliert, aber nicht auf dem Board getestet ist, ehrlich so nennen.
- Vor jedem Push: `node --check main/app.js`, und bei Änderungen an `tools/preview.py` dessen Syntax prüfen.

### Sichtbare Änderungen zeigen

Vom Display gibt es keine Screenshots. Stattdessen:

- **Weboberfläche:** `python tools/preview.py` startet eine Vorschau mit Testdaten (README, „Weboberfläche
  lokal testen“); davon Screenshots machen.
- **Display:** Pixel-Vorschauen bauen (die Schrift ist 3×5 Pixel, die Welt 107×80 Pixel, jedes Pixel auf dem
  Display 3×3), oder den Mitnutzer um ein Foto vom Board bitten.

### Wissen im Repository festhalten

Sitzungen enden und werden gelöscht; das Repository bleibt. Entscheidungen und ihre Gründe, Fallen, die Zeit
gekostet haben, und wie man baut und veröffentlicht, gehören in diese Datei, die README oder die
Commit-Nachrichten, nicht nur ins Gespräch.

<!-- owner-rules v2 (angepasst für newsapp): general (AGENTS.md) - end -->

## Projektwissen

### Bauen

- Die Firmware ist ein ESP-IDF-v5.3-Projekt für den ESP32-C6. In Cloud-Sitzungen gibt es **kein lokales
  ESP-IDF**: der GitHub-Build (`.github/workflows/build.yml`, „Firmware bauen“) ist die Kompilier-Prüfung.
  Er läuft bei jedem Push, baut die Standard- und die Legacy-Variante (altes 2″-Panel) und führt die
  Host-Tests aus.
- Nach dem Push den Build-Lauf abwarten und auswerten; erst bei grünem Build mergen.

### Veröffentlichen

- Ein Release löst man **per Hand über die Actions-Seite** aus: Workflow „Firmware bauen“ → „Run workflow“
  auf `main` → Feld `release_tag` (z. B. `v1.9`). Der Workflow legt Tag und Release an und hängt
  `newsboard.bin`, `newsboard-legacy.bin` und die beiden `-full.bin` an.
- Tags lassen sich aus Cloud-Sitzungen **nicht** pushen (HTTP 403), Branches auf GitHub auch nicht löschen.
  Releases also immer über den Workflow; erledigte Branches löscht ein Mensch auf GitHub.
- Die Beschreibung des Releases kommt aus `docs/release-einleitung.md`, darunter die automatische
  Änderungsliste.
- Die Boards holen Updates selbst vom neuesten Release (Online-Update). Ein Release ist also sofort bei
  allen Boards sichtbar, die danach suchen oder nachts automatisch installieren.

### Fallen beim Online-Update (`main/ota.c`)

- GitHub leitet `releases/download/...` auf eine **signierte** Adresse weiter. Die Signatur steckt in der
  Query; `esp_http_client_get_url()` lässt die Query weg, deshalb den `Location`-Header roh mitschneiden.
- Die Weiterleitung **per GET** auflösen, nicht per HEAD: GitHub signiert passend zur Methode, eine per HEAD
  erhaltene Adresse liefert beim Download 401.
- Der Download-Server schickt TLS-Pakete von 16 KB. Der zersplitterte Speicher des Boards hat dafür keinen
  zusammenhängenden Block (`alloc(16749 bytes) failed`). Deshalb lädt das Board in **8-KB-Teilen** per Range
  über eine Verbindung und nutzt kein `esp_https_ota`.
- Ein Fehler in der Update-Funktion lässt sich nur per **manuellem Upload** beheben, weil der Knopf ja
  genau den Fehler hat. Änderungen an `ota.c` deshalb besonders sorgfältig prüfen.

### Einstellungen (`main/config.c`, `config.h`)

- Die Einstellungen liegen als ein Block im NVS. Seine Größe ist mit `_Static_assert` auf 5180 Byte
  festgelegt und darf sich nicht ändern, sonst gelten alte Einstellungen als ungültig.
- Es ist **kein freies Byte** mehr übrig. Neue Schalter stecken als Bits in `theme`: Bit 0 Figur, Bit 1
  Terminal-Kopfzeile, Bit 2 blinkender Doppelpunkt. Nur über die Hilfsfunktionen lesen (`THEME_FIGURE()`,
  `config_tty_header()`, `config_tty_blink()`), und beim Schreiben eines Bits die anderen erhalten.

### Anzeige

- Eigene Schriftzeichen nutzen Steuercodes unter dem Leerzeichen: 1–3 Ä/Ö/Ü, 4 ß, 5 Gradzeichen
  (`TEXT_DEG` in `main/text.h`).
- Die Kopfzeile ist schmal (107 Pixel, 4 Pixel je Zeichen): neue Elemente nur zeichnen, wenn Platz ist.
