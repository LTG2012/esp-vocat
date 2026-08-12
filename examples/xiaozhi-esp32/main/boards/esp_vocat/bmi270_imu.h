#pragma once

#include <cstdint>

#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

enum class ImuState {
    kNotFound,
    kRunning,
    kFault,
};

struct AttitudeSnapshot {
    ImuState state = ImuState::kNotFound;
    float accel_g[3] = {};
    uint32_t sample_age_ms = 0;
};

class Bmi270Imu {
public:
    explicit Bmi270Imu(i2c_master_bus_handle_t bus);
    ~Bmi270Imu();

    bool Start();
    AttitudeSnapshot GetSnapshot() const;
    void SetHighRate(bool high_rate);

private:
    static void TaskEntry(void *arg);
    void Run();
    bool InitializeSensor();
    bool ReadSample(float accel_g[3]);

    i2c_master_bus_handle_t bus_ = nullptr;
    void *device_ = nullptr;
    struct bmi2_dev *sensor_ = nullptr;
    mutable SemaphoreHandle_t mutex_ = nullptr;
    TaskHandle_t task_ = nullptr;
    AttitudeSnapshot snapshot_;
    volatile bool high_rate_ = false;
    volatile bool stop_ = false;
    uint8_t address_ = 0;
};
