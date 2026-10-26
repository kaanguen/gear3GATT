# Entwicklungsumgebung für die Gear S3

Zielgerät: SM-R760, Tizen **4.0.0.4**, Software **R760XXU2ESF2**.
Das Manifest benötigt Wearable API **4.0**, Zielarchitektur **ARMv7**.
Die Versionsnummer der IDE ist davon unabhängig.

## Befund vom 26.10.2026

Die Meldung „Tizen SDK nicht heruntergeladen“ lässt sich durch fehlende
Zielpakete erklären. Lokal ist ein Baseline-SDK der neuen VS-Code-Erweiterung
unter `/Users/kaan/.tizen-extension-platform/server/sdktools/data` vorhanden.
Die Erweiterung listet Plattformen 8.0, 9.0 und 10.0. Das ARM-Rootstrap für
Wearable 4.0 und der passende ARM-C-Compiler fehlen. Unter
`/Users/kaan/tizen-studio` liegt kein Legacy-SDK.

Ein SDK-Pfad allein oder ein Wechsel des Manifests auf Tizen 10 behebt diesen
Fehler nicht. Ein aktuelles SDK ist kein Ersatz für die alten Header und
Bibliotheken. Die aktuelle Erweiterung wird nicht durch erfundene
Konfigurationsschlüssel auf einen nicht vorhandenen Wearable-4.0-Support gesetzt.

Das offizielle [Installerarchiv](https://download.tizen.org/sdk/Installer/) und
die [Snapshotliste](https://download.tizen.org/sdk/tizenstudio/official/snapshot.info)
enthalten Studio 4.1.1 und 4.5, aber keinen Eintrag für Studio 4.4.
Eine angeblich passende „4.4.0“-Installation wird deshalb nicht vorausgesetzt.
Der [Studio-4.5-Paketkatalog für macOS](https://download.tizen.org/sdk/tizenstudio/official/snapshots/Tizen_Studio_4.5/pkg_list_macos-64)
enthält noch:

- `WEARABLE-4.0-NativeAppDevelopment-CLI`, Version 0.2.1;
- `wearable-4.0-rs-device.core`, Version 0.0.64;
- `NativeToolchain-Gcc-6.2` und dessen Abhängigkeiten.

Die Zielpakete liegen unter dem offiziellen `/binary/`-Verzeichnis,
nicht unter dem Snapshotverzeichnis. Die Erreichbarkeit des
[ARM-Rootstrap-Archivs](https://download.tizen.org/sdk/tizenstudio/official/binary/wearable-4.0-rs-device.core_0.0.64_macos-64.zip)
wurde per HTTP-Header geprüft. Das belegt die Verfügbarkeit einer Datei,
keine erfolgreiche Installation der gesamten Toolchain.

Dieser Mac ist `arm64` (Apple Silicon). Die historischen SDK-Werkzeuge wurden
für x64 bereitgestellt; die damaligen offiziellen
[Systemanforderungen](https://docs.stg.tizen.org/application/tizen-studio/setup/prerequisites/)
nennen für macOS x64. Funktionsfähigkeit auf diesem Mac ist noch ungeprüft.
Eine Übersetzungsschicht oder eine geeignete andere Entwicklungsmaschine kann
erforderlich sein. Es wurde nichts davon installiert oder gestartet.

## Jetzt gefahrlos prüfen

Im Arbeitsordner `/Users/kaan/Tizen`:

```sh
python3 gear-s3-sensor-bridge/tools/check_sdk.py
```

Oder in VS Code über „Tasks: Run Task“ →
„Tizen: Wearable-4.0-SDK pruefen (nur lesen)“.
Das Skript liest ausschließlich Dateien und startet keine SDK-Programme,
keinen Compiler, kein SDB und kein Gerät. Exitcode 2 bedeutet fehlende
Zielpakete; Exitcode 0 bedeutet nur, dass die erwarteten Dateien vorhanden sind.
Ein bestimmtes SDK lässt sich mit `--sdk /absoluter/sdk/pfad` prüfen.

## Nächster Schritt nach ausdrücklicher Installationsfreigabe

Eine separate Legacy-SDK-Installation mit Wearable-4.0-ARM-Rootstrap und
GCC-6.2-Toolchain vorbereiten; die vorhandene Baseline-Installation dabei erhalten.
Die archivierte Paketliste muss als Grundlage dienen, einschließlich der
Abhängigkeiten und SHA256-Prüfsummen. Nicht nur einzelne Header in das moderne
SDK kopieren. Zuerst Host-Kompatibilität und Vollständigkeit klären.

Danach `SB_TIZEN_SDK` auf das Legacy-SDK-Verzeichnis setzen und die Diagnose
erneut ausführen. Der Diagnosepfad ist unabhängig von der SDK-Verwaltung der
aktuellen VS-Code-Erweiterung.

## Bauen – derzeit ausdrücklich nicht ausgeführt

Ein CMake-Toolchainfile liegt unter `cmake/tizen-wearable-4.cmake`.
Es verlangt `SB_TIZEN_SDK` und `SB_TIZEN_CC` (den absoluten Pfad zum
SDK-Compiler `arm-linux-gnueabi-gcc`). Zusätzlich braucht CMake ein ausführbares
Host-`pkg-config`; dessen Suchpfade werden auf das Wearable-Rootstrap begrenzt.
Diese Konfiguration ist noch nicht durch einen Build validiert.

Erst nach separater Buildfreigabe:

```sh
cmake -S gear-s3-sensor-bridge -B gear-s3-sensor-bridge/build-device \
  -DCMAKE_TOOLCHAIN_FILE=cmake/tizen-wearable-4.cmake
cmake --build gear-s3-sensor-bridge/build-device
```

Für die portablen C-Protokolltests ist kein Tizen-SDK nötig. Auch sie müssen
kompiliert werden und wurden daher noch nicht ausgeführt:

```sh
cmake -S gear-s3-sensor-bridge -B gear-s3-sensor-bridge/build-tests \
  -DSB_BUILD_APP=OFF -DSB_BUILD_TESTS=ON
cmake --build gear-s3-sensor-bridge/build-tests
ctest --test-dir gear-s3-sensor-bridge/build-tests --output-on-failure
```

## Signierung und lokale Installation

Für die reale Samsung-Uhr benötigt der bisherige offizielle Entwicklerworkflow
ein Author-Zertifikat und ein zur Uhr/DUID passendes Samsung-Distributor-Zertifikat.
Vorhandene Zertifikate einschließlich privater Schlüssel sichern und deren
Gültigkeit und Gerätezuordnung prüfen. Keine privaten Schlüssel ins Projekt legen.
Generische Tizen- oder Samsung-TV-Zertifikate sind kein bestätigter Ersatz.

Die bisherige Anleitung zur Neuausstellung ist heute nicht mehr ausreichend:
Eine [Antwort im Samsung-Entwicklerforum vom 20.05.2026](https://forum.developer.samsung.com/t/detail?id=10203-Request-a-Tizen-development-certificate-%E2%80%93-SM-R800)
erklärt, dass offizielle Wearable-Zertifikatsausstellung nach der Unterstützung
bis März 2026 nicht mehr angeboten wird. Das ist eine Auskunft im offiziellen
Forum, keine Prüfung deines Zertifikatsbestands. Neue lokale Installation wird
hier deshalb nicht als gesichert versprochen.

Wenn eine gültige Signierkette vorhanden ist, folgt nach Buildfreigabe das
Zusammenstellen eines nativen TPK mit Manifest und ARM-Binärdatei, Signieren mit
dem vorhandenen Profil und – erst nach Installations-/Gerätefreigabe – Verbinden
über Device Manager/SDB und Installation des signierten TPK. Ein CMake-Executable
allein ist noch kein installierbares TPK.

Die hier beschriebenen Befehle und Schritte sind Anleitung, keine ausgeführten
Builds, Installationen, Signierungen oder Gerätetests.
