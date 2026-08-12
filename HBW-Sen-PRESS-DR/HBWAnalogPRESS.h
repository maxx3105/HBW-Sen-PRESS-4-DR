/*
 * HBWAnalogPRESS.h
 *
 * Analogeingang fuer Drucksensoren
 * Unterstuetzt Hydraulik-Drucksensoren 0,5 MPa und 1,2 MPa
 *
 * Basiert auf HBWired von Thorsten Pferdekaemper
 * www.loetmeister.de
 *
 */

#ifndef HBWAnalogPRESS_h
#define HBWAnalogPRESS_h

//#define DEBUG_OUTPUT   // zusaetzliche Debug-Ausgabe ueber Serial/USB

#define SAMPLE_INTERVAL 3           // Sekunden zwischen zwei Einzelmessungen
#define MAX_SAMPLES 4               // Einzelmessungen je Messwert (Mittelwert)
#define DEFAULT_UPDATE_INTERVAL 30  // Sekunden

/* Unterstuetzte Sensoren */
// 0,5 MPa - Sensortyp-Code 2 in der Konfiguration
// 1,2 MPa - Sensortyp-Code 1 in der Konfiguration
// Ausgang: 0,5 V - 4,5 V (Signal 0-5 V, 0,5 V Offset, 4 V Spanne)
// Die Sensoren arbeiten ratiometrisch zu ihrer Versorgung
// -> ADC mit der Standardreferenz (VCC) betreiben, nicht mit den internen 1,1 V

#define SENSOR_OFFSET_MV 500   // Sensorausgang bei Druck null
#define SENSOR_RANGE_MV 4000   // 0,5 V .. 4,5 V
#define OFFSET_BIAS 127        // config->offset wird mit Bias 127 gespeichert (siehe XML)

#include <inttypes.h>
#include "HBWired.h"

// address_step 8
struct hbw_config_analog_press {
  uint8_t  send_delta_value;     // +0: 1 Byte (in 0,01 bar)
  uint8_t  offset;               // +1: 1 Byte (in 0,01 bar, Bias 127)
  uint16_t send_max_interval;    // +2: 2 Byte (in Sekunden)
  uint16_t send_min_interval;    // +4: 2 Byte (in Sekunden)
  uint8_t  update_interval;      // +6: 1 Byte (in Sekunden)
  uint8_t  pressure_sensor_type; // +7: 1 Byte (0=aus, 1=1,2MPa, 2=0,5MPa)
}; // insgesamt 8 Byte je Kanal

class HBWAnalogPRESS : public HBWChannel {
  public:
    HBWAnalogPRESS(uint8_t pin, hbw_config_analog_press* config);
    virtual uint8_t get(uint8_t* data);
    virtual void loop(HBWDevice*, uint8_t channel);
    virtual void afterReadConfig();

  private:
    uint8_t pin;   // ADC-Pin
    hbw_config_analog_press* config;
    uint32_t lastActionTime;
    uint16_t nextActionDelay;  // Sekunden
    uint32_t adcSum;           // Summe fuer die Mittelwertbildung
    uint8_t sampleCount;       // bisher genommene Einzelmessungen
    uint16_t currentValue;     // aktueller Druck in 0,01 bar
    uint16_t lastSendValue;    // zuletzt gesendeter Wert in 0,01 bar
    uint32_t lastSentTime;     // Zeitpunkt der letzten Sendung
};

#endif
