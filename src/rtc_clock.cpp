#ifndef EMBLEM_DIAG
#include "rtc_clock.h"

#include <Arduino.h>
#include <Wire.h>
#include <sys/time.h>

namespace {

const uint8_t ADDR = 0x51;
const uint8_t REG_CONTROL1 = 0x00;
const uint8_t REG_SECONDS = 0x04;  // seconds, minutes, hours, days, weekdays, months, years
bool present = false;

uint8_t bcd(uint8_t v) { return uint8_t(((v / 10) << 4) | (v % 10)); }
uint8_t unbcd(uint8_t v) { return uint8_t((v >> 4) * 10 + (v & 0x0F)); }

// UTC broken-down time -> seconds since 1970, without the local time zone.
time_t utcFromTm(const struct tm &t) {
    static const int days[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    const int y = t.tm_year + 1900;
    long d = (y - 1970) * 365L + (y - 1969) / 4 + days[t.tm_mon] + t.tm_mday - 1;
    if (t.tm_mon > 1 && y % 4 == 0) d++;
    return time_t(d) * 86400 + t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec;
}

}  // namespace

bool rtcBegin() {
    Wire.beginTransmission(ADDR);
    Wire.write(REG_SECONDS);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom(ADDR, uint8_t(7)) != 7) return false;
    present = true;
    uint8_t r[7];
    for (int i = 0; i < 7; i++) r[i] = Wire.read();
    if (r[0] & 0x80) return false;  // oscillator stopped: the time was lost
    struct tm t = {};
    t.tm_sec = unbcd(r[0] & 0x7F);
    t.tm_min = unbcd(r[1] & 0x7F);
    t.tm_hour = unbcd(r[2] & 0x3F);
    t.tm_mday = unbcd(r[3] & 0x3F);
    t.tm_mon = unbcd(r[5] & 0x1F) - 1;
    t.tm_year = unbcd(r[6]) + 100;
    if (t.tm_mon < 0 || t.tm_mon > 11 || t.tm_year < 124) return false;
    struct timeval tv = {utcFromTm(t), 0};
    settimeofday(&tv, nullptr);
    return true;
}

bool rtcPresent() { return present; }

void rtcSet(time_t utc) {
    struct timeval tv = {utc, 0};
    settimeofday(&tv, nullptr);
    rtcSaveSystemTime();
}

void rtcSaveSystemTime() {
    if (!present || !clockValid()) return;
    time_t now = time(nullptr);
    struct tm t;
    gmtime_r(&now, &t);
    Wire.beginTransmission(ADDR);
    Wire.write(REG_CONTROL1);
    Wire.write(0x00);  // running, 24-hour
    Wire.endTransmission();
    Wire.beginTransmission(ADDR);
    Wire.write(REG_SECONDS);
    Wire.write(bcd(t.tm_sec));  // also clears the "time lost" flag
    Wire.write(bcd(t.tm_min));
    Wire.write(bcd(t.tm_hour));
    Wire.write(bcd(t.tm_mday));
    Wire.write(uint8_t(t.tm_wday));
    Wire.write(bcd(t.tm_mon + 1));
    Wire.write(bcd(t.tm_year % 100));
    Wire.endTransmission();
}

bool clockValid() { return time(nullptr) > 1700000000; }
#endif  // EMBLEM_DIAG
