/*
 * HBWAnalogPRESS.cpp
 *
 * Analogeingang fuer Drucksensoren
 * Unterstuetzt Hydraulik-Drucksensoren 0,5 MPa und 1,2 MPa
 *
 * Basiert auf HBWired von Thorsten Pferdekaemper
 * www.loetmeister.de
 *
 */

#include "HBWAnalogPRESS.h"

HBWAnalogPRESS::HBWAnalogPRESS(uint8_t _pin, hbw_config_analog_press* _config) {
    pin = _pin;
    config = _config;
    lastActionTime = 0;
    nextActionDelay = SAMPLE_INTERVAL;  // Wartezeit bis zur ersten Messung
    adcSum = 0;
    sampleCount = 0;
    currentValue = 0;
    lastSendValue = 0;
    lastSentTime = 0;
}


void HBWAnalogPRESS::afterReadConfig() {
    // Vorgabewerte setzen, wenn das EEPROM leer ist (0xFF)
    if (config->update_interval == 0xFF)   config->update_interval = DEFAULT_UPDATE_INTERVAL;
    if (config->send_delta_value == 0xFF)  config->send_delta_value = 10;   // 0,1 bar
    if (config->send_max_interval == 0xFFFF) config->send_max_interval = 600;  // 10 Minuten
    if (config->send_min_interval == 0xFFFF) config->send_min_interval = 30;
    if (config->offset == 0xFF)            config->offset = OFFSET_BIAS;   // 0,00 bar

    // Sensortyp pruefen: 0=aus, 1=1,2MPa, 2=0,5MPa (faengt auch 0xFF ab)
    if (config->pressure_sensor_type > 2)  config->pressure_sensor_type = 0;
}


/* Standardfunktion - liefert die Laenge des Datenfelds; darin steht der aktuelle Messwert */
uint8_t HBWAnalogPRESS::get(uint8_t* data) {
    // hoeherwertiges Byte zuerst
    *data++ = (currentValue >> 8);
    *data = currentValue & 0xFF;
    return 2;
}


/* Standardfunktion - wird von der Hauptschleife fuer jeden Kanal der Reihe nach aufgerufen */
void HBWAnalogPRESS::loop(HBWDevice* device, uint8_t channel) {
    // Abgeschaltete Kanaele sofort verlassen (Sensortyp = NICHT_BELEGT in der CCU)
    if (config->pressure_sensor_type == 0) {
        currentValue = 0;
        lastSendValue = 0;
        return;
    }

    uint32_t now = millis();

    // === ADC einlesen, eine Messung je Durchlauf (blockiert den Bus nie) ===
    if (now - lastActionTime >= ((uint32_t)nextActionDelay * 1000)) {
        lastActionTime = now;
        nextActionDelay = SAMPLE_INTERVAL;

        adcSum += analogRead(pin);
        sampleCount++;

        if (sampleCount >= MAX_SAMPLES) {
            uint16_t avgADC = adcSum / MAX_SAMPLES;
            adcSum = 0;
            sampleCount = 0;
            nextActionDelay = config->update_interval;  // Pause bis zur naechsten Messreihe

            // === ADC-Wert in Millivolt umrechnen (ratiometrisch, VCC-Referenz) ===
            uint32_t voltage = ((uint32_t)avgADC * 5000UL) >> 10;

            // === Druck berechnen: 0,5 V = 0 bar, 4,5 V = Endwert ===
            if (voltage <= SENSOR_OFFSET_MV) {
                currentValue = 0;
            }
            else {
                // Endwert in 0,01 bar
                uint16_t maxBar = (config->pressure_sensor_type == 1) ? 1200 : 500;
                currentValue = (uint16_t)(((voltage - SENSOR_OFFSET_MV) * maxBar) / SENSOR_RANGE_MV);
            }

            // === Kalibrier-Offset anwenden (mit Bias 127 gespeichert) ===
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

    // === Sendelogik - unabhaengig vom Messzyklus ===
    // vor Ablauf des Mindestintervalls nicht senden
    if (config->send_min_interval && now - lastSentTime < (uint32_t)config->send_min_interval * 1000)
        return;

    uint16_t delta = (currentValue > lastSendValue) ? (currentValue - lastSendValue)
                                                    : (lastSendValue - currentValue);

    if ((config->send_max_interval && now - lastSentTime >= (uint32_t)config->send_max_interval * 1000) ||
        (config->send_delta_value && delta >= config->send_delta_value)) {

        uint8_t data[2];
        get(data);
        if (device->sendInfoMessage(channel, sizeof(data), data) != HBWDevice::BUS_BUSY) {
            lastSendValue = currentValue;   // nur bei Erfolg als gesendet vermerken
        }
        lastSentTime = now;   // bei Fehlschlag greift send_max_interval oder das naechste Delta

#ifdef DEBUG_OUTPUT
        hbwdebug(F("press-ch: "));  hbwdebug(channel);
        hbwdebug(F(" sent: "));  hbwdebug(lastSendValue);
        lastSendValue == currentValue ? hbwdebug(F(" SUCCESS!\n")) : hbwdebug(F(" FAILED!\n"));
#endif
    }
}
