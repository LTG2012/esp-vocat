#include "bmi270_imu.h"

#include <cstdlib>
#include <cstring>

#include <esp_log.h>
#include <esp_rom_sys.h>
#include <esp_timer.h>
#include <bmi270_api.h>

extern "C" int8_t bmi270_init(struct bmi2_dev *dev);

namespace {

constexpr char kTag[] = "bmi270_imu";

BMI2_INTF_RETURN_TYPE I2cRead(uint8_t reg, uint8_t *data, uint32_t length, void *context)
{
    const esp_err_t result = i2c_master_transmit_receive(
        static_cast<i2c_master_dev_handle_t>(context), &reg, 1, data, length, 100);
    return result == ESP_OK ? BMI2_INTF_RET_SUCCESS : BMI2_E_COM_FAIL;
}

BMI2_INTF_RETURN_TYPE I2cWrite(uint8_t reg, const uint8_t *data, uint32_t length, void *context)
{
    if (length > 128) {
        return BMI2_E_COM_FAIL;
    }
    uint8_t buffer[129];
    buffer[0] = reg;
    std::memcpy(buffer + 1, data, length);
    const esp_err_t result = i2c_master_transmit(
        static_cast<i2c_master_dev_handle_t>(context), buffer, length + 1, 100);
    return result == ESP_OK ? BMI2_INTF_RET_SUCCESS : BMI2_E_COM_FAIL;
}

void DelayUs(uint32_t period, void *)
{
    esp_rom_delay_us(period);
}

}  // namespace

Bmi270Imu::Bmi270Imu(i2c_master_bus_handle_t bus) : bus_(bus)
{
    mutex_ = xSemaphoreCreateMutex();
}

Bmi270Imu::~Bmi270Imu()
{
    stop_ = true;
    for (int i = 0; task_ != nullptr && i < 50; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (device_) {
        i2c_master_bus_rm_device(static_cast<i2c_master_dev_handle_t>(device_));
    }
    std::free(sensor_);
    if (mutex_) {
        vSemaphoreDelete(mutex_);
    }
}

bool Bmi270Imu::Start()
{
    if (!bus_ || !mutex_ || task_) {
        return false;
    }
    return xTaskCreate(TaskEntry, "bmi270", 4096, this, 4, &task_) == pdPASS;
}

void Bmi270Imu::TaskEntry(void *arg)
{
    static_cast<Bmi270Imu *>(arg)->Run();
}

bool Bmi270Imu::InitializeSensor()
{
    constexpr uint8_t addresses[] = {0x68, 0x69};
    for (uint8_t candidate : addresses) {
        if (i2c_master_probe(bus_, candidate, 50) == ESP_OK) {
            address_ = candidate;
            break;
        }
    }
    if (!address_) {
        return false;
    }

    i2c_device_config_t device_config = {};
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = address_;
    device_config.scl_speed_hz = 400000;
    i2c_master_dev_handle_t device = nullptr;
    if (i2c_master_bus_add_device(bus_, &device_config, &device) != ESP_OK) {
        return false;
    }
    device_ = device;

    sensor_ = static_cast<bmi2_dev *>(std::calloc(1, sizeof(bmi2_dev)));
    if (!sensor_) {
        return false;
    }
    sensor_->intf = BMI2_I2C_INTF;
    sensor_->intf_ptr = device_;
    sensor_->read = I2cRead;
    sensor_->write = I2cWrite;
    sensor_->delay_us = DelayUs;
    sensor_->read_write_len = 128;
    const int8_t init_status = bmi270_init(sensor_);
    if (init_status != BMI2_OK) {
        ESP_LOGW(kTag, "BMI270 init failed at 0x%02x: %d", address_, init_status);
        return false;
    }

    bmi2_sens_config config = {};
    config.type = BMI2_ACCEL;
    if (bmi2_get_sensor_config(&config, 1, sensor_) != BMI2_OK) {
        return false;
    }
    config.cfg.acc.odr = BMI2_ACC_ODR_100HZ;
    config.cfg.acc.range = BMI2_ACC_RANGE_4G;
    config.cfg.acc.bwp = BMI2_ACC_NORMAL_AVG4;
    config.cfg.acc.filter_perf = BMI2_PERF_OPT_MODE;
    const uint8_t sensor = BMI2_ACCEL;
    if (bmi2_set_sensor_config(&config, 1, sensor_) != BMI2_OK ||
        bmi2_sensor_enable(&sensor, 1, sensor_) != BMI2_OK) {
        return false;
    }

    ESP_LOGI(kTag, "BMI270 accelerometer running at 0x%02x", address_);
    return true;
}

bool Bmi270Imu::ReadSample(float accel_g[3])
{
    bmi2_sens_data data = {};
    if (!sensor_ || bmi2_get_sensor_data(&data, sensor_) != BMI2_OK ||
        (data.status & BMI2_DRDY_ACC) == 0) {
        return false;
    }
    constexpr float scale = BMI2_ACC_RANGE_4G_VAL / 32768.0f;
    accel_g[0] = data.acc.x * scale;
    accel_g[1] = data.acc.y * scale;
    accel_g[2] = data.acc.z * scale;
    return true;
}

void Bmi270Imu::Run()
{
    if (!InitializeSensor()) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        snapshot_.state = address_ ? ImuState::kFault : ImuState::kNotFound;
        xSemaphoreGive(mutex_);
        task_ = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    int failures = 0;
    while (!stop_) {
        float accel[3];
        if (ReadSample(accel)) {
            AttitudeSnapshot next;
            next.state = ImuState::kRunning;
            std::memcpy(next.accel_g, accel, sizeof(accel));
            next.sample_age_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
            xSemaphoreTake(mutex_, portMAX_DELAY);
            snapshot_ = next;
            xSemaphoreGive(mutex_);
            failures = 0;
        } else if (++failures >= 3) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            snapshot_.state = ImuState::kFault;
            xSemaphoreGive(mutex_);
            failures = 0;
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        vTaskDelay(pdMS_TO_TICKS(high_rate_ ? 10 : 40));
    }
    task_ = nullptr;
    vTaskDelete(nullptr);
}

AttitudeSnapshot Bmi270Imu::GetSnapshot() const
{
    AttitudeSnapshot result;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    result = snapshot_;
    xSemaphoreGive(mutex_);
    if (result.sample_age_ms) {
        result.sample_age_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000) - result.sample_age_ms;
    }
    return result;
}

void Bmi270Imu::SetHighRate(bool high_rate)
{
    high_rate_ = high_rate;
}
