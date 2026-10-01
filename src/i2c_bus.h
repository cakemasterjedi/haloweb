#pragma once
// The I2C bus is shared by the main loop (I/O expander, clock chip, INA219)
// and the motion sensor task. Wire only locks between its own calls and
// unlocks before the received bytes are read out, so each whole transaction
// (write, read, Wire.read()) holds this lock.
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

inline SemaphoreHandle_t i2cMutex() {
    static SemaphoreHandle_t m = xSemaphoreCreateRecursiveMutex();
    return m;
}

struct I2CLock {
    I2CLock() { xSemaphoreTakeRecursive(i2cMutex(), portMAX_DELAY); }
    ~I2CLock() { xSemaphoreGiveRecursive(i2cMutex()); }
    I2CLock(const I2CLock &) = delete;
    I2CLock &operator=(const I2CLock &) = delete;
};
