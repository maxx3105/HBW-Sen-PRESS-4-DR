# HBW-Sen-PRESS-DR

HomeMatic-Wired-Drucksensormodul für Hutschienenmontage

## Überblick

HomeMatic-Wired-Gerät (RS485) zur Überwachung hydraulischer Drücke mit
Industrie-Drucksensoren. Es stellt 4 analoge Sensoreingänge bereit (A0–A3).
Die Platine führt zwar 8 ADC-Pins, das CRMB2-Gehäuse gibt aber nur 4
Sensoranschlüsse heraus. Nicht belegte Kanäle werden in der CCU über
`Sensortyp = NICHT_BELEGT` abgeschaltet.

`NUMBER_OF_CHAN` im Sketch und `count` in der XML **müssen** übereinstimmen.
Eine dynamische Kanalzahl über `count_from_sysinfo` gibt es im Wired-Zweig
nicht: `hs485d` implementiert das Attribut nicht (im Binary weder
`count_from_sysinfo` noch ein Äquivalent, `rfd` dagegen schon). Ohne `count`
legt die CCU null Kanäle an. Mehr Kanäle bräuchten einen zweiten Gerätetyp mit
eigenem Typ-Byte, wie bei `HBW-LC-RGBWW-3` / `-6`.

### Basiert auf:
- **HBWired** von Thorsten Pferdekaemper: https://github.com/ThorstenPferdekaemper/HBWired
- **HB-UNI-Sen-PRESS** von jp112sdl: https://github.com/jp112sdl/HB-UNI-Sen-PRESS

## Aufbau des Repositories

```
HBW-Sen-PRESS/
├── HBW-Sen-PRESS-DR/            Arduino-Sketch (Ordnername muss zur .ino passen)
│   ├── HBW-Sen-PRESS-DR.ino
│   ├── HBWAnalogPRESS.h
│   └── HBWAnalogPRESS.cpp
├── hbw-sen-press-dr.xml         CCU-Gerätedefinition (hs485types)
├── HBW-Sen-PRESS-4_Platine1/    KiCad-Projekt Hauptplatine (+ Gerber)
├── HBW-Sen-PRESS-4_Platine2/    KiCad-Projekt zweite Platine (+ Gerber)
├── BUGFIXES.md                  was in v0.01–v0.03 defekt war und warum
└── CLAUDE.md                    kompakter Projektstand / Übergabenotizen
```

## Hardware

### Unterstützte Sensoren
- **0,5 MPa** Hydraulik-Drucksensor (0–5 bar / 0–72,5 PSI)
- **1,2 MPa** Hydraulik-Drucksensor (0–12 bar / 0–174 PSI)

Beide liefern ein Analogsignal von **0,5 V bis 4,5 V**:
- 0,5 V = 0 bar
- 4,5 V = Endwert (5 bzw. 12 bar je nach Sensortyp)

### Bauteile
- **ATmega328P-A** Mikrocontroller (alternativ Arduino Nano)
- **MAX487E** RS485-Transceiver
- **MC34063AD** Step-Down-Wandler (24 V Bus → 5 V)
- Analogeingänge: A0–A3 in Verwendung (die Platine führt A0–A7, das
  CRMB2-Gehäuse gibt nur 4 heraus)

### Merkmale der Platine
- Hutschienenmontage
- RS485-Busanschluss (A, B, GND, +24 V)
- Schraubklemmen für die Sensoren
- ISP-Programmierstecker
- Status-LED
- Taster für Werksreset

## Pinbelegung

| Pin | Funktion |
|-----|----------|
| A0–A3 | Analogeingänge der Drucksensoren (A4–A7 auf der Platine vorhanden, ungenutzt) |
| D2 | RS485 TX (bzw. TXD bei SoftwareSerial) |
| D3 | RS485 TXEN |
| D4 | RS485 RX (nur bei SoftwareSerial) |
| D8 | Taster für Werksreset |
| D13 | Status-LED |

## Funktionsumfang

### Konfiguration je Kanal (in CCU/FHEM)

| Parameter | Bereich | Vorgabe | Bedeutung |
|-----------|---------|---------|-----------|
| **Sensortyp** | 0.5MPa / 1.2MPa / NICHT_BELEGT | NICHT_BELEGT | Sensortyp wählen oder Kanal abschalten |
| **SEND_DELTA_VALUE** | 0,0–2,54 bar | 0,1 bar | Sendet, sobald sich der Druck um diesen Betrag ändert |
| **OFFSET** | −1,27 bis +1,27 bar | 0,0 bar | Kalibrier-Offset (vorzeichenbehaftet, mit Bias 127 gespeichert) |
| **UPDATE_INTERVAL** | 1–254 s | 30 s | Pause zwischen zwei Messreihen |
| **SEND_MIN_INTERVAL** | 10–3600 s | 30 s | Mindestabstand zwischen zwei Sendungen |
| **SEND_MAX_INTERVAL** | 10–3600 s | 600 s | Spätestens nach dieser Zeit wird gesendet |

Die Obergrenzen vermeiden den Wert aus lauter Einsen (`0xFF` / `0xFFFF`) — der
markiert eine gelöschte EEPROM-Zelle und wird stattdessen auf die Vorgabe
abgebildet.

### Ablauf

1. **Messung**: alle SAMPLE_INTERVAL (3 s) ein ADC-Wert, gemittelt über
   MAX_SAMPLES (4) — ohne blockierendes `delay()`, damit der RS485-Empfang
   reaktionsfähig bleibt. Nach einer vollständigen Messreihe pausiert der Kanal
   für UPDATE_INTERVAL.
2. **Umrechnung**: ADC → Spannung → Druck (abhängig vom Sensortyp)
3. **Kalibrierung**: OFFSET anwenden
4. **Sendeentscheidung** (in jedem Schleifendurchlauf geprüft, unabhängig vom
   Messzyklus):
   - Mindestens SEND_MIN_INTERVAL seit der letzten Sendung abwarten
   - Senden, wenn sich der Druck um ≥ SEND_DELTA_VALUE geändert hat
   - Nach SEND_MAX_INTERVAL in jedem Fall senden
   - Bei `BUS_BUSY` bleibt der Messwert offen, statt als gesendet zu gelten

Ein vollständiger Messwert entsteht damit alle
`(MAX_SAMPLES − 1) × SAMPLE_INTERVAL + UPDATE_INTERVAL` Sekunden, mit den
Vorgabewerten also alle 39 s. `UPDATE_INTERVAL` ist die Pause *zwischen* den
Messreihen, nicht die Messrate — auf 1 s gestellt misst das Gerät alle 10 s.

## Installation

### 1. Arduino-IDE vorbereiten
```bash
git clone https://github.com/ThorstenPferdekaemper/HBWired
# nach Arduino/libraries/HBWired kopieren
```

### 2. Übersetzen und flashen

Der Sketch liegt im Unterordner `HBW-Sen-PRESS-DR/` (der Ordnername muss dem
`.ino`-Namen entsprechen).

- `HBW-Sen-PRESS-DR/HBW-Sen-PRESS-DR.ino` in der Arduino-IDE öffnen
- Board: **Arduino Nano** (ATmega328P)
- Für Debug: `USE_HARDWARE_SERIAL` auskommentiert lassen
- Für den Produktivbetrieb: `#define USE_HARDWARE_SERIAL` aktivieren
- Flashen über ISP oder Bootloader

Oder aus dem Projektverzeichnis heraus:

```
arduino-cli compile --fqbn arduino:avr:nano:cpu=atmega328 HBW-Sen-PRESS-DR
arduino-cli compile --fqbn arduino:avr:nano:cpu=atmega328 --upload --programmer usbasp HBW-Sen-PRESS-DR
```

Hinweis: Ist die EESAVE-Fuse nicht gesetzt, löscht jeder ISP-Flash das EEPROM —
und damit die Busadresse. Prüfen mit
`avrdude -c usbasp -p m328p -U hfuse:r:-:h` (Bit 3 gesetzt, z. B. `0xDA`,
bedeutet: EEPROM wird gelöscht).

### 3. CCU/RaspberryMatic einrichten
1. `hbw-sen-press-dr.xml` auf die CCU kopieren
2. Gerätedefinition einbinden (Addon bzw. FHEM)
3. Gerät an den RS485-Bus anschließen
4. Anlernen und aus dem Posteingang übernehmen

Wird eine bereits installierte XML ersetzt: alte XML löschen → Neustart → neue
XML einspielen → Neustart → Gerät löschen und neu anlernen. Ändert sich die
Kanalstruktur, reicht ein Übernehmen nicht, das steckt in der regadom-Datenbank.

## EEPROM-Belegung

`HBWDevice` liest die Konfigurationsstruktur ab EEPROM-Adresse `0x01`
(siehe `readConfig()` in HBWired.cpp), Struktur-Offset 6 landet also auf `0x07`.

```
0x00      : Gerätetyp (0x50)
0x01      : Logging-Zeit
0x02-0x05 : Zentralen-Adresse
0x06      : Direct-Link-Flag
0x07      : Kanal 1, Konfiguration (8 Byte)
  +0: send_delta_value
  +1: offset (Bias 127: 0 = −1,27 bar, 127 = 0,00 bar, 254 = +1,27 bar)
  +2-3: send_max_interval (16 Bit, little endian)
  +4-5: send_min_interval (16 Bit, little endian)
  +6: update_interval
  +7: pressure_sensor_type
0x0F/0x17/0x1F : Kanal 2..4, Konfiguration
0x3FC-0x3FF : OWN_ADDRESS (Busadresse, E2END−3 beim 1-kB-EEPROM des 328P)
```

Die Belegung ist über drei `static_assert` in
`HBW-Sen-PRESS-DR/HBW-Sen-PRESS-DR.ino` an die XML gekoppelt — Strukturgröße,
Startoffset der Kanäle und der Abstand zu `OWN_ADDRESS` müssen passen,
sonst bricht der Build.

## Fehlersuche

### Keine Messwerte / alles null
- Verdrahtung prüfen (3-adrig: VCC, GND, Signal)
- Liefert der Sensor tatsächlich 0,5–4,5 V?
- Ist der Kanal freigegeben (`Sensortyp` ≠ `NICHT_BELEGT`)? Bei abgeschaltetem
  Kanal wird weder gemessen noch gesendet.

### Falsche Druckwerte
- Richtigen Sensortyp gewählt (0,5 MPa vs. 1,2 MPa)?
- Zur Kalibrierung den Parameter OFFSET verwenden
- VCC muss *nicht* exakt 5,0 V betragen: Die Sensoren arbeiten ratiometrisch,
  ihr Ausgang skaliert mit der Versorgung. Hängen Sensor und ADC an derselben
  5-V-Schiene, hebt sich die Abweichung des Reglers auf — deshalb wird die
  VCC-Referenz des ADC benutzt und nicht die interne 1,1-V-Referenz.
  Wichtig ist nur, dass Sensor und MCU aus **derselben** Schiene versorgt werden.

### CCU zeigt das Gerät, aber keinen Messwert
- Ist die aktuelle XML installiert? Gerätetyp `0x50`, Frame `INFO_LEVEL` als
  `type="#i" channel_field="10"`, Nutzdaten ab Index `11.0`
- Nach dem Austausch der XML: löschen, Neustart, neue XML, Neustart, Gerät neu
  anlernen

### Gerät kommuniziert nicht
- RS485-Verdrahtung prüfen (A/B nicht vertauscht)
- Busabschluss vorhanden (120 Ω an beiden Enden)?
- TX/RX-Aktivität beobachten
- Im Debug-Modus mitlesen (serieller Monitor, 115200 Baud)

## Debug-Modus

Bleibt `//#define USE_HARDWARE_SERIAL` auskommentiert, läuft RS485 über
SoftwareSerial und die Debug-Ausgabe über USB:

```
HBW-Sen-PRESS-DR v4
Free RAM: 1093 bytes
Channels: 4
ADC Ch0: 329 (1606 mV)
ADC Ch1: 291 (1420 mV)
ADC Ch2: 264 (1289 mV)
ADC Ch3: 268 (1308 mV)
=== Setup complete ===
```

Der ATmega328P hat nur einen UART: entweder Bus über die Hardware-UART **oder**
Debug über USB, nie beides gleichzeitig.

## Lizenz

Creative Commons BY-NC-SA 3.0 AT
http://creativecommons.org/licenses/by-nc-sa/3.0/at/

## Dank an

- Thorsten Pferdekaemper – HBWired-Framework
- Dirk Hoffmann – Beiträge zu HBWired
- jp112sdl – ursprüngliches Konzept HB-UNI-Sen-PRESS
- maxx3105 – Umsetzung HBW-Sen-PRESS-DR

## Änderungsverlauf

### v0.04
- Kanalzahl 8 → 4, passend zu dem, was das CRMB2-Gehäuse herausführt
- Am Bus bestätigt, dass `count_from_sysinfo` im Wired-Zweig nicht nutzbar ist:
  `hs485d` wertet die übrige XML aus (Gerätetyp, Firmware, Geräteparameter),
  legt ohne `count` aber **null** Kanäle an

### v0.03
- **XML gegen das HMW-Frame-Layout neu aufgebaut.** Der `<frames>`-Block war
  noch AskSin-/Funk-Syntax aus HB-UNI-Sen-PRESS (`type="0x53"`,
  `channel_field="11"`, Nutzdaten bei `12.0`). HBWired sendet `0x69` ('i') mit
  dem Kanal in Byte 10 und den Nutzdaten ab Byte 11 — die CCU hätte die
  Info-Nachrichten nie dem Datenpunkt zuordnen können.
- `Sensortyp` von `interface="config" list="1"` (Funk) auf `interface="eeprom"`
  umgestellt — hs485d hätte dieses Byte nie geschrieben, jeder Kanal wäre
  dauerhaft abgeschaltet geblieben.
- `LEVEL_GET`-Frame (`#S`) und `<get>` ergänzt, damit die CCU den Wert abfragen
  kann; HBWired beantwortet 'S' bereits.
- `count_from_sysinfo` durch festes `count` ersetzt; das Kanalzahl-Byte an 0x17
  und sein Padding entfallen, die Kanalkonfiguration beginnt jetzt bei 0x07.
- `OWN_ADDRESS` an 0x03FC ergänzt (E2END−3 des 1-kB-EEPROMs im 328P).
- Blockierendes `delay(2)` aus der ADC-Schleife entfernt — 8 ms je Messung
  reichten aus, um auf SoftwareSerial RS485-Frames zu verlieren.
- Rückgabewert von `sendInfoMessage()` wird ausgewertet; bei `BUS_BUSY` gilt der
  Messwert nicht mehr als gesendet.
- Sendelogik vom Messzyklus entkoppelt.
- OFFSET mit Bias 127 gespeichert, damit −0,01 bar nicht mehr mit der Markierung
  für gelöschtes EEPROM (0xFF) kollidiert.
- EEPROM-Belegung durch `static_assert` abgesichert.

### v0.02 (06.04.2024)
- Doppelte Kanal-Initialisierung behoben
- EEPROM-Adressierung korrigiert (address_step = 8)
- OFFSET im Code umgesetzt
- Timing-Fehler bei send_min_interval behoben
- send_min_interval auf 16 Bit erweitert (war 8 Bit)
- Debug-Ausgabe erweitert

### v0.01 (02.04.2024)
- Erste Fassung auf Basis von HB-UNI-Sen-PRESS
- Grundlegende Druckmessung
- CCU-Anbindung über XML
