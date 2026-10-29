# Hinweise für KI-Assistenten

Diese Datei lesen alle KI-Werkzeuge, die in diesem Repository arbeiten (Claude Code liest sie über
`CLAUDE.md` mit). Allgemeines zum Projekt steht in der [README](README.md).

<!-- owner-rules v3 (angepasst für newsapp: übersetzt ins Deutsche): general (AGENTS.md) - begin -->

## Allgemeine Regeln

### Personen

- Unter „Projektwissen“ steht, **wer am Projekt arbeitet** und wer programmiert.
- Finde am Anfang der Sitzung heraus, mit wem du arbeitest; wenn es unklar ist, frag nach.
- Mit jemandem, der nicht programmiert: **Deutsch** sprechen, in einfachen Worten, ohne Fachjargon. Sag, was
  sich für ihn ändert, nicht welche Datei.
- Mit allen:
  - Wenn jemand nach Ideen fragt oder etwas unklar ist: **Varianten anbieten und wählen lassen, bevor du
    baust**, mit einer Empfehlung. Baue keinen Vorschlag, den niemand ausgewählt hat.
  - **Ehrlich berichten**: ein fehlgeschlagener Test, ein übersprungener Schritt, ein Fix, der sich als
    falsch herausgestellt hat. Sag, was **wirklich getestet** und was nur gebaut oder kompiliert ist, und was
    das für die Person bedeutet.
  - **Mergen, veröffentlichen oder ausliefern nur, wenn die Person es will.** Arbeit anderer nie ohne deren
    Zustimmung mergen oder überschreiben; stattdessen vorschlagen (Pull Request, Kommentar).

### Testen

- Wo das Projekt automatische Tests hat, bekommt **jedes neue Verhalten, das sich automatisch testen lässt,
  einen Test und eine Sabotage-Prüfung**: das Verhalten absichtlich kaputt machen (es muss noch kompilieren
  und wirklich das Verhalten brechen, nicht nur einen Text), Test laufen lassen, er muss fehlschlagen; Code
  zurücksetzen und mit `cmp` prüfen, dass die Datei Byte für Byte wieder dieselbe ist. Ein Test, der gegen
  kaputten Code besteht, ist wertlos.
- **Was sich nicht automatisch testen lässt** (Hardware, Geräte, Displays, Animationen, Dinge mit echtem
  Konto oder Dienst), bekommt keine eigene Testsuite. So weit wie möglich prüfen (Build, Kompilieren,
  Vorschau), dann am echten Gerät oder Dienst, und klar sagen, welcher Teil wie geprüft ist.
- Wo das Projekt gar keine Tests hat, nicht eigenmächtig eine Testsuite aufbauen: wäre eine Änderung ohne
  Tests riskant, das als Wahl anbieten.
- Vor dem Push die Prüfungen des Projekts laufen lassen, wie unter „Projektwissen“ beschrieben.

### Änderungen zeigen

- Sichtbare Änderungen zeigen statt beschreiben: **Screenshots**, wo das Ergebnis auf einem Computer läuft.
- Wo kein Screenshot möglich ist (Gerät, Display, Handy-App, Druck): eine **Vorschau** bauen (Mockup-Seite,
  Pixel-Vorschau) oder um ein **Foto** vom echten Gerät bitten.

### Fakten an der Quelle prüfen

- Nicht aus dem Gedächtnis bauen, wo man nachsehen kann: den echten Quellcode einer Bibliothek lesen, die
  echte Antwort eines Servers oder Dienstes ansehen, die echten Daten lesen. Verhalten, das „so sein
  sollte“, ist es oft nicht.
- Ist eine Ursache unklar, **zuerst nach einem Log oder einer Fehlermeldung fragen, statt zu raten**, und
  sagen, was es zeigen würde.

### Wissen im Repository festhalten

- Sitzungen enden und werden gelöscht; das Repository bleibt. Was eine spätere Sitzung wissen muss, gehört in
  die Dateien des Projekts („Projektwissen“, README, weitere Doku, Commit-Nachrichten), nicht nur ins
  Gespräch: Entscheidungen und ihre Gründe, Fallen, die Zeit gekostet haben, wie man baut, testet und
  veröffentlicht, und die Grenzen der Umgebung (was eine Cloud-Sitzung hier nicht kann).

<!-- owner-rules v3 (angepasst für newsapp: übersetzt ins Deutsche): general (AGENTS.md) - end -->

## Projektwissen

### Personen

- **Der Ersteller des Repositorys** programmiert selbst, kennt den Code und entscheidet über Merges in
  seine Arbeit.
- **Ein Mitnutzer** programmiert **nicht**. Er bringt Wünsche ein, testet am echten Board und spielt
  Updates ein. Mit ihm gelten die Regeln für Personen, die nicht programmieren.

### Bauen und Testen

- Die Firmware ist ein ESP-IDF-v5.3-Projekt für den ESP32-C6. In Cloud-Sitzungen gibt es **kein lokales
  ESP-IDF**: der GitHub-Build (`.github/workflows/build.yml`, „Firmware bauen“) ist die Kompilier-Prüfung.
  Er läuft bei jedem Push, baut die Standard- und die Legacy-Variante (altes 2″-Panel) und führt die
  Host-Tests aus. Nach dem Push den Lauf abwarten; erst bei grünem Build mergen.
- **Host-Tests** gibt es für Logik, die auch am PC läuft (bisher der iCal-Parser, `tests/test_ical.py`;
  Aufruf siehe README, „Tests“). Neue Logik dieser Art (Parser, Textumbruch, Prüfung von Einstellungen)
  bekommt Test und Sabotage-Prüfung.
- **Anzeige, Animation, WLAN und Update-Funktion** lassen sich nicht automatisch testen: grüner Build, dann
  Kontrolle auf dem echten Board.
- Vor jedem Push: `node --check main/app.js`, und bei Änderungen an `tools/preview.py` dessen Syntax prüfen.
- **Vorschau:** die Weboberfläche mit `python tools/preview.py` (Testdaten, README „Weboberfläche lokal
  testen“). Für das Display Pixel-Vorschauen (Schrift 3×5 Pixel, Welt 107×80 Pixel, jedes Pixel auf dem
  Display 3×3) oder ein Foto vom Board.

### Veröffentlichen

- Ein Release löst man **per Hand über die Actions-Seite** aus: Workflow „Firmware bauen“ → „Run workflow“
  auf `main` → Feld `release_tag` (z. B. `v1.9`). Der Workflow legt Tag und Release an und hängt
  `newsboard.bin`, `newsboard-legacy.bin` und die beiden `-full.bin` an.
- Die Beschreibung des Releases kommt aus `docs/release-einleitung.md`, darunter die automatische
  Änderungsliste.
- Die Boards holen Updates selbst vom neuesten Release (Online-Update). Ein Release ist also sofort bei
  allen Boards sichtbar, die danach suchen oder nachts automatisch installieren.

### Fallen

- **Online-Update** (`main/ota.c`):
  - GitHub leitet `releases/download/...` auf eine **signierte** Adresse weiter. Die Signatur steckt in der
    Query; `esp_http_client_get_url()` lässt die Query weg, deshalb den `Location`-Header roh mitschneiden.
  - Die Weiterleitung **per GET** auflösen, nicht per HEAD: GitHub signiert passend zur Methode, eine per
    HEAD erhaltene Adresse liefert beim Download 401.
  - Der Download-Server schickt TLS-Pakete von 16 KB. Der zersplitterte Speicher des Boards hat dafür keinen
    zusammenhängenden Block (`alloc(16749 bytes) failed`). Deshalb lädt das Board in **8-KB-Teilen** per
    Range über eine Verbindung und nutzt kein `esp_https_ota`.
  - Ein Fehler in der Update-Funktion lässt sich nur per **manuellem Upload** beheben, weil der Knopf ja
    genau den Fehler hat. Änderungen an `ota.c` besonders sorgfältig prüfen.
- **Einstellungen** (`main/config.c`, `config.h`):
  - Die Einstellungen liegen als ein Block im NVS. Seine Größe ist mit `_Static_assert` auf 5180 Byte
    festgelegt und darf sich nicht ändern, sonst gelten alte Einstellungen als ungültig.
  - Es ist **kein freies Byte** mehr übrig. Neue Schalter stecken als Bits in `theme`: Bit 0 Figur, Bit 1
    Terminal-Kopfzeile, Bit 2 blinkender Doppelpunkt. Nur über die Hilfsfunktionen lesen (`THEME_FIGURE()`,
    `config_tty_header()`, `config_tty_blink()`), und beim Schreiben eines Bits die anderen erhalten.
- **Anzeige:**
  - Eigene Schriftzeichen nutzen Steuercodes unter dem Leerzeichen: 1–3 Ä/Ö/Ü, 4 ß, 5 Gradzeichen
    (`TEXT_DEG` in `main/text.h`).
  - Die Kopfzeile ist schmal (107 Pixel, 4 Pixel je Zeichen): neue Elemente nur zeichnen, wenn Platz ist.

### Grenzen der Umgebung

- Aus Cloud-Sitzungen lassen sich **keine Tags pushen** (HTTP 403) und **keine Branches auf GitHub
  löschen**. Releases deshalb immer über den Workflow; erledigte Branches löscht ein Mensch auf GitHub.
- Kein lokales ESP-IDF (siehe „Bauen und Testen“).
