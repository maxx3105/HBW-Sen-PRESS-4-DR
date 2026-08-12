# CLAUDE.md — HBW-Sen-PRESS-DR

Kontext-Übergabedatei für zukünftige Sessions mit Claude. Kompakter Stand, kein Tutorial.

---

## Projekt

Umbau von **HB-UNI-Sen-PRESS** (jp112sdl, Funk/AskSinPP) zu **HBW-Sen-PRESS-DR**
(HomeMatic Wired / RS485, Hutschienenmontage). Basis-Framework:
[ThorstenPferdekaemper/HBWired](https://github.com/ThorstenPferdekaemper/HBWired).

Zweck: Hutschienenmodul mit 4 Analogkanälen zur Auswertung hydraulischer
Drucksensoren (0,5 V–4,5 V Ausgang), wahlweise 0,5 MPa oder 1,2 MPa Sensortyp
pro Kanal, per CCU/RaspberryMatic konfigurierbar.

**Wichtige Abgrenzung:** HB (Funk/CC1101) ≠ HBW (Wired/RS485) — nicht verwechseln.

---

## Hardware

Eigenes PCB vorhanden (KiCad-Schaltplan `HBW-Sen-PRESS-4_Platine1`), Rev 1.0:

- **ATmega328P** — als Zielplattform festgelegt (07.08.2026). Mit 4 Kanälen
  (A0..A3) genügt jedes Gehäuse; das TQFP/MLF wäre nur für A6/A7 nötig, die es
  im DIP nicht gibt. Konsequenz aus dem einen UART: entweder Bus über HW-UART
  **oder** Debug über USB, nie beides.
- **MAX487E** RS485-Transceiver
- **MC34063AD** Step-Down (24 V-Bus → 5 V)
- Sensor-Anschluss J2 (8 Kanäle auf der Platine, im CRMB2-Gehäuse nur 4
  herausgeführt), RS485-Terminal J5, 24V-Terminal J4, ISP J6
- Die Platine wird noch überarbeitet; die Beschaltung der Sensoreingänge ist
  daher hier bewusst nicht beschrieben.

---

## Aktueller Software-Stand (v0.04)

Struktur (Sketch liegt im gleichnamigen Unterordner, damit arduino-cli und
Arduino-IDE ihn direkt bauen — `files.zip` konserviert den v0.02-Stand):

```
HBW-UNI-Sen-PRESS/
├── HBW-Sen-PRESS-DR/          <- Sketch-Ordner
│   ├── HBW-Sen-PRESS-DR.ino
│   ├── HBWAnalogPRESS.h
│   └── HBWAnalogPRESS.cpp
├── hbw-sen-press-dr.xml       <- CCU-Gerätedefinition
├── README.md, BUGFIXES.md, CLAUDE.md
└── files.zip                  <- Archiv v0.02
```

Bauen:
`arduino-cli compile --fqbn arduino:avr:nano:cpu=atmega328 HBW-Sen-PRESS-DR`

Beide Build-Varianten kompilieren (arduino:avr:nano, ATmega328P, 4 Kanaele):
Debug 13032 B / 453 B RAM.

### v0.03: XML von Funk- auf Wired-Syntax umgebaut

Die XML stammte aus der AskSin-Vorlage (HB-UNI-Sen-PRESS) und war an drei
Stellen nicht lauffähig — Details mit Belegstellen in `BUGFIXES.md`:

1. Event-Frame `type="0x53" channel_field="11"`, Payload `12.0` → HBWired
   sendet `0x69` ('i'), Kanal @10, Payload @11. Die CCU hätte den Messwert nie
   zugeordnet.
2. `Sensortyp` über `interface="config" list="1"` → hs485d schreibt das Byte
   nie, jeder Kanal bleibt dauerhaft deaktiviert. Jetzt `interface="eeprom"`.
3. `count_from_sysinfo` gibt es im Wired-Zweig nicht → festes `count`
   (v0.03 noch 8, seit v0.04 4). Belege siehe „Geklärt" unten.

Ergänzt: `LEVEL_GET` (`#S`) + `<get>`, `OWN_ADDRESS` an 0x03FC.

### v0.03: Code

- `delay(2)` im ADC-Sampling entfernt (blockierte `loop()` 8 ms → Frame-Verlust
  auf SoftwareSerial). Inkrementelles Sampling wie `HBWAnalogIn`, aber mit
  Member- statt `static`-Akkumulator (die Lib teilt sie über alle Kanäle).
- `sendInfoMessage()`-Rückgabewert wird ausgewertet (BUS_BUSY → Wert bleibt offen).
- Sendelogik vom Messintervall entkoppelt.
- OFFSET mit Bias 127 statt 0xFF-Sonderfall.

### v0.04

Kanalzahl auf **4** festgelegt (A0..A3), Firmware-Version `0x0004`,
XML `count="4"` + `cond_op="GE" 0x0004`.

### EEPROM-Layout

`HBWDevice::readConfig()` liest die Struktur ab EEPROM 0x01, struct-Offset 6
landet also auf 0x07. Kanal-Config 8 Byte je Kanal ab **0x07**
(Kanal 2..4: 0x0F, 0x17, 0x1F):

```
+0: send_delta_value     (1 Byte, 0.01 bar)
+1: offset               (1 Byte, Bias 127: 0=-1.27bar, 127=0.00bar, 254=+1.27bar)
+2-3: send_max_interval  (2 Byte, little endian, s)
+4-5: send_min_interval  (2 Byte, little endian, s)
+6: update_interval      (1 Byte, s)
+7: pressure_sensor_type (1 Byte: 0=aus, 1=1.2MPa, 2=0.5MPa)
0x3FC-0x3FF: OWN_ADDRESS (E2END-3 beim 328P)
```

Layout ist über drei `static_assert` im `.ino` an die XML gekoppelt — ein
`address_step`-Fehler wie in v0.01 bricht jetzt den Build.

---

## Geklärt (07.08.2026)

- **ADC-Referenz:** VCC-Referenz ist hier die *genauere* Wahl, nicht der
  Kompromiss. Die 0,5–4,5-V-Sensoren sind ratiometrisch — ihr Ausgang skaliert
  mit der Versorgung. Hängen Sensor und ADC am selben 5-V-Rail, kürzt sich die
  Drift des MC34063AD heraus. Die interne 1,1-V-Referenz wäre bei 4,5 V
  Eingang ohnehin nicht nutzbar und würde die Ratiometrie zerstören.
  Bedingung: Sensor und MCU aus **derselben** Schiene versorgen.
- **Kanalanzahl: 4** (A0..A3), festgelegt 09.08.2026 — das CRMB2-Gehäuse führt
  nicht mehr heraus. `NUMBER_OF_CHAN` und `count` in der XML müssen
  übereinstimmen; nicht bestückte Kanäle in der CCU auf
  `Sensortyp = NICHT_BELEGT`.
- **`count_from_sysinfo` ist im Wired-Zweig nicht nutzbar** — zweifach belegt:
  `strings $(which hs485d)` findet weder `count_from_sysinfo` noch einen Ersatz
  (`rfd` dagegen schon, samt `SYSINFO_CH_A`/`_CH_B`); und am Bus legt die CCU
  ohne `count` **null Kanäle** an, obwohl Typ und Firmware korrekt erkannt
  werden. Mehr Kanäle bräuchten einen zweiten Gerätetyp mit eigenem Typ-Byte
  (Muster: `hbw_lc_rgbww_3.xml` 0xD2 / `_6.xml` 0xD1). Frei wäre ab 0x53
  (0x51/0x52 sind HMW-WSE-SM bzw. HMW-WSTH-SM).
- **Firmware-Version:** Code `0x0004`, XML verlangt `GE 0x0004`. v0.01/v0.02
  waren wegen der XML-Fehler ohnehin nie lauffähig.
- **Debug-Plattform:** Mit der 328P-Festlegung entfällt das 644P-Testboard.
  Debug läuft über den vorhandenen Software-Serial-Modus (RS485 auf
  SoftwareSerial, Debug auf USB); Produktivbetrieb mit `USE_HARDWARE_SERIAL`.

## Offene Fragen / Nächste Schritte

1. **Platine wird überarbeitet.** Solange das läuft, bleibt die Beschaltung der
   Sensoreingänge hier bewusst undokumentiert — erst danach festhalten, ob die
   Firmware einen Korrekturfaktor braucht oder das Signal unverändert am ADC
   ankommt.
2. **Am Bus bereits bestätigt** (08.08.2026, mit v0.03/`count="8"`): Announce
   korrekt, Discovery findet das Modul, Interrogation läuft komplett durch
   ('h'→50 01, 'v'→FW, 'n'→Serial, 'R'/'W' EEPROM), Gerät erscheint in der CCU
   mit allen deklarierten Kanälen, Funktionstest grün. Option-IDs mit Punkt
   (`1.2MPa`, `0.5MPa`) wurden dabei nicht beanstandet.

   **Noch offen am Bus:**
   - Der erste `'i'`-Frame mit echtem Messwert — bisher stand jeder Kanal auf
     `NICHT_BELEGT`, dann kehrt `loop()` sofort zurück und es wird nichts
     gesendet (verifiziert: im Trace kein einziger 0x69-Frame)
   - `Sensortyp`-Umschaltung: schreibt hs485d das EEPROM-Byte wirklich?
   - Timing SEND_MIN/MAX_INTERVAL im Zusammenspiel
   - OFFSET-Wirkung mit bekanntem Referenzdruck
   - Nach dem Umstieg auf 4 Kanäle: XML tauschen, Gerät löschen und **neu
     anlernen** (Kanalstruktur steckt in der regadom-DB)
3. **XML-Deployment:** Beim Austausch der XML auf einer CCU, die das Gerät
   schon kennt: XML löschen → Neustart → neue XML → Neustart → Posteingang →
   übernehmen. Sonst bleibt die alte Definition im Cache.
4. **Sprache:** Repo ist durchgängig deutsch (README, BUGFIXES, CLAUDE.md,
   Code-Kommentare). Debug-Ausgaben und Bezeichner bleiben englisch/technisch.

---

## Arbeitskonventionen für diese Session/Repo

- Gezielte, benannte Patches statt große Rewrites.
- Vor Code-Änderungen: Diskussion/Bestätigung des Ansatzes.
- Vorschläge nur auf Basis der tatsächlich hochgeladenen Dateien, keine
  Annahmen über nicht gezeigten Code.
- EEPROM wird während der Entwicklung bei jedem ISP-Flash bewusst gelöscht
  (sauberer Zustand zwischen CCU-Pairing-Zyklen).
