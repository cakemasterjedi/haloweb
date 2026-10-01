#ifndef EMBLEM_DIAG
#include "motion.h"

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

namespace {

// QMI8658 registers.
const uint8_t REG_WHO_AM_I = 0x00;  // reads 0x05
const uint8_t REG_CTRL1 = 0x02;     // serial interface
const uint8_t REG_CTRL2 = 0x03;     // accelerometer range + data rate
const uint8_t REG_CTRL5 = 0x06;     // low-pass filters
const uint8_t REG_CTRL7 = 0x08;     // sensor enables
const uint8_t REG_AX_L = 0x35;      // AX_L, AX_H, AY_L, AY_H, AZ_L, AZ_H
const uint8_t REG_RESET = 0x60;

const float LSB_PER_G = 4096.0f;  // +-8 g range
const uint32_t PERIOD_MS = 5;     // 200 Hz
const float DT = PERIOD_MS / 1000.0f;

uint8_t addr = 0;
volatile float tiltDeg = 0;
volatile float boost = 0;
volatile float vibration = 0;  // RMS of the vibration, g
volatile uint32_t doubleTaps = 0;
volatile float peakJolt = 0;  // biggest recent jolt, g (fades over ~2 s)
volatile uint32_t jolts = 0;
volatile float joltThreshold = 0.25f;
volatile uint32_t lastMovement = 0;  // millis() of the last movement
volatile uint32_t quietBeforeJolt = 0;
uint32_t burstQuiet = 0, burstStart = 0, lastJolt = 0;
const float MOVEMENT_G = 0.06f;      // smaller jolts don't count as movement
volatile uint8_t tapSensitivity = 0;

bool writeReg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

bool readRegs(uint8_t reg, uint8_t *buf, size_t n) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(addr, uint8_t(n)) != n) return false;
    for (size_t i = 0; i < n; i++) buf[i] = Wire.read();
    return true;
}

void task(void *) {
    float gx = 0, gy = 0, gz = 1;  // gravity (slow average)
    float tx = 0, ty = 1;          // gravity in the screen plane (quicker average, for tilt)
    float px = 0, py = 0, pz = 0;  // previous sample
    bool first = true;
    uint32_t lastTap = 0, lastSpike = 0, lockUntil = 0;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(PERIOD_MS));
        uint8_t b[6];
        if (!readRegs(REG_AX_L, b, 6)) continue;
        const float ax = int16_t(b[0] | (b[1] << 8)) / LSB_PER_G;
        const float ay = int16_t(b[2] | (b[3] << 8)) / LSB_PER_G;
        const float az = int16_t(b[4] | (b[5] << 8)) / LSB_PER_G;
        if (first) {
            gx = px = tx = ax;
            gy = py = ty = ay;
            gz = pz = az;
            first = false;
        }

        // Gravity: a slow average (about 2 s) so driving doesn't move it much.
        const float kg = DT / 2.0f;
        gx += (ax - gx) * kg;
        gy += (ay - gy) * kg;
        gz += (az - gz) * kg;
        const float kt = DT / 0.4f;
        tx += (ax - tx) * kt;
        ty += (ay - ty) * kt;
        tiltDeg = atan2f(tx, ty) * 180.0f / float(M_PI);

        // Acceleration on top of gravity: fast attack, ~1 s release.
        const float dx = ax - gx, dy = ay - gy, dz = az - gz;
        const float dyn = sqrtf(dx * dx + dy * dy + dz * dz);
        const float b0 = boost;
        boost = b0 + (dyn - b0) * (dyn > b0 ? 0.25f : DT / 1.0f);

        // Vibration level (driving): RMS of sample-to-sample change, ~1 s.
        const float jx = ax - px, jy = ay - py, jz = az - pz;
        const float jerk = sqrtf(jx * jx + jy * jy + jz * jz);
        const float v2 = vibration * vibration;
        vibration = sqrtf(v2 + (jerk * jerk - v2) * (DT / 1.0f));
        px = ax;
        py = ay;
        pz = az;
        const float pk = peakJolt * (1 - DT / 2.0f);
        peakJolt = jerk > pk ? jerk : pk;

        // Jolts, and how long the car had been still before the burst of
        // movement they belong to (opening a door moves it a little before
        // the door shuts).
        const uint32_t nowMs = millis();
        const bool moved = jerk > MOVEMENT_G || vibration > 0.06f;
        if (moved && nowMs - lastMovement > 5000) {
            burstQuiet = nowMs - lastMovement;
            burstStart = nowMs;
        }
        if (jerk > joltThreshold && nowMs - lastJolt > 300) {  // not the ringing of the previous one
            quietBeforeJolt = nowMs - burstStart < 30000 ? burstQuiet : 0;
            jolts = jolts + 1;
            lastJolt = nowMs;
        }
        if (moved) lastMovement = nowMs;

        // Double-tap: two sharp spikes 0.1-0.6 s apart while otherwise still.
        const uint8_t sens = tapSensitivity;
        if (sens) {
            const float threshold = sens == 1 ? 1.2f : sens == 2 ? 0.8f : 0.5f;
            const uint32_t now = millis();
            if (jerk > threshold && int32_t(now - lockUntil) >= 0) {
                if (now - lastSpike > 60) {  // a new tap, not the ringing of the last one
                    if (lastTap && now - lastTap >= 100 && now - lastTap <= 600) {
                        doubleTaps = doubleTaps + 1;
                        lastTap = 0;
                        lockUntil = now + 1000;
                    } else if (vibration < 0.08f + threshold * 0.1f) {
                        lastTap = now;
                    }
                }
                lastSpike = now;
            }
            if (lastTap && now - lastTap > 600) lastTap = 0;
        }
    }
}

}  // namespace

bool motionBegin() {
    for (uint8_t a : {uint8_t(0x6B), uint8_t(0x6A)}) {
        addr = a;
        uint8_t id = 0;
        if (readRegs(REG_WHO_AM_I, &id, 1) && id == 0x05) break;
        addr = 0;
    }
    if (!addr) return false;
    writeReg(REG_RESET, 0xB0);
    delay(20);
    writeReg(REG_CTRL1, 0x40);  // register address auto-increment, little-endian
    writeReg(REG_CTRL2, 0x24);  // +-8 g, 500 Hz
    writeReg(REG_CTRL5, 0x07);  // accelerometer low-pass on (narrowest)
    writeReg(REG_CTRL7, 0x01);  // accelerometer only
    delay(20);
    xTaskCreatePinnedToCore(task, "motion", 3072, nullptr, 2, nullptr, 0);
    return true;
}

bool motionPresent() { return addr != 0; }
float motionTiltDeg() { return tiltDeg; }
float motionBoost() { return boost; }
bool motionMoving() { return vibration > 0.06f; }
float motionPeakJolt() { return peakJolt; }
uint32_t motionJolts() { return jolts; }
void motionSetJoltThreshold(float g) { joltThreshold = g; }
uint32_t motionQuietBeforeJolt() { return quietBeforeJolt; }
uint32_t motionQuietMs() { return addr ? millis() - lastMovement : 0; }
uint32_t motionDoubleTaps() { return doubleTaps; }
void motionSetTapSensitivity(uint8_t sensitivity) { tapSensitivity = sensitivity; }
#endif  // EMBLEM_DIAG
