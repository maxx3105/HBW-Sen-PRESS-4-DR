//*******************************************************************
//
// HBW-Sen-PRESS-DR
//
// Homematic Wired Homebrew Hardware
// ATmega328P als Homematic-Device
// Drucksensormodul fuer Hutschienenmontage
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
//
//*******************************************************************

#define HARDWARE_VERSION 0x01
#define FIRMWARE_VERSION 0x0004
#define HMW_DEVICETYPE 0x50  // Device ID (hbw-sen-press-dr.xml auf der CCU installieren)

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

//#define USE_HARDWARE_SERIAL   // RS485 ueber die Hardware-UART (Produktivbetrieb).
                                // Schaltet die Debug-Ausgabe ab - der 328P hat nur
                                // einen UART, entweder Bus oder Debug.
/* "HBW_DEBUG" in 'HBWired.h' auskommentieren, um den Debug-Code zu entfernen.
 * Es wirkt zugleich als Hauptschalter fuer die hbwdebug()-Funktionen. */

#include <Arduino.h>
#include <HBWired.h>
#include "HBWAnalogPRESS.h"

// Pinbelegung
#ifdef USE_HARDWARE_SERIAL
  #define RS485_TXEN 2     // Sendefreigabe fuer den MAX485
  #define BUTTON 8         // Taster fuer Werksreset
#else
  #define RS485_RXD 4      // SoftwareSerial RX
  #define RS485_TXD 2      // SoftwareSerial TX
  #define RS485_TXEN 3     // Sendefreigabe fuer den MAX485
  #define BUTTON 8         // Button for factory reset
  #define DEBUG_OUTPUT
  #include "FreeRam.h"
  #include <HBWSoftwareSerial.h>
  HBWSoftwareSerial rs485(RS485_RXD, RS485_TXD); // RX, TX
#endif

#define LED LED_BUILTIN    // Status-LED

// Konfigurationsstruktur im EEPROM
// HBWDevice liest sie ab EEPROM-Adresse 0x01 (HBWired.cpp: readConfig)
struct hbw_config {
  uint8_t logging_time;             // 0x01
  uint32_t central_address;         // 0x02-0x05
  uint8_t direct_link_deactivate:1; // 0x06:0
  uint8_t dummy1:7;                 // 0x06:1-7

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

// Zuordnung der Sensoreingaenge (A4..A7 sind auf der Platine vorhanden,
// im CRMB2-Gehaeuse aber nicht herausgefuehrt)
uint8_t SENSOR_PINS[] = {A0, A1, A2, A3, A4, A5, A6, A7};

#if NUMBER_OF_CHAN > 8
  #error "ATmega328P hat nur 8 ADC-Eingaenge (A0..A7)"
#endif

// Geraete- und Kanalobjekte
HBWDevice* device = NULL;
HBWAnalogPRESS* channels[NUMBER_OF_CHAN];


void setup() {
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
  hbwdebug(F("HBW-Sen-PRESS-DR v"));
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
}
