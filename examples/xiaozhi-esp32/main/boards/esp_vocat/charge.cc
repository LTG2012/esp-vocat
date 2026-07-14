#include "charge.h"

#include <algorithm>

namespace {
constexpr uint8_t kVoltageRegister = 0x08;
constexpr uint8_t kCurrentRegister = 0x0C;
constexpr uint8_t kStateOfChargeRegister = 0x2C;

uint16_t ReadWord(const uint8_t* bytes) {
    return static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8);
}
}  // namespace

Charge::Charge(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr)
{
}

bool Charge::GetBatteryStatus(ChargeStatus& status) {
    uint8_t voltage_bytes[2];
    uint8_t current_bytes[2];
    uint8_t level_bytes[2];

    ReadRegs(kVoltageRegister, voltage_bytes, sizeof(voltage_bytes));
    ReadRegs(kCurrentRegister, current_bytes, sizeof(current_bytes));
    ReadRegs(kStateOfChargeRegister, level_bytes, sizeof(level_bytes));

    status.voltage_mv = ReadWord(voltage_bytes);
    status.current_ma = static_cast<int16_t>(ReadWord(current_bytes));
    status.level = std::min<int>(ReadWord(level_bytes), 100);
    return true;
}
