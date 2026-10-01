#pragma once
// PCF85063 clock chip on the board (I2C 0x51). Keeps UTC while the board has
// power (including deep sleep); set from the phone or internet time.
#include <time.h>

// Reads the chip and sets the system clock from it. Returns false if the chip
// is missing or has lost the time.
bool rtcBegin();
bool rtcPresent();

// Sets the system clock and the chip (UTC seconds).
void rtcSet(time_t utc);

// Writes the current system time to the chip (e.g. after internet time sync).
void rtcSaveSystemTime();

// True once the system clock holds a real time.
bool clockValid();
