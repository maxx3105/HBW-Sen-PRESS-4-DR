//*******************************************************************
//
// HBW-Sen-PRESS-4-DR
//
// Homematic Wired Homebrew Hardware
// ATmega328P als Homematic-Device
// Drucksensormodul 4-fach fuer Hutschienenmontage
//
// Basiert auf HBWired von Thorsten Pferdekaemper & Dirk Hoffmann
// (thorsten@pferdekaemper.com, hoffmann@vmd-jena.de)
//
// 2024-04-06 maxx3105
// Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/at/
//
//*******************************************************************
// Aenderungsverlauf:
// v0.01 - Erste Fassung
// v0.02 - doppelte Kanal-Initialisierung behoben
//       - EEPROM-Adressierung korrigiert
//       - OFFSET im Code umgesetzt
//       - Timing der Sendeintervalle korrigiert
// v0.03 - XML auf HMW-Frames umgestellt (INFO_LEVEL '#i', LEVEL_GET '#S'),
//         Sensortyp von interface="config" auf eeprom, OWN_ADDRESS ergaenzt
//       - blockierendes delay() im ADC-Sampling entfernt (SoftwareSerial!)
//       - Sendeergebnis wird ausgewertet (BUS_BUSY -> Wert nicht verwerfen)
//       - Sendelogik vom Messintervall entkoppelt
//       - OFFSET mit Bias 127 statt 0xFF-Sonderfall
//       - feste Kanalzahl 8, count_from_sysinfo entfaellt (Wired kennt es nicht)
// v0.04 - Kanalzahl 8 -> 4 (CRMB2-Gehaeuse fuehrt nur 4 Sensoren heraus)
// v0.05 - Geraet in HBW-Sen-PRESS-4-DR umbenannt
//       - Pinbelegung/EepromPtr in <Name>_config_example.h ausgelagert
//       - Identify-LED ergaenzt (blinkt, solange IDENTIFY_LED gesetzt ist)
//
//*******************************************************************

#define HARDWARE_VERSION 0x01
#define FIRMWARE_VERSION 0x0005
#define HMW_DEVICETYPE 0x50  // Device ID (hbw-sen-press-4-dr.xml auf der CCU installieren)

#define IDENTIFY_LED_INTERVAL 600   // Blinkintervall der Identify-LED in ms

#define NUMBER_OF_CHAN 4   // Drucksensor-Kanaele, belegt A0..A3.
                           // MUSS mit count="4" in der XML uebereinstimmen.
                           /* Die Platine haette 8 ADC-Eingaenge, im CRMB2-Gehaeuse
                            * sind aber nur 4 Sensoren anschliessbar. Eine dynamische
                            * Kanalzahl ueber count_from_sysinfo gibt es im Wired-Zweig
                            * nicht: hs485d kennt das Attribut nicht (binaer belegt,
                            * `strings hs485d` findet weder count_from_sysinfo noch
                            * einen Ersatz; rfd dagegen schon). Ohne count legt die CCU
                            * NULL Kanaele an - am Bus verifiziert. Mehr Kanaele
                            * braeuchten deshalb einen zweiten Geraetetyp mit eigenem
                            * Typ-Byte, wie bei HBW-LC-RGBWW-3/-6. */

// HBWired-Protokoll und Module
#include <Arduino.h>
#include <HBWired.h>
#include "HBWAnalogPRESS.h"
#include <HBW_eeprom.h>

// Pinbelegung, EepromPtr und USE_HARDWARE_SERIAL stehen hier. Bei abweichender
// Hardware die Datei kopieren und die Kopie stattdessen einbinden.
#include "HBW-Sen-PRESS-4-DR_config_example.h"


// Konfigurationsstruktur im EEPROM
// HBWDevice liest sie ab EEPROM-Adresse 0x01 (HBWired.cpp: readConfig)
struct hbw_config {
  uint8_t logging_time;             // 0x01
  uint32_t central_address;         // 0x02-0x05
  uint8_t direct_link_deactivate:1; // 0x06:0
  uint8_t n_identify_led:1;         // 0x06:1  0=Identify-LED an (blinkt), 1=aus
  uint8_t              :6;          // 0x06:2-7

  // Die Kanalkonfiguration beginnt bei 0x07 (XML: address_start="0x07" address_step="8")
  hbw_config_analog_press analogPressConfigs[NUMBER_OF_CHAN];
} hbwconfig;


// EEPROM-Layout gegen die XML absichern. HBWDevice liest die Struktur ab
// EEPROM-Adresse 0x01, struct-Offset 6 entspricht also EEPROM 0x07.
static_assert(sizeof(hbw_config_analog_press) == 8,
              "Kanalstruktur passt nicht zu XML address_step=8");
static_assert(offsetof(hbw_config, analogPressConfigs) == 6,
              "Kanalconfig liegt nicht auf EEPROM 0x07 (XML address_start)");
static_assert(0x01 + sizeof(hbw_config) <= 0x03FC,
              "Config-Bereich kollidiert mit OWN_ADDRESS (E2END-3)");

// Zuordnung der Sensoreingaenge (Reihenfolge = Kanal 1..NUMBER_OF_CHAN),
// steht im Konfig-Header
uint8_t SENSOR_PINS[] = SENSOR_PIN_LIST;

static_assert(sizeof(SENSOR_PINS) >= NUMBER_OF_CHAN,
              "SENSOR_PIN_LIST im Konfig-Header hat weniger Pins als NUMBER_OF_CHAN");

// Geraete- und Kanalobjekte
HBWDevice* device = NULL;
HBWAnalogPRESS* channels[NUMBER_OF_CHAN];


#ifdef IDENTIFY_LED
// Identify-LED, um das Geraet im Verteiler zu finden. Blinkt, solange IDENTIFY_LED
// in der Geraetekonfiguration gesetzt ist. 'n_identify_led' wird invertiert
// gespeichert (siehe XML: boolean_integer invert="true"), damit die LED bei
// blankem EEPROM (0xFF) nach einem Werksreset aus bleibt.
void identifyLedLoop()
{
  static uint32_t lastTime = 0;
  uint32_t now = millis();

  if (hbwconfig.n_identify_led) {
    digitalWrite(IDENTIFY_LED, LOW);
    lastTime = now;
  }
  else if (now - lastTime >= IDENTIFY_LED_INTERVAL) {
    digitalWrite(IDENTIFY_LED, !digitalRead(IDENTIFY_LED));
    lastTime = now;
  }
};
#endif


void setup() {
#ifdef IDENTIFY_LED
  pinMode(IDENTIFY_LED, OUTPUT);
  digitalWrite(IDENTIFY_LED, LOW);
#endif

  // Kanalobjekte anlegen
  for (uint8_t i = 0; i < NUMBER_OF_CHAN; i++) {
    channels[i] = new HBWAnalogPRESS(SENSOR_PINS[i], &hbwconfig.analogPressConfigs[i]);
  }

#ifdef USE_HARDWARE_SERIAL
  // Produktivbetrieb: RS485 ueber die Hardware-UART
  Serial.begin(19200, SERIAL_8E1);

  device = new HBWDevice(HMW_DEVICETYPE, HARDWARE_VERSION, FIRMWARE_VERSION,
                         &Serial, RS485_TXEN, sizeof(hbwconfig), &hbwconfig,
                         NUMBER_OF_CHAN, (HBWChannel**)channels,
                         NULL,  // kein Debug-Stream
                         NULL, NULL);

  device->setConfigPins(BUTTON, LED);
  device->setStatusLEDPins(LED, LED); // Tx/Rx nutzen dieselbe LED wie die Konfiguration

#else
  // Debug-Betrieb: RS485 ueber SoftwareSerial, Debug-Ausgabe ueber die Hardware-UART
  Serial.begin(115200);  // Debug-Ausgabe ueber USB
  rs485.begin(19200);    // RS485 (muss 19200 Baud sein!)

  device = new HBWDevice(HMW_DEVICETYPE, HARDWARE_VERSION, FIRMWARE_VERSION,
                         &rs485, RS485_TXEN, sizeof(hbwconfig), &hbwconfig,
                         NUMBER_OF_CHAN, (HBWChannel**)channels,
                         &Serial,  // Debug-Stream
                         NULL, NULL);

  device->setConfigPins(BUTTON, LED);

  // Startmeldung
  hbwdebug(F("HBW-Sen-PRESS-4-DR v"));
  hbwdebug(FIRMWARE_VERSION);
  hbwdebug(F("\nFree RAM: "));
  hbwdebug(freeRam());
  hbwdebug(F(" bytes\nChannels: "));
  hbwdebug(NUMBER_OF_CHAN);
  hbwdebug(F("\n"));

  // ADC-Rohwerte beim Start ausgeben
  for (uint8_t i = 0; i < NUMBER_OF_CHAN; i++) {
    uint16_t adcValue = analogRead(SENSOR_PINS[i]);

    hbwdebug(F("ADC Ch"));
    hbwdebug(i);
    hbwdebug(F(": "));
    hbwdebug(adcValue);
    hbwdebug(F(" ("));
    hbwdebug((uint16_t)(((uint32_t)adcValue * 5000UL) >> 10));
    hbwdebug(F(" mV)\n"));
  }

  hbwdebug(F("=== Setup complete ===\n"));
#endif
}


void loop() {
  device->loop();

#ifdef IDENTIFY_LED
  identifyLedLoop();
#endif
}
