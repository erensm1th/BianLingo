<p align="center"><img src="resources/branding/logo.png" width="140" alt="BianLingo"></p>

# BianLingo

[English](README.md) · [简体中文](README.zh-CN.md) · [繁體中文](README.zh-TW.md) · [日本語](README.ja.md) · [한국어](README.ko.md)<br>
[Français](README.fr.md) · **Deutsch** · [Español](README.es.md) · [Português (Brasil)](README.pt-BR.md) · [Русский](README.ru.md)

**World-of-Warcraft-Chatübersetzung in Echtzeit.** Lies Nachrichten in der gewählten Sprache und übersetze Antworten in einem kompakten Overlay.

[Aus Quellcode bauen](docs/BUILDING.md) · [Datenschutz](PRIVACY.md)

BianLingo ist ein kostenloses, nicht kommerzielles Fanprojekt. Es ist nicht mit Blizzard Entertainment verbunden und wird von Blizzard weder unterstützt noch genehmigt.

## Funktionen

- **Lokal oder online übersetzen**: Bergamot, CTranslate2, Ollama oder eigene API-Verbindung mit URL, Modell und Schlüssel. Unterstützt OpenAI-kompatible, Anthropic- und Gemini-Anfrageformate.
- **Modelle vor dem Spielen vorbereiten**: Zielsprache wählen, benötigte Sprachpaare prüfen und herunterladen. Verfügbare Sprachen hängen von Engine und Modell ab.
- **Chat-Overlay**: Fenster verschieben und skalieren, Hintergrundtransparenz, Schriftgröße, Themen und eigene Skins einstellen.
- **Chat-Tabs**: Eigene Namen und Nachrichtenfilter oder Import der vom Addon exportierten Chatkonfiguration.
- **Antwort per Klick**: Linksklick bereitet eine Antwort im Nachrichtenkanal vor. Das Rechtsklickmenü bietet Flüstern, manuelle Stufenabfrage und Blockieren. Enter übersetzt und kopiert; Einfügen und Senden im Spiel erfolgen manuell.
- **Filter**: Kanäle, Rauschen und Namen im Schriftsystem der Zielsprache filtern. Spielbegriffe und Links schützen; getrennte Prompts für eingehende Nachrichten und Antworten bearbeiten.
- **Manuelles Nachsenden**: Standardmäßig 10 vollständige Nachrichten im Addon-Arbeitsspeicher, einstellbar auf 1–100. Rechtsklick auf das Minikartensymbol oder `/bl replay` sendet erneut; Duplikate werden nicht erneut übersetzt.
- **Pixelübertragung und OCR**: Kleine Farbmatrix oder binäres Raster, optionales Ausblenden bei Inaktivität mit kleinem Statusmarker. Windows kann einen ausgewählten Chatbereich per OCR lesen.
- **Optionale Protokolle**: Standardmäßig aus; empfangene Nachrichten und abgesendete Übersetzungsaufträge werden täglich als UTF-8 angehängt.
- **Portable Daten**: Einstellungen, Protokolle, heruntergeladene Modelle und App-Caches liegen in `UserData` neben `BianLingo.exe`.

## Übersetzungsarten

| Engine | Fähigkeiten | Grenzen | Einsatz |
|---|---|---|---|
| **Online-API** | Prompts und Stile, kein Modelldownload | Netzwerk, Kontingente, Gebühren und Datenregeln des Anbieters | Promptsteuerung ohne lokale Inferenz |
| **Ollama** | Lokales LLM mit Prompts und Stilen | Laufzeit-/Modelldownloads; hardwareabhängiger Speicher und Zeitbedarf | Lokale Verarbeitung mit Prompts |
| **Bergamot** | Offline-CPU-Übersetzung ohne API-Gebühren | Vorbereitete Sprachpaare, keine Prompts | Unterstütztes Offline-Sprachpaar |
| **CTranslate2** | Offline-CPU-INT8-Übersetzung ohne API-Gebühren | Vorbereitete OPUS-MT-Paare, keine Prompts | Offline-OPUS-MT-Sprachpaar |

Die automatische Chatzuordnung von Bergamot/CTranslate2 nimmt englischen Text an; beim Ziel Englisch wählt sie anhand von Han-Zeichen chinesischen Text. Weitere Modelle ermöglichen keine beliebige automatische Quellspracherkennung. Lokale Verarbeitung sendet keinen Chat an einen Übersetzungsanbieter. Ergebnis und Latenz hängen von Modell, Sprachpaar, Nachricht und Hardware/Netzwerk ab.

### Benchmark · AMD Ryzen 7 9700X

| Verfahren | Median pro Aufruf | CPU bei 1 Nachricht/s | Lokaler Arbeitssatz |
|---|---:|---:|---:|
| Bergamot | 20.9 ms | 0.10% | 337.5 MiB |
| CTranslate2 · Ruy CPU INT8 | 67.2 ms | 1.36% | 157.3 MiB |
| DeepSeek · `deepseek-chat` | 708.2 ms | 0.01% | 23.4 MiB |

### API-Startkonfigurationen

Diese normalen eigenen Verbindungen ändern weder UI-Sprache noch Übersetzungsziel. DeepSeek Flash unterstützt Anfragen ohne Denkmodus; Google nennt Übersetzung als Anwendungsfall für Gemini 3.5 Flash-Lite. Quellen: [DeepSeek](https://api-docs.deepseek.com/), [Gemini-Modell](https://aistudio.google.com/docs/models/gemini-3.5-flash-lite), [Gemini-Anwendungen und Preise](https://aistudio.google.com/docs/pricing#gemini-3.5-flash-lite).

| Einstellung | DeepSeek | Google Gemini |
|---|---|---|
| Basis-URL | `https://api.deepseek.com` | `https://generativelanguage.googleapis.com/v1beta/openai` |
| Modell | `deepseek-flash` | `gemini-3.5-flash-lite` |
| API-Schlüssel | [DeepSeek Platform](https://platform.deepseek.com/) | [Google AI Studio](https://aistudio.google.com/apikey) |
| Zusatzparameter | `{"thinking":{"type":"disabled"}}` | `{}` (Modellstandard) |

Unter **API und Nutzung** eine Verbindung hinzufügen. **OpenAI Chat Completions** (`openai_chat`), Authentifizierung **Auto**, **Vollständige URL** ausgeschaltet lassen und eigenen Schlüssel eingeben. In den erweiterten Einstellungen die Parameter aus der Tabelle übernehmen, zusätzliche Header leer und Saldoabfrage aus lassen. Speichern und Verbindung testen. Siehe [Verbindungen](docs/PROVIDERS.md) und Googles [OpenAI-kompatiblen Endpunkt](https://aistudio.google.com/docs/openai).

Verfügbarkeit, Preise, Kontingente und Datenverarbeitung bestimmt der Anbieter: [DeepSeek-Preise](https://api-docs.deepseek.com/quick_start/pricing/), [Gemini-Preise und Datenverwendung](https://aistudio.google.com/docs/pricing), [Gemini-Regionen](https://aistudio.google.com/docs/available-regions). Kostenlose und kostenpflichtige Gemini-Stufen haben unterschiedliche Grenzen und Datenbedingungen.

## Funktionsweise

1. Das **BianLingo-Addon** liest Nachrichten über die Addon-API und zeigt kurz codierte Pixel in einer Spielecke an.
2. Die **Desktop-App** decodiert die Pixel, übersetzt über die ausgewählte Verbindung und zeigt das Ergebnis im Overlay.

Die App liest keinen Spielspeicher, injiziert keinen Code und sendet keinen Chat automatisch. OCR erfasst den ausgewählten Bereich. Verbirgt Blizzard den Text vor Addons, kann die Pixelübertragung ihn nicht wiederherstellen; OCR ist ein eigener Eingabepfad.

## Voraussetzungen und Sprachen

| Plattform | Status |
|---|---|
| Windows 10/11 x64 | Verfügbar. Fenster-OCR ab Windows 10 1903. |
| macOS | In Entwicklung, für eine zukünftige Version geplant; noch kein macOS-Build verfügbar. |

- World of Warcraft mit BianLingo-Addon im Fenster- oder randlosen Fenstermodus.
- Lokales Modell oder eigene API-Verbindung. Das vollständige Paket enthält Bergamot Englisch ↔ vereinfachtes Chinesisch; weitere verfügbare Paare werden separat vorbereitet.

Die Oberfläche unterstützt Englisch, vereinfachtes Chinesisch, Japanisch, Koreanisch, Französisch, Deutsch, Spanisch, brasilianisches Portugiesisch und Russisch. Unter **Chat-Ansicht → Oberflächensprache** auswählen und zum sofortigen Übernehmen speichern. Standard ist die **Systemsprache**, bei nicht unterstützten Sprachen Englisch; chinesische Gebietsschemas verwenden vereinfachtes Chinesisch. Oberfläche, README und Übersetzungsziele haben unabhängige Spracheinstellungen. Die Übersetzungsabdeckung hängt von Engine und Modell ab. Fehler und Statusmeldungen erscheinen in der chinesischen Oberfläche auf Chinesisch, in allen anderen Oberflächensprachen auf Englisch.

Das Addon deklariert Unterstützung für Classic Era und kompatible Classic-/Retail-Clients. Chatverfügbarkeit hängt von Client und Situation ab. HDR, mehrere Bildschirme mit verschiedenen DPI und Spielversionen müssen in der jeweiligen Umgebung geprüft werden.

## Schnellstart

1. **Addon installieren.** `AddOns/BianLingo` nach `World of Warcraft/<client>/Interface/AddOns/` kopieren, etwa unter `_retail_`, `_classic_era_` oder `_forever_`. Die Datei muss unter `Interface/AddOns/BianLingo/BianLingo.toc` liegen. Im Spiel aktivieren und `/reload` ausführen.
2. **Starten und verschieben.** `BianLingo.exe` und `data` zusammen lassen und die EXE starten. **Ein Klick auf die Katze oben rechts öffnet die Einstellungen; Ziehen derselben Schaltfläche verschiebt das Fenster.**
3. **Übersetzung wählen.** In den API-Einstellungen eine Verbindung wählen. **Bergamot / CTranslate2:** Zielsprache wählen und das aktuelle Sprachpaket vorbereiten oder über die Paketverwaltung vorab herunterladen. **Ollama:** Modell wählen und herunterladen; kein API-Schlüssel nötig. **Online-API:** Verbindung wählen oder hinzufügen, Basis-URL, Modell und API-Schlüssel eintragen, speichern und testen. Beispiele stehen oben.
4. **Empfang konfigurieren.** Die Chat-Zielsprache in den API-Einstellungen und die Kanäle im Übersetzungsumfang wählen, dann speichern. Lokale Übersetzung benötigt ein unterstütztes, vorbereitetes Sprachpaar.
5. **Antwort übersetzen.** Standard ist die **Taste · / Backtick (&#96;), üblicherweise unter Esc**. Text eingeben und **Enter** drücken: Das Ergebnis wird übersetzt und kopiert, anschließend manuell im Spiel einfügen. Unter Ausgabeübersetzung Zielsprache und Stil wählen. Zum Ändern der Tastenkombination das Feld anklicken, die neue Kombination drücken und speichern. Empfangs- und Ausgabesprache sind getrennt.
6. **Spiel-Chatfenster übernehmen.** Zuerst Fenster und Kanäle in WoW einstellen. Bei laufender EXE `/bl windows` ausführen oder in `/bl settings` die Chatfenster synchronisieren. Nach vollständigem Empfang werden die Tabs automatisch **ersetzt**; kein zusätzlicher Import nötig. Das Addon muss aktiviert sein.


App und Addon aus demselben Paket verwenden. Um chinesischen Chat in eine andere Sprache zu übersetzen, bei aktivem chinesischem Addon-Filter `/bl cn off` ausführen.

### Windows SmartScreen

Windows kann für diese unsignierte Version „Der Computer wurde durch Windows geschützt“ anzeigen. Dies ist die Signatur- und Download-Reputationsprüfung von Microsoft Defender SmartScreen. Neue oder unsignierte Dateien können unbekannt sein; eine Signatur garantiert nicht, dass die Warnung sofort verschwindet. Siehe Microsofts offizielle Erklärung. [Microsoft Learn](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation)

## Dokumentation

[Verbindungen](docs/PROVIDERS.md) · [Lokale Modelle](docs/LOCAL_TRANSLATION.md) · [Chat-Tabs](docs/CHAT_TABS.md) · [Aktionen und Blockieren](docs/MESSAGE_ACTIONS_AND_BLOCKLIST.md) · [Skins](docs/SKINS.md) · [OCR](docs/OCR_FALLBACK.md) · [Build](docs/BUILDING.md)

Detaillierte Anleitungen und Projekthinweise sind standardmäßig auf Englisch. Übersetzte READMEs sind oben verlinkt.

## Datenschutz und Lizenz

Lokale Übersetzung läuft auf deinem PC. Online-Verbindungen senden Text an den eingestellten Anbieter; dessen Bedingungen und Gebühren gelten. Protokolle können Spielernamen und private Nachrichten enthalten. Siehe [Datenschutz](PRIVACY.md).

Der Projektcode steht unter [MIT](LICENSE). Abhängigkeiten, Modelle und Terminologie behalten ihre eigenen Bedingungen und Quellenangaben: [Drittanbieterhinweise](THIRD_PARTY_NOTICES.md). World of Warcraft und Blizzard Entertainment sind Marken ihrer jeweiligen Rechteinhaber.

Alle öffentlichen Downloads und Funktionen sind ohne Spende verfügbar. Kontakt bei Fragen zum Projekt, Datenschutz oder zu Lizenzen: [@erensm1th](https://github.com/erensm1th).
