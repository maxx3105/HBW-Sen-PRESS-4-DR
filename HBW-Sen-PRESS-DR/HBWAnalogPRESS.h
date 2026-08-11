/*
 * HBWAnalogPRESS.h
 *
 * analog input channel for pressure sensors
 * Supports 0.5 MPa and 1.2 MPa hydraulic pressure sensors
 *
 * Based on HBWired by Thorsten Pferdekaemper
 * www.loetmeister.de
 *
 */

#ifndef HBWAnalogPRESS_h
#define HBWAnalogPRESS_h

//#define DEBUG_OUTPUT   // extra debug output on serial/USB

#define SAMPLE_INTERVAL 3           // seconds between single samples
#define MAX_SAMPLES 4               // samples per reading (average)
#define DEFAULT_UPDATE_INTERVAL 30  // seconds

/* Supported sensors */
// MPa 0.5 - Sensortyp Code: 2 in config
// MPa 1.2 - Sensortyp Code: 1 in config
// Output: 0.5V - 4.5V (0V-5V Signal, 0.5V offset, 4V range)
// Sensors are ratiometric to their supply -> use default (VCC) ADC reference

#define SENSOR_OFFSET_MV 500   // sensor output at zero pressure
#define SENSOR_RANGE_MV 4000   // 0.5V .. 4.5V
#define OFFSET_BIAS 127        // config->offset is stored with bias 127 (see XML)

#include <inttypes.h>
#include "HBWired.h"

// address step 8
struct hbw_config_analog_press {
  uint8_t  send_delta_value;     // +0: 1 Byte (in 0.01 bar)
  uint8_t  offset;               // +1: 1 Byte (in 0.01 bar, bias 127)
  uint16_t send_max_interval;    // +2: 2 Bytes (in seconds)
  uint16_t send_min_interval;    // +4: 2 Bytes (in seconds)
  uint8_t  update_interval;      // +6: 1 Byte (in seconds)
  uint8_t  pressure_sensor_type; // +7: 1 Byte (0=Aus, 1=1.2MPa, 2=0.5MPa)
}; // Total: 8 Bytes per channel

class HBWAnalogPRESS : public HBWChannel {
  public:
    HBWAnalogPRESS(uint8_t pin, hbw_config_analog_press* config);
    virtual uint8_t get(uint8_t* data);
    virtual void loop(HBWDevice*, uint8_t channel);
    virtual void afterReadConfig();

  private:
    uint8_t pin;   // ADC Pin
    hbw_config_analog_press* config;
    uint32_t lastActionTime;
    uint16_t nextActionDelay;  // seconds
    uint32_t adcSum;           // accumulator for oversampling
    uint8_t sampleCount;       // samples taken so far
    uint16_t currentValue;   // current pressure value in 0.01 bar
    uint16_t lastSendValue;  // last sent value in 0.01 bar
    uint32_t lastSentTime;   // timestamp of last send
};

#endif
