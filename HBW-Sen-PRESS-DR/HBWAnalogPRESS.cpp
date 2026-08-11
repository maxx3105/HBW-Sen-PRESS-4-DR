/*
 * HBWAnalogPRESS.cpp
 *
 * analog input channel for pressure sensors
 * Supports 0.5 MPa and 1.2 MPa hydraulic pressure sensors
 *
 * Based on HBWired by Thorsten Pferdekaemper
 * www.loetmeister.de
 *
 */

#include "HBWAnalogPRESS.h"

HBWAnalogPRESS::HBWAnalogPRESS(uint8_t _pin, hbw_config_analog_press* _config) {
    pin = _pin;
    config = _config;
    lastActionTime = 0;
    nextActionDelay = SAMPLE_INTERVAL;  // initial delay
    adcSum = 0;
    sampleCount = 0;
    currentValue = 0;
    lastSendValue = 0;
    lastSentTime = 0;
}


void HBWAnalogPRESS::afterReadConfig() {
    // Set default values if EEPROM is empty (0xFF)
    if (config->update_interval == 0xFF)   config->update_interval = DEFAULT_UPDATE_INTERVAL;
    if (config->send_delta_value == 0xFF)  config->send_delta_value = 10;   // 0.1 bar
    if (config->send_max_interval == 0xFFFF) config->send_max_interval = 600;  // 10 minutes
    if (config->send_min_interval == 0xFFFF) config->send_min_interval = 30;
    if (config->offset == 0xFF)            config->offset = OFFSET_BIAS;   // 0.00 bar

    // Sensor type validation: 0=Disabled, 1=1.2MPa, 2=0.5MPa (also catches 0xFF)
    if (config->pressure_sensor_type > 2)  config->pressure_sensor_type = 0;
}


/* standard public function - returns length of data array. Data array contains current channel reading */
uint8_t HBWAnalogPRESS::get(uint8_t* data) {
    // MSB first
    *data++ = (currentValue >> 8);
    *data = currentValue & 0xFF;
    return 2;
}


/* standard public function - called by main loop for every channel in sequential order */
void HBWAnalogPRESS::loop(HBWDevice* device, uint8_t channel) {
    // Early exit if channel is disabled in CCU
    if (config->pressure_sensor_type == 0) {
        currentValue = 0;
        lastSendValue = 0;
        return;
    }

    uint32_t now = millis();

    // === ADC reading, one sample per pass (never block the bus) ===
    if (now - lastActionTime >= ((uint32_t)nextActionDelay * 1000)) {
        lastActionTime = now;
        nextActionDelay = SAMPLE_INTERVAL;

        adcSum += analogRead(pin);
        sampleCount++;

        if (sampleCount >= MAX_SAMPLES) {
            uint16_t avgADC = adcSum / MAX_SAMPLES;
            adcSum = 0;
            sampleCount = 0;
            nextActionDelay = config->update_interval;  // "sleep" until next reading

            // === Convert ADC to millivolts (ratiometric, VCC reference) ===
            uint32_t voltage = ((uint32_t)avgADC * 5000UL) >> 10;

            // === Calculate pressure: 0.5V = 0 bar, 4.5V = full scale ===
            if (voltage <= SENSOR_OFFSET_MV) {
                currentValue = 0;
            }
            else {
                // full scale in 0.01 bar
                uint16_t maxBar = (config->pressure_sensor_type == 1) ? 1200 : 500;
                currentValue = (uint16_t)(((voltage - SENSOR_OFFSET_MV) * maxBar) / SENSOR_RANGE_MV);
            }

            // === Apply calibration offset (stored with bias 127) ===
            int32_t corrected = (int32_t)currentValue + ((int16_t)config->offset - OFFSET_BIAS);
            if (corrected < 0)            currentValue = 0;
            else if (corrected > 0xFFFF)  currentValue = 0xFFFF;
            else                          currentValue = (uint16_t)corrected;

#ifdef DEBUG_OUTPUT
            hbwdebug(F("press-ch:"));  hbwdebug(channel);
            hbwdebug(F(" adc:"));  hbwdebug(avgADC);
            hbwdebug(F(" measured:"));  hbwdebug(currentValue);  hbwdebug(F("\n"));
#endif
        }
    }

    // === Send logic - independent of the measurement cycle ===
    // do not send before min interval
    if (config->send_min_interval && now - lastSentTime < (uint32_t)config->send_min_interval * 1000)
        return;

    uint16_t delta = (currentValue > lastSendValue) ? (currentValue - lastSendValue)
                                                    : (lastSendValue - currentValue);

    if ((config->send_max_interval && now - lastSentTime >= (uint32_t)config->send_max_interval * 1000) ||
        (config->send_delta_value && delta >= config->send_delta_value)) {

        uint8_t data[2];
        get(data);
        if (device->sendInfoMessage(channel, sizeof(data), data) != HBWDevice::BUS_BUSY) {
            lastSendValue = currentValue;   // store last value only on success
        }
        lastSentTime = now;   // if send failed, retry on send_max_interval or on next delta

#ifdef DEBUG_OUTPUT
        hbwdebug(F("press-ch: "));  hbwdebug(channel);
        hbwdebug(F(" sent: "));  hbwdebug(lastSendValue);
        lastSendValue == currentValue ? hbwdebug(F(" SUCCESS!\n")) : hbwdebug(F(" FAILED!\n"));
#endif
    }
}
