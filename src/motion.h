#pragma once
// QMI8658 motion sensor on the board (I2C 0x6B / 0x6A). A background task
// reads it 200 times a second and keeps a few simple measurements.
#include <stdint.h>

// Finds the sensor and starts the task. Call after Wire.begin().
bool motionBegin();
bool motionPresent();

// Direction of gravity in the plane of the screen, degrees (-180..180), in
// the sensor's own axes. Only meaningful while the emblem is still.
float motionTiltDeg();

// Acceleration beyond gravity (launching, braking, bumps) in g: rises quickly,
// falls back over about a second.
float motionBoost();

// True while there's steady vibration, i.e. the car is being driven.
bool motionMoving();

// Biggest recent jolt (sample-to-sample change, g), to tune tap sensitivity.
float motionPeakJolt();

// Number of double-taps on the badge so far. Taps are ignored while driving.
// sensitivity: 1 low .. 3 high (0 = don't detect).
uint32_t motionDoubleTaps();
void motionSetTapSensitivity(uint8_t sensitivity);
