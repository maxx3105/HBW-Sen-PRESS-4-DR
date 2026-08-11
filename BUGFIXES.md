# Bug Fixes und Verbesserungen - HBW-Sen-PRESS-DR

## v0.03 → v0.04: Kanalzahl 4, count_from_sysinfo endgültig ausgeschlossen

Das CRMB2-Gehäuse führt nur 4 Sensoranschlüsse heraus, also `NUMBER_OF_CHAN 4`
und `count="4"`. Zuvor wurde geprüft, ob sich die Kanalzahl dynamisch über
`count_from_sysinfo` melden lässt (eine XML/Firmware für 4 und 8 Kanäle).

**Ergebnis: geht nicht.** Belegt auf zwei Wegen:

1. **Binär**, auf der OpenCCU:
```
strings $(which rfd)     | grep -i "count_from\|sysinfo"
   -> SYSINFO, SYSINFO_SWVER, SYSINFO_TYPE, SYSINFO_SERIAL, SYSINFO_CODE,
      SYSINFO_CH_A, SYSINFO_CH_B, count_from_sysinfo, peering_sysinfo_expect_channel

strings $(which hs485d)  | grep -i "count_from\|count.*channel\|channel.*count"
   -> (keine Treffer)
```
`hs485d` kennt zwar `sysinfo` (gemeinsamer Matching-Code mit `rfd`, Meldung
`Matching "sysinfo" attribute … Trying "type" instead`), aber weder
`count_from_sysinfo` noch irgendeinen anders benannten Ersatz. Bei Funk sitzt
die Kanalzahl in der Pairing-Sysinfo (`SYSINFO_CH_A`/`_CH_B`, Byte 23) — das
Wired-Protokoll überträgt so etwas nicht.

2. **Am Bus:** Mit `count_from_sysinfo="23.0:0.4"` statt `count` erkennt die CCU
das Gerät weiterhin korrekt (Typenbezeichnung `HBW-Sen-PRESS`, Firmware 0.03,
Geräteparameter vorhanden), legt aber **null Kanäle** an → „Keine Parameter
einstellbar". Das unbekannte Attribut wird ignoriert, und ohne `count` gibt es
keine Kanäle.

Variable Kanalzahl geht bei Wired deshalb nur über getrennte Gerätetypen mit
eigenem Typ-Byte — so wie eq3 (`hmw_io_4_fm` 0x10 / `hmw_io_12_fm` 0x1B) und
wie im eigenen Bestand `hbw_lc_rgbww_3.xml` (0xD2) / `hbw_lc_rgbww_6.xml` (0xD1).

Merksatz für künftige Fälle: Ob ein CCU-Daemon ein XML-Attribut auswertet, klärt
`strings` auf das Binary — schneller und risikofreier als XML-Tausch am
laufenden System. Dass ein Attribut in keiner XML vorkommt, ist nur ein Indiz.

---

## Übersicht der Änderungen von v0.02 → v0.03

Gemeinsame Ursache der XML-Fehler: Die Datei wurde aus der Funk-XML von
HB-UNI-Sen-PRESS abgeleitet, nicht aus einer Wired-XML. HB (Funk/CC1101) und
HBW (Wired/RS485) haben unterschiedliche Frame-Layouts und Parameter-Interfaces.

Referenzen für den korrigierten Stand:
- `hs485types/hmw_sen_sc_12_dr.xml` (eq3-Original)
- `JP-HB-Devices-addon-fork/.../hs485types/hbw_1w_t10_v1.xml` (loetmeister,
  HBWired-Sensormodul mit analogen Messwerten — direkte Vorlage)
- `HBWired/src/HBWAnalogIn.cpp` (Lib-Referenz für Sampling und Sendelogik)

---

### 🔴 BLOCKER 1: Event-Frame in Funk-Syntax

**Problem:**
```xml
<frame id="PRESSURE_EVENT" direction="from_device" event="true"
       type="0x53" channel_field="11">
    <parameter type="integer" index="12.0" size="2.0" param="UNI_PRESSURE"/>
</frame>
```

`0x53` ist der MEASURE_EVENT-Typ eines AskSin-Funkgeräts. HBWired sendet aber:

```cpp
// HBWired.cpp, sendInfoMessage()
txFrame.data[0] = 0x69;         // 'i'
txFrame.data[1] = channel;      // Sensornummer
memcpy(&(txFrame.data[2]), data, length);
```

HMW-Frame-Layout: 4 Byte Ziel + 1 Byte Control + 4 Byte Absender = Index 0..8,
danach Kommando @9, Kanal @10, Payload ab @11.

**Auswirkung:** Die CCU ordnet die Info-Nachrichten keinem Datenpunkt zu. Der
Messwert wäre nie angekommen — auch bei fehlerfreiem Bus und Pairing.

**Fix:** ✅
```xml
<frame id="INFO_LEVEL" direction="from_device" event="true"
       type="#i" channel_field="10">
    <parameter type="integer" index="11.0" size="2.0" param="UNI_PRESSURE"/>
</frame>
```

---

### 🔴 BLOCKER 2: Sensortyp über `interface="config"`

**Problem:**
```xml
<physical type="integer" interface="config" list="1" index="+7.0" size="1"/>
```

`interface="config"` mit `list=` ist AskSin-Funk. HM485/hs485d kennt keine
Lists — der Parameter wird nie ins EEPROM geschrieben.

**Auswirkung:** `pressure_sensor_type` bleibt 0xFF → `afterReadConfig()` setzt
auf 0 → **jeder Kanal dauerhaft deaktiviert**, unabhängig von der CCU-Auswahl.

**Fix:** ✅
```xml
<physical size="1.0" type="integer" interface="eeprom">
    <address index="+7.0"/>
</physical>
```

---

### 🟠 BLOCKER 3: `count_from_sysinfo` existiert im Wired-Zweig nicht

**Problem:** `count_from_sysinfo="23.0:0.3"` kommt in keiner der 15 eq3- und
21 loetmeister-Wired-XMLs vor — nur in rftypes (Funk). Zusätzlich fassen die
3 Bit nur Werte 0..7, die angestrebten 8 Kanäle passen gar nicht hinein.

**Fix:** ✅ Festes `count="8"`. Damit entfallen das Kanalzahl-Byte an 0x17, die
`EEPROM.write(0x17, ...)` im Sketch und beide Padding-Blöcke; die Kanalconfigs
rücken von 0x23 auf 0x07 (direkt hinter den Device-Master-Block, wie in der
Referenz-XML).

---

### 🟠 Fehlend: LEVEL_GET und OWN_ADDRESS

- Ohne `<frame id="LEVEL_GET" direction="to_device" type="#S" channel_field="10"/>`
  und `<get request="LEVEL_GET" response="INFO_LEVEL"/>` kann die CCU den Wert
  nicht aktiv abfragen. HBWired beantwortet 'S' bereits (`HBWired.cpp`, `case 'S'`).
- `OWN_ADDRESS` fehlte komplett. Ergänzt an `0x03FC` — beim ATmega328P ist
  E2END = 0x3FF, die Busadresse liegt in den letzten 4 EEPROM-Bytes.
  Ohne diesen Block lässt sich die Busadresse nicht über die CCU setzen.

---

### 🔴 CODE: blockierendes `delay()` im ADC-Sampling

**Problem:** In v0.02 als „Verbesserung" eingeführt (siehe Punkt 8 weiter unten):
```cpp
for(uint8_t i = 0; i < 4; i++) {
    adcSum += analogRead(pin);
    delay(2);
}
```

**Auswirkung:** 8 ms blockierte `loop()` pro Messung und Kanal. Bei
SoftwareSerial-RS485 (Debug-Modus, ATmega328P hat nur einen UART) gehen in
dieser Zeit Empfangs-Frames verloren. `analogRead()` selbst braucht ~112 µs und
ist bereits die Wandlungspause — das `delay(2)` bringt keinen Rauschgewinn.

**Fix:** ✅ Inkrementelles Sampling wie `HBWAnalogIn`: ein Sample pro
`loop()`-Durchlauf, Mittelwert nach `MAX_SAMPLES`. Anders als in der Lib sind
`sum`/`sampleCount` **Member statt `static`** — in `HBWAnalogIn.cpp` sind sie
`static` in einer Member-Funktion und damit über alle Kanäle geteilt, was bei
mehreren Instanzen die Samples vermischt.

---

### 🟠 CODE: Sendeergebnis wurde ignoriert

**Problem:**
```cpp
device->sendInfoMessage(channel, 2, data);
lastSendValue = currentValue;   // auch wenn der Bus belegt war
```

**Auswirkung:** Bei `BUS_BUSY` gilt der Wert als gesendet. Die Delta-Prüfung
vergleicht danach gegen einen Wert, den die CCU nie gesehen hat — der Messwert
geht bis zum nächsten `send_max_interval` verloren.

**Fix:** ✅ Rückgabewert auswerten (wie `HBWAnalogIn.cpp`):
```cpp
if (device->sendInfoMessage(channel, sizeof(data), data) != HBWDevice::BUS_BUSY) {
    lastSendValue = currentValue;
}
lastSentTime = now;
```

---

### 🟡 CODE: Sendelogik hing am Messintervall

**Problem:** Die komplette Sendeprüfung lag innerhalb von
`if (now - lastActionTime >= nextActionDelay)`. `send_min_interval` und
`send_max_interval` konnten also nur zu Messzeitpunkten greifen.

**Fix:** ✅ Sendeblock aus dem Messblock herausgezogen, wird jetzt in jedem
`loop()`-Durchlauf ausgewertet.

---

### 🟡 CODE/XML: OFFSET kollidierte mit dem Leer-EEPROM-Marker

**Problem:** Offset signed in 1 Byte, dazu `if (config->offset != 0xFF)` als
„nicht gesetzt"-Prüfung. 0xFF ist als `int8_t` aber −1, also −0.01 bar — ein
gültiger Wert, der stumm verworfen wurde.

**Fix:** ✅ Bias 127 in der XML:
```xml
<conversion type="float_integer_scale" offset="1.27" factor="100"/>
```
`device = (param + 1.27) × 100` → −1.27 bar = 0, 0.00 bar = 127, +1.27 bar = 254.
0xFF liegt außerhalb des gültigen Bereichs und wird in `afterReadConfig()` auf
127 gesetzt. Aus demselben Grund enden `SEND_DELTA_VALUE` bei 2.54 statt 2.55
und `UPDATE_INTERVAL` bei 254 statt 255.

---

### ✅ Neu: EEPROM-Layout zur Compile-Zeit geprüft

Der `address_step`-Fehler aus v0.01 kann nicht mehr unbemerkt auftreten:

```cpp
static_assert(sizeof(hbw_config_analog_press) == 8, ...);
static_assert(offsetof(hbw_config, analogPressConfigs) == 6, ...);   // -> EEPROM 0x07
static_assert(0x01 + sizeof(hbw_config) <= 0x03FC, ...);             // OWN_ADDRESS
```

Build-Stand (arduino:avr:nano, ATmega328P):

| Variante | Flash | RAM |
|---|---|---|
| Debug (SoftwareSerial + USB-Debug) | 12878 B (41%) | 493 B (24%) |
| Produktiv (`USE_HARDWARE_SERIAL`) | 11200 B (36%) | 378 B (18%) |

---

## Übersicht der Änderungen von v0.01 → v0.02

### 🐛 KRITISCHE BUGFIXES

#### 1. **Doppelte Channel-Initialisierung** (Memory Leak)
**Problem:**
```cpp
// In Test_A.ino v0.01:
for (uint8_t i = 0; i < NUMBER_OF_CHAN; i++) {
    channels[i] = new HBWAnalogPRESS(...);  // Zeile 80-82
}
// ... später im Code ...
for (uint8_t i = 0; i < NUMBER_OF_CHAN; i++) {
    channels[i] = new HBWAnalogPRESS(...);  // Zeile 136-138 - NOCHMAL!
}
```

**Auswirkung:** 
- Pointer wurde überschrieben
- Alter Speicher nicht freigegeben → Memory Leak
- Bei jedem Reset wächst der RAM-Verlust

**Fix:** Zweite Initialisierungsschleife entfernt ✅

---

#### 2. **EEPROM Address Mapping - Falsche Schrittweite**
**Problem:**
```xml
<!-- v0.01 XML: -->
<paramset type="MASTER" address_start="0x23" address_step="6">
```

Aber die Struktur ist 8 Bytes groß:
```cpp
struct hbw_config_analog_press {
  uint8_t  send_delta_value;     // +0
  uint8_t  offset;               // +1
  uint16_t send_max_interval;    // +2-3
  uint16_t send_min_interval;    // +4-5
  uint8_t  update_interval;      // +6
  uint8_t  pressure_sensor_type; // +7
}; // = 8 Bytes!
```

**Auswirkung:**
- Kanal 1: 0x23-0x2A ✓
- Kanal 2: 0x29-0x30 ✗ (Überschneidung mit Kanal 1!)
- Kanal 3: 0x2F-0x36 ✗ (Chaos)
- Parameter wurden überschrieben
- Konfiguration funktionierte nicht korrekt

**Fix:** `address_step="8"` ✅

**Neue Speicherkarte:**
```
Kanal 1: 0x23-0x2A  (8 Bytes)
Kanal 2: 0x2B-0x32  (8 Bytes)
Kanal 3: 0x33-0x3A  (8 Bytes)
Kanal 4: 0x3B-0x42  (8 Bytes)
```

---

#### 3. **SEND_MIN_INTERVAL - Timing Bug**
**Problem:**
```cpp
// v0.01 Code:
if (config->send_min_interval != 0xFFFF) {
    if ((now - lastSentTime) < ((uint32_t)config->send_min_interval * 1000)) {
        return; // Springt aus der GANZEN Funktion!
    }
}
```

**Auswirkung:**
- `lastActionTime` wird NICHT gesetzt
- `nextActionDelay` wird NICHT aktualisiert
- Die nächste Messung findet SOFORT statt (nicht nach UPDATE_INTERVAL)
- CPU-Last steigt, ADC wird permanent gelesen

**Fix:**
```cpp
// v0.02 Code:
if (config->send_min_interval != 0xFFFF && config->send_min_interval != 0) {
    if ((now - lastSentTime) < ((uint32_t)config->send_min_interval * 1000)) {
        // Messung wurde aktualisiert, aber zu früh zum Senden
        return; // Timing wurde VORHER korrekt gesetzt
    }
}
```

Timing-Updates wurden VOR dem MIN_INTERVAL Check verschoben ✅

---

#### 4. **SEND_MIN_INTERVAL - Datentyp-Konflikt**
**Problem:**
```cpp
// C-Struktur sagt:
uint16_t send_min_interval;  // 2 Bytes

// XML v0.01 sagt:
<parameter id="SEND_MIN_INTERVAL">
    <physical size="1.0" type="integer" ...>  // Nur 1 Byte!
```

**Auswirkung:**
- High-Byte des Wertes wurde in `update_interval` geschrieben
- Werte >255 funktionierten nicht
- Max. MIN_INTERVAL war 255s statt 65535s

**Fix:** XML jetzt auch 2 Bytes ✅
```xml
<physical size="2.0" type="integer" interface="eeprom" endian="little">
    <address index="+4.0"/>
</physical>
```

---

#### 5. **Sensortyp-Index - Absolut statt Relativ**
**Problem:**
```xml
<!-- v0.01: -->
<parameter id="Sensortyp">
    <physical type="integer" interface="config" list="1" index="39.0" size="1"/>
</parameter>
```

**Auswirkung:**
- Fest kodierte Adresse 0x39
- Funktioniert nur für Kanal 1 zufällig korrekt
- Bei anderen Kanälen: falsches EEPROM-Byte

**Fix:** Relative Adressierung ✅
```xml
<physical type="integer" interface="config" list="1" index="+7.0" size="1"/>
```

---

### ⚙️ FEHLENDE FEATURES IMPLEMENTIERT

#### 6. **OFFSET-Parameter nicht verwendet**
**Problem:**
- Variable `offset` existierte in struct
- Parameter war in XML (v0.01 falsch platziert)
- Code verwendete Offset NIEMALS

**Auswirkung:**
- Sensor-Kalibrierung unmöglich
- Ungenaue Messungen konnten nicht korrigiert werden

**Fix:** Offset-Anwendung implementiert ✅
```cpp
if (config->offset != 0xFF && config->offset != 0) {
    int16_t offsetValue = (int8_t)config->offset; // Signed!
    int32_t temp = (int32_t)currentValue + offsetValue;
    
    // Clamp to valid range
    if (temp < 0) currentValue = 0;
    else if (temp > 65535) currentValue = 65535;
    else currentValue = (uint16_t)temp;
}
```

---

#### 7. **OFFSET im falschen Paramset**
**Problem:**
```xml
<!-- v0.01: OFFSET war im Device-Master -->
<paramset id="HBW-Sen-PRESS_dev_master" type="MASTER">
    <parameter id="OFFSET">
        <address index="+1.0"/>  <!-- Bezieht sich auf 0x0001! -->
    </parameter>
</paramset>
```

**Auswirkung:**
- Offset galt für ALLE Kanäle gleichzeitig
- Konnte nicht pro Kanal kalibriert werden
- Überschrieb LOGGING_TIME Parameter

**Fix:** OFFSET in Channel-Paramset verschoben ✅
```xml
<paramset type="MASTER" id="hbw_press_master" address_start="0x23" address_step="8">
    <parameter id="OFFSET">
        <address index="+1.0"/>  <!-- Jetzt relativ zu Kanal-Start -->
    </parameter>
</paramset>
```

---

### 🔧 VERBESSERUNGEN

#### 8. **ADC Oversampling verbessert** ⚠️ in v0.03 zurückgenommen
> Das eingefügte `delay(2)` war keine Verbesserung, sondern ein Bug: es
> blockiert die `loop()` und kostet auf SoftwareSerial-RS485 Empfangs-Frames.
> Siehe v0.03-Abschnitt oben.

**Vorher:**
```cpp
for(uint8_t i=0; i<4; i++) { 
    adcSum += analogRead(pin); 
}
```

**Nachher:**
```cpp
for(uint8_t i = 0; i < 4; i++) { 
    adcSum += analogRead(pin);
    delay(2); // Kleine Pause zwischen Samples
}
```

**Vorteil:** Bessere Rauschunterdrückung

---

#### 9. **Firmware-Version erhöht**
```cpp
// v0.01:
#define FIRMWARE_VERSION 0x0001

// v0.02:
#define FIRMWARE_VERSION 0x0002
```

---

#### 10. **Debug-Output verbessert**
**Neu:**
- Freier RAM angezeigt
- Alle ADC-Kanäle mit Spannungswerten
- Firmware-Version im Startup-Banner
- Strukturierte Debug-Ausgabe

---

#### 11. **Kommentare und Dokumentation**
- Vollständige README.md
- Inline-Kommentare in allen Dateien
- EEPROM Memory Map dokumentiert
- Troubleshooting-Guide

---

## Zusammenfassung der kritischen Fixes

| Bug | Schwere | Status |
|-----|---------|--------|
| Doppelte Initialisierung | 🔴 Kritisch | ✅ Gefixt |
| EEPROM address_step | 🔴 Kritisch | ✅ Gefixt |
| MIN_INTERVAL Timing | 🔴 Kritisch | ✅ Gefixt |
| MIN_INTERVAL Datentyp | 🟠 Hoch | ✅ Gefixt |
| Sensortyp-Index | 🟠 Hoch | ✅ Gefixt |
| OFFSET nicht verwendet | 🟡 Mittel | ✅ Gefixt |
| OFFSET falsch platziert | 🟡 Mittel | ✅ Gefixt |

---

## Test-Empfehlungen nach Bugfixes

### 1. EEPROM Test
```cpp
// Lese Konfiguration aller Kanäle aus
// Prüfe ob Werte an richtigen Adressen stehen
```

### 2. Timing Test
```cpp
// Setze UPDATE_INTERVAL = 10s
// Setze MIN_INTERVAL = 30s
// → Messung alle 10s, Senden max. alle 30s
```

### 3. Offset Test
```cpp
// Setze bekannten Druck (z.B. 2.00 bar)
// Zeigt 1.95 bar → OFFSET = +0.05 bar
// Prüfe ob Anzeige korrigiert wird
```

### 4. Memory Leak Test
```cpp
// Mehrfach Reset drücken
// Free RAM sollte GLEICH bleiben
```

---

## Dateien zum Deployment

✅ `HBWAnalogPRESS.h` - Header mit korrekten Kommentaren
✅ `HBWAnalogPRESS.cpp` - Alle Bugfixes implementiert
✅ `HBW-Sen-PRESS-DR.ino` - Sauberer Setup, keine doppelte Init
✅ `HBW-Sen-PRESS.xml` - Korrigiertes Address-Mapping
✅ `README.md` - Vollständige Dokumentation
✅ `BUGFIXES.md` - Diese Datei

Bereit für Produktion! 🚀
