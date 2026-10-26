# Prüfstand und offene Hardwaretests

## Nur lesend geprüft am 26.10.2026

- Lokale SDK-Verzeichnisse und angebotene Plattformversionen: Wearable 4.0 fehlt.
- Projektmanifest: Wearable, API 4.0, ausschließlich `bluetooth` und `healthinfo`.
- Native BLE- und Sensor-API-Namen gegen Originalheader des Wearable-4.0-ARM-SDKs:
  alle referenzierten `bt_*`, `sensor_*` und `ppm_*`-Funktionen vorhanden.
- Headerherkunft: offizielles Paket
  `wearable-4.0-rs-device.core_0.0.62_macos-64.zip`, gelistet im
  [Studio-3.3-Katalog](https://download.tizen.org/sdk/tizenstudio/official/snapshots/Tizen_Studio_3.3/pkg_list_macos-64).
  SHA256 der heruntergeladenen Lesekopie wurde gegen den Katalog geprüft:
  `dd08f6bbff367583f0a425c86cdee3b109e5d1fefd9e6b372187c97080ec66c9`.
  Nur ausgewählte Header wurden nach `/tmp` zum Lesen extrahiert; kein SDK installiert.
- `bluetooth_type.h` bestätigt `bt_gatt_h`, `bt_advertiser_h` sowie die Reihenfolge
  `bool response_needed, int offset` im Schreib-Callback. Die alten API4-.NET-Bindings
  haben dort eine abweichende Reihenfolge und sind keine zuverlässige C-Signaturquelle.
- `bluetooth.h` bestätigt den Read-Response-Vertrag: vollständiger Wert plus Offset;
  der bisherige Code hatte Pointer und Länge bereits um Offset verschoben.
- SDK-Diagnoseskript ausgeführt: fehlende Wearable-4.0-Komponenten erkannt.

Dies sind Quelltext-/Dateiprüfungen. Kein Compiler wurde gestartet. Es gibt noch
keinen erfolgreichen Build und keine bestätigte TPK-Installation.

## Im Quelltext korrigiert, noch nicht auf Hardware bestätigt

- Falsche BLE-Handle-Typen und Schreib-Callback-Signatur.
- Read-Offset doppelt angewendet.
- Advertising bereits als aktiv gemeldet, bevor der asynchrone Callback Erfolg meldet.
- Übertragung bereits bei Abonnement angezeigt: jetzt letzter erfolgreicher
  Sensor-Notification-Callback innerhalb von zwei Sekunden. Dies ist eine lokale
  Stack-Rückmeldung, keine Empfangsbestätigung der späteren Unity-Anwendung.
- Höchstens eine ausstehende Notification je Characteristic; übergroße Werte werden abgewiesen.
- Sensorereignisse mit zu wenigen Werten, NaN/Infinity oder Genauigkeit BAD
  invalidieren die vorherigen Werte. Abgelaufene Messwerte verschwinden auch aus der UI.
- HRM verwendet die Sensor-Standardrate und prüft die Gesundheitsberechtigung.
  Am lokalen Startknopf wird eine noch offene Berechtigung angefragt. Nach dem
  Dialog nochmals Start drücken; eine verspätete Antwort startet keine Messung.
- Device-orientierte Bewegungsachsen werden explizit konfiguriert.
- Bluetooth-Aus schließt den lokalen Server; der Vordergrund-Timer baut ihn erneut auf.
- CMake trennt portable Tests vom Tizen-Ziel und lehnt versehentliche Host-App-Builds ab.

## Vorbereitete Tests, nicht ausgeführt

`tests/protocol_test.c`: vollständiger 20-Byte-Goldenvektor einschließlich
negativer Grenzwerte und Pufferwächter, ungültige Sensorkanäle, HR-Gültigkeit,
Command-Längen/Versionen/Werte, unveränderte Ausgabe bei abgelehnten Commands,
Statusfehler und Sequenz-/Zeitstempel-Grenzwerte.
Das ist kein Nachweis für Callback-, Lifecycle- oder Funkverhalten.

## Erforderliche spätere Tests auf SM-R760 / R760XXU2ESF2

1. GATT-Server-Feature und Advertising tatsächlich unterstützt? Kein Ersatztransport
   wird automatisch gewählt. Fehler 8 kennzeichnet fehlende API-/Firmwareunterstützung.
2. Alle UUIDs, CCCD-Abonnements, Read-Offsets, 4-Byte-Writes und ATT-MTU 23 prüfen.
3. Echte Sensorwerte, Einheiten, physische Achsenrichtung, HR-Signalqualität und
   tatsächlich erreichbare 20 Hz messen. Die Empfangszeit des Callbacks ist kein
   exakter Hardwaremesszeitpunkt; Beschleunigung und Gyroskop sind nicht synchronisiert.
4. Bluetooth aus/an, Sensor-/Berechtigungsfehler, Abbruch, Reconnect und erneutes
   Abonnieren prüfen. Die Anwendung ist für eine Gegenstelle vorgesehen.
5. Während Messung/Permission-Dialog pausieren, Home/Back/Beenden betätigen;
   keine Listener oder Werbeinstanzen dürfen weiterlaufen.
6. Display aus: Standard-Pausepolitik und App-Pause können die Erfassung stoppen.
   Dieses Projekt deklariert keine Hintergrundkategorie und fordert keine Wake-Locks an.
   Nach App-Pause muss die Messung erneut gestartet werden.
   Unbegrenzte Hintergrundausführung ist kein unterstützter Modus.
7. Langzeitlauf, Sequenzüberlauf, Datenverlust und Energiebedarf beobachten.
   Notifications garantieren keine verlustfreie Zustellung.

Die [offizielle Sensor-Listener-Dokumentation](https://docs.stg.tizen.org/application/native/api/mobile/4.0/group__CAPI__SYSTEM__SENSOR__LISTENER__MODULE.html)
beschreibt Intervallgrenzen und die Standard-Pausepolitik. Die API-Oberfläche
wurde zusätzlich mit den originalen Wearable-4.0-Headern geprüft; Firmwareverhalten
bleibt davon getrennt.
