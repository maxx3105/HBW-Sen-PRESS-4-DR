/* Vorlage fuer die Geraete-Konfiguration. Bei abweichender Hardware (Pinbelegung,
 * EEPROM, Controller) diese Datei kopieren, anpassen und die Kopie im Sketch
 * einbinden - dann wird sie von neuen Versionen aus GitHub nicht ueberschrieben. */

// Arduino NANO / ATmega328P: RS485 haengt per Vorgabe an der Hardware-UART,
// die Debug-Ausgabe ist dann abgeschaltet (der 328P hat nur einen UART).

EEPROMClass* EepromPtr = &EEPROM;  // internes EEPROM

#define USE_HARDWARE_SERIAL   // RS485 ueber die Hardware-UART (Produktivbetrieb).
                              // Auskommentieren fuer Debug-Ausgabe ueber USB,
                              // RS485 laeuft dann ueber SoftwareSerial.
/* "HBW_DEBUG" in 'HBWired.h' auskommentieren, um nicht benoetigten Code zu entfernen.
 * Es wirkt zugleich als Hauptschalter: hbwdebug() und hbwdebughex() zeigen dann auf
 * leere Funktionen.
 * ACHTUNG: Die Debug-Ausgaben der Kanalklasse schaltet NICHT dieser Sketch, sondern
 * DEBUG_OUTPUT in HBWAnalogPRESS.h - die .cpp ist eine eigene Uebersetzungseinheit
 * und sieht ein #define von hier nicht. */

// Pins
#ifdef USE_HARDWARE_SERIAL
  #define RS485_TXEN 2           // Sendefreigabe fuer den MAX487
  #define BUTTON 8               // Taster fuer Werksreset
  #define LED LED_BUILTIN        // Status-LED
  #define IDENTIFY_LED 12        // PB4/D12 - Identify-LED, auskommentieren wenn nicht bestueckt

  // Analogeingaenge der Drucksensoren, Reihenfolge = Kanal 1..NUMBER_OF_CHAN
  #define SENSOR_PIN_LIST {A0, A1, A2, A3}

#else
  #define RS485_RXD 4            // SoftwareSerial RX
  #define RS485_TXD 2            // SoftwareSerial TX
  #define RS485_TXEN 3           // Sendefreigabe fuer den MAX487
  #define BUTTON 8               // Taster fuer Werksreset
  #define LED LED_BUILTIN        // Status-LED
  #define IDENTIFY_LED 12        // PB4/D12 - Identify-LED, auskommentieren wenn nicht bestueckt

  #define SENSOR_PIN_LIST {A0, A1, A2, A3}

  #include "FreeRam.h"
  #include <HBWSoftwareSerial.h>
  HBWSoftwareSerial rs485(RS485_RXD, RS485_TXD); // RX, TX
#endif  //USE_HARDWARE_SERIAL
