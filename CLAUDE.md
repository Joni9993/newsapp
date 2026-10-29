@AGENTS.md

<!-- owner-rules v3 (angepasst für newsapp: übersetzt ins Deutsche): claude (CLAUDE.md) - begin -->

## Claude: Modelle und Verbrauch

Wer Claude nutzt, hat einen Plan mit Nutzungsgrenze; je stärker das Modell, desto schneller ist sie
aufgebraucht. Was eine Aufgabe kostet, ist nicht die Größe der Änderung, sondern **Anzahl der Schritte mal
Länge des Gesprächs**: jeder Schritt verarbeitet das ganze Gespräch erneut.

### Das Modell wählen

Nimm **das günstigste Modell, das die Aufgabe zuverlässig erledigt**. Stand Ende 2026 gibt es drei Stufen;
die Namen ändern sich, das Prinzip nicht. Prüfe die aktuell angebotenen Modelle (`/model`), statt dieser
Liste zu vertrauen, und folge dem Nutzer, wenn er sagt, dass sich Grenzen oder Angebot geändert haben.

| Stufe                                      | Wofür                                                                                  |
| ------------------------------------------ | -------------------------------------------------------------------------------------- |
| Klein und schnell (jetzt: **Haiku**)       | Nur Subagenten: im Code suchen, Logs und Build-Ausgaben lesen, mechanische Änderungen über viele Dateien nach genauer Anweisung. Keine Entscheidungen, keine Gespräche mit dem Nutzer. |
| Mitte (jetzt: **Sonnet**), **der Standard** | Kleine Fixes, Texte und Optik, Dokumentation, Fragen zur Funktionsweise, Auswertungen, und die Schritte einer Aufgabe, die Opus geplant hat. |
| Am stärksten (jetzt: **Opus**; **Fable**, wo verfügbar) | Ein Fehler mit unklarer Ursache, eine größere neue Funktion oder alles, was die Sicherheit oder Updates betrifft, eine schwer umkehrbare Entscheidung, ein Problem, an dem die mittlere Stufe zweimal gescheitert ist. |

- Die Sitzung läuft auf dem Modell, das der Nutzer gewählt hat; nur er kann es wechseln (`/model`). **Sag
  es**, wenn eine Aufgabe auf einer anderen Stufe besser aufgehoben wäre: hoch für etwas Schwieriges,
  runter, sobald der schwierige Teil erledigt ist.
- Subagenten bekommen ihr Modell ausdrücklich (`model: "haiku"`, `model: "sonnet"`), nie geerbt.

### Delegieren

- Auf der stärksten Stufe **die Schritte abgeben**. Alles mit mehreren Schritten (Ändern, Tests laufen
  lassen, auf einen Build warten, Sabotage-Prüfungen, Screenshots) geht an einen Subagenten auf der
  günstigsten passenden Stufe, mit kurzer Anweisung und der Bitte, knapp zu berichten: was geändert wurde,
  Ergebnis von Tests und Build, sonst nichts. Das starke Modell plant, liest Diff und Bericht, entscheidet
  und committet.
- Das gilt auch für kleine Änderungen: der kurze Kontext eines frischen Subagenten ist günstiger als jeder
  Schritt in einem langen Gespräch. Nicht argumentieren, eine Aufgabe sei zu klein zum Abgeben.
- Einem Bericht nie blind vertrauen: vor dem Commit den Diff lesen, und wo es darauf ankommt, Aussagen über
  Bibliotheken oder Dienste an der echten Quelle prüfen (siehe „Fakten an der Quelle prüfen“).
- Subagenten dürfen keine Hintergrundprozesse laufen lassen, wenn sie fertig sind.

### Kosten niedrig halten

- **Screenshots** nur, wenn sich etwas Sichtbares geändert hat. Bilder sind teuer.
- **Eine lange Sitzung kostet bei jedem Schritt.** Ist ein Thema erledigt und committet, eine neue Sitzung
  für das nächste vorschlagen; alles Wichtige steht im Repository.
- Ausgaben kurz halten: Logs und Testläufe auf das Wesentliche filtern statt ganz zu lesen.
- Beim Warten auf etwas (einen Build, ein Release): eine Erinnerung einplanen, statt Schritt für Schritt
  nachzufragen.

<!-- owner-rules v3 (angepasst für newsapp: übersetzt ins Deutsche): claude (CLAUDE.md) - end -->
