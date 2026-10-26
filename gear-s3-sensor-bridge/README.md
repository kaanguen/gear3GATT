# Gear S3 Sensor Bridge

Native C-Anwendung für **SM-R760**, **Tizen 4.0.0.4**, Firmware
**R760XXU2ESF2**. Das Projekt enthält Sensorerfassung, BLE-Peripheral/GATT-Server,
Binärprotokoll und eine einfache EFL-Oberfläche. Kein Unity-Code.

## Aktueller Zustand

Die lokal vorhandene moderne VS-Code-SDK-Installation enthält **keine
Wearable-4.0-Entwicklungsplattform und keinen passenden ARM-Compiler**.
Die gemeldete SDK-Fehlermeldung ist deshalb noch nicht durch einen Pfadwechsel
behebbar. Details und die vorbereitete Lösung stehen in [SETUP.md](SETUP.md).

Das Projektmanifest bleibt bei API **4.0**. Studio-Version und Uhren-Firmware
sind unterschiedliche Versionen; im offiziellen Archiv wurde kein Studio 4.4
gefunden. Der Studio-4.5-Paketkatalog enthält die benötigten Wearable-4.0-Pakete.

Native BLE-Typen und Callback-Signaturen wurden anhand originaler
Wearable-4.0-C-Header korrigiert. Die Unterstützung der konkreten Samsung-Firmware
wird zur Laufzeit geprüft; ein dokumentiertes API allein beweist sie nicht.
Es wurden **keine Builds, SDK-/App-Installationen, Signierungen oder Gerätetests**
ausgeführt. Quelltextkorrekturen sind noch nicht kompiliert.

## Einstieg

```sh
python3 gear-s3-sensor-bridge/tools/check_sdk.py
```

Aus dem übergeordneten Arbeitsordner ausführen, oder den gleichnamigen
lesenden VS-Code-Task verwenden. Kein SDK-Programm wird dabei gestartet.
Automatisches CMake-Konfigurieren beim Öffnen wurde im Arbeitsordner abgeschaltet.
CMake baut die Anwendung nur mit einem Wearable-4.0-ARM-Rootstrap. Portable
Protokolltests können später separat gebaut werden, ohne Tizen-Bibliotheken zu benötigen.

## Dateien

- `src/sensors.*`: 50-ms-Bewegungslistener, HRM mit Sensor-Standardrate,
  Verfügbarkeit/Berechtigung und Gültigkeit.
- `src/ble_server.*`: Advertising, GATT, Read/Write, Notifications und Fehlerzustände.
- `src/protocol.*`: feste UUID-unabhängige Paketkodierung und Command-Parser.
- `src/ui.*`: Start/Stopp, Funkstatus und Sensoranzeige.
- `src/main.c`: Übertragungszeitplan und App-Lifecycle.
- `tests/protocol_test.c`: vorbereitete portable Tests; noch nicht gebaut/ausgeführt.
- `cmake/tizen-wearable-4.cmake`: explizite ARM-Cross-Konfiguration.
- [SETUP.md](SETUP.md): SDK-Diagnose, Architektur, Signierung und Installation.
- [PROTOCOL.md](PROTOCOL.md): UUIDs und Wireformat für die spätere Gegenstelle.
- [VALIDATION.md](VALIDATION.md): Belege, bereits korrigierte Fehler und offene Tests.

## Laufzeitgrenzen

Die App arbeitet im Vordergrund. Bei App-Pause werden Messung, Listener, Timer
und BLE-Ressourcen beendet. Nach Resume muss die Messung erneut gestartet werden.
Keine unbegrenzte Hintergrundausführung oder Zustellung aller Notifications wird
versprochen. „Überträgt“ bedeutet eine erfolgreiche lokale Notification-Rückmeldung
innerhalb der letzten zwei Sekunden, keine Bestätigung durch den Empfänger.

Beim ersten lokalen Start kann Tizen die Gesundheitsberechtigung anfragen.
Nach dem Dialog nochmals Start drücken. Bei Ablehnung bleiben Bewegungssensoren
nutzbar, HRM wird nicht geöffnet. Remote-Start löst keinen Berechtigungsdialog aus.

Vor einer Installation bleibt die Samsung-Wearable-Signierung zu klären:
Neue offizielle Wearable-Zertifikate werden laut einer Samsung-Forenantwort
vom Mai 2026 nicht mehr ausgestellt. Vorhandene passende Zertifikate können
entscheidend sein; siehe die Quellen und Einschränkungen in SETUP.md.
