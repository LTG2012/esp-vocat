#ifndef CHARGE_H
#define CHARGE_H

#include "i2c_device.h"
#include <driver/i2c_master.h>

struct ChargeStatus {
    int level = 0;
    int voltage_mv = 0;
    int current_ma = 0;
};

class Charge : public I2cDevice {
public:
    Charge(i2c_master_bus_handle_t i2c_bus, uint8_t addr);
    bool GetBatteryStatus(ChargeStatus& status);
};

#endif // CHARGE_H
