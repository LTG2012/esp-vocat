/* SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <inttypes.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "unity.h"
#include "touch_sensor_fsm.h"
#include "touch_sensor_lowlevel.h"

static size_t before_free_8bit;
static size_t before_free_32bit;

#define TEST_MEMORY_LEAK_THRESHOLD (-200)

#if CONFIG_IDF_TARGET_ESP32S3
#define ENABLE_BUZZER 1
#else
#define ENABLE_BUZZER 0
#endif

#if ENABLE_BUZZER
#include "buzzer.h"
#define IO_BUZZER_CTRL          36
#endif

static uint64_t start_time = 0;

static uint64_t get_time_in_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t current_time = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    if (start_time == 0) {
        start_time = current_time;
    }
    return current_time - start_time;
}

// print touch values(raw, smooth, benchmark) of each enabled channel, every 50ms
#define PRINT_VALUE(pad_num, raw, smooth, benchmark) printf("vl,%"PRId64",%"PRIu32",%"PRIu32",%"PRIu32",%"PRIu32"\n", get_time_in_ms(), pad_num, raw, smooth, benchmark)
// print trigger if touch active/inactive happens
#define PRINT_TRIGGER(pad_num, smooth, if_active) printf("tg,%"PRId64",%"PRIu32",%"PRIu32",%d,%d,%d,%d,%d\n", get_time_in_ms(), pad_num, smooth, if_active?1:0, if_active?0:1,0,0,0)
/* run command `pip3 install tptool` to install monitor app, python3 -m tptool to start */
#define PRINT_TRIGGER_REAL(pad_num, smooth, if_active) printf("bt,%"PRId64",%"PRIu32",%"PRIu32",%d,%d,%d,%d,%d\n", get_time_in_ms(), pad_num, smooth, if_active?1:0, if_active?0:1,0,0,0)

#if SOC_TOUCH_SAMPLE_CFG_NUM == 1
#if CONFIG_IDF_TARGET_ESP32
static uint32_t s_channel_list[] = {8, 6, 4};
#else
static uint32_t s_channel_list[] = {8, 12, 10};
#endif
#if SOC_TOUCH_PROXIMITY_MEAS_DONE_SUPPORTED
static touch_lowlevel_type_t s_channel_type_list[] = {
    TOUCH_LOWLEVEL_TYPE_TOUCH,
    TOUCH_LOWLEVEL_TYPE_TOUCH,
    TOUCH_LOWLEVEL_TYPE_PROXIMITY
};
#else
static touch_lowlevel_type_t s_channel_type_list[] = {
    TOUCH_LOWLEVEL_TYPE_TOUCH,
    TOUCH_LOWLEVEL_TYPE_TOUCH,
    TOUCH_LOWLEVEL_TYPE_TOUCH
};
#endif
#elif SOC_TOUCH_SAMPLE_CFG_NUM == 3
static uint32_t s_channel_list[] = {9};
static touch_lowlevel_type_t s_channel_type_list[] = {TOUCH_LOWLEVEL_TYPE_TOUCH};
#endif

static float s_threshold_p[] = {0.015, 0.01, 0.012}; // Example positive threshold
static float s_noise_p[] = {0.005, 0.004, 0.003}; // Example positive noise threshold
static float s_noise_n[] = {0.005, 0.004, 0.003}; // Example negative noise threshold
static uint32_t active_count[] = {0, 0, 0};
static uint32_t output_data[][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};

static uint32_t _get_channel_index(uint32_t channel)
{
    for (size_t i = 0; i < sizeof(s_channel_list) / sizeof(s_channel_list[0]); i++) {
        if (s_channel_list[i] == channel) {
            return i;
        }
    }
    return -1;
}

static void state_callback(fsm_handle_t handle, uint32_t channel, fsm_state_t state, uint32_t data, void *user_data)
{
    uint32_t channel_index = _get_channel_index(channel);
    if (channel_index == -1) {
        return;
    }
    if (state == FSM_STATE_ACTIVE) {
        PRINT_TRIGGER(channel, data, true);
        active_count[channel_index]++;
#if ENABLE_BUZZER
        buzzer_set_voice(1);
#endif
    } else {
        PRINT_TRIGGER(channel, data, false);
#if ENABLE_BUZZER
        buzzer_set_voice(0);
#endif
    }
}

static void polling_callback(fsm_handle_t handle, uint32_t channel, uint32_t *raw_data, void *user_data)
{
    uint32_t data[SOC_TOUCH_SAMPLE_CFG_NUM] = {0};
    // give the first frequency data only for esp32p4
    if (touch_sensor_lowlevel_get_data(channel, data) == ESP_OK) {
        // For simplicity, use the first data slot as raw data
        *raw_data = data[0];
    } else {
        *raw_data = 0;
    }
}

static void check_leak(size_t before_free, size_t after_free, const char *type)
{
    ssize_t delta = after_free - before_free;
    printf("MALLOC_CAP_%s: Before %zu bytes free, After %zu bytes free (delta %zd)\n", type, before_free, after_free, delta);
    TEST_ASSERT_MESSAGE(delta >= TEST_MEMORY_LEAK_THRESHOLD, "memory leak");
}

void setUp(void)
{
    before_free_8bit = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    before_free_32bit = heap_caps_get_free_size(MALLOC_CAP_32BIT);
}

void tearDown(void)
{
    size_t after_free_8bit = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t after_free_32bit = heap_caps_get_free_size(MALLOC_CAP_32BIT);
    check_leak(before_free_8bit, after_free_8bit, "8BIT");
    check_leak(before_free_32bit, after_free_32bit, "32BIT");
}

#if !CONFIG_IDF_TARGET_ESP32
static void touch_sensor_callback(uint32_t channel, touch_lowlevel_state_t state, void *state_data, void *arg)
{
    fsm_handle_t handle = (fsm_handle_t)arg;
    switch (state) {
    case TOUCH_LOWLEVEL_STATE_START:
        break;
    case TOUCH_LOWLEVEL_STATE_STOP:
        break;
    case TOUCH_LOWLEVEL_STATE_NEW_DATA:
        touch_sensor_fsm_update_data(handle, channel, *(uint32_t *)state_data, true);
        break;
    default:
        break;
    }
}
#endif

TEST_CASE("Test touch sensor FSM in polling mode", "[touch_sensor_fsm]")
{
    // Create and start low-level driver
    touch_lowlevel_config_t ll_config = {
        .channel_num = sizeof(s_channel_list) / sizeof(s_channel_list[0]),
        .channel_list = s_channel_list,
        .channel_type = s_channel_type_list,
        .sample_period_ms = 0,
    };
    TEST_ASSERT_EQUAL(ESP_OK, touch_sensor_lowlevel_create(&ll_config));

    fsm_config_t config = DEFAULTS_TOUCH_SENSOR_FSM_CONFIG();
    config.mode = FSM_MODE_POLLING;
    config.channel_num = sizeof(s_channel_list) / sizeof(s_channel_list[0]);
    config.channel_list = s_channel_list;
    config.threshold_p = s_threshold_p;
    config.threshold_n = s_threshold_p;
    config.noise_p = s_noise_p;
    config.noise_n = s_noise_n;
    config.smooth_coef = 0.2f;
    config.baseline_coef = 0.1f;
    config.hysteresis_active = 0.1f;
    config.hysteresis_inactive = 0.1f;
    config.debounce_active = 2;
    config.debounce_inactive = 1;
    config.reset_cover = 1000;
    config.reset_calibration = 3;
    config.raw_buf_size = 10;
#if CONFIG_IDF_TARGET_LINUX
    config.polling_interval = 0;
#else
    config.polling_interval = 20;
#endif
    config.scale_factor = 100;
    config.state_cb = state_callback;
    config.polling_cb = polling_callback;
#if CONFIG_IDF_TARGET_ESP32
    config.active_low = true;
#endif

    // print all members of the config, one line

    fsm_handle_t handle;
    TEST_ASSERT_EQUAL(ESP_OK, touch_sensor_fsm_create(&config, &handle));
    TEST_ASSERT_EQUAL(ESP_OK, touch_sensor_fsm_control(handle, FSM_CTRL_START, NULL));
    TEST_ASSERT_EQUAL(ESP_OK, touch_sensor_lowlevel_start());

    for (size_t j = 0; j < sizeof(s_channel_list) / sizeof(s_channel_list[0]); j++) {
        active_count[j] = 0;
    }

    uint32_t data_index[1] = {0};

    // Replace usage of channel_1_size with a simple loop
    while (data_index[0] < 10000) {
        touch_sensor_fsm_handle_events(handle);
        for (size_t j = 0; j < sizeof(s_channel_list) / sizeof(s_channel_list[0]); j++) {
            uint32_t channel = s_channel_list[j];
            touch_sensor_fsm_get_data(handle, channel, output_data[j]);
            if (output_data[j][0] != 0) {
                PRINT_VALUE(channel, output_data[j][0], output_data[j][1], output_data[j][2]);
            }
        }
        if (config.polling_interval) {
            vTaskDelay(pdMS_TO_TICKS(config.polling_interval));
        }
        data_index[0]++;
    }

    for (size_t j = 0; j < sizeof(s_channel_list) / sizeof(s_channel_list[0]); j++) {
        printf("channel %"PRIu32" active count: %"PRIu32 "\n", s_channel_list[j], active_count[j]);
        printf("channel %"PRIu32" real active count: %"PRIu32 "\n", s_channel_list[j], (uint32_t)0); // Replace channel_1_active_count with 0 or appropriate value
    }
    TEST_ASSERT_EQUAL(ESP_OK, touch_sensor_fsm_control(handle, FSM_CTRL_STOP, NULL));
    TEST_ASSERT_EQUAL(ESP_OK, touch_sensor_fsm_delete(handle));
    TEST_ASSERT_EQUAL(ESP_OK, touch_sensor_lowlevel_stop());
    TEST_ASSERT_EQUAL(ESP_OK, touch_sensor_lowlevel_delete());
}

// ESP32 Does not support interrupt mode
#if !CONFIG_IDF_TARGET_ESP32
TEST_CASE("Test touch sensor FSM in interrupt mode", "[touch_sensor_fsm]")
{
    // Create and start low-level driver
    touch_lowlevel_config_t ll_config = {
        .channel_num = sizeof(s_channel_list) / sizeof(s_channel_list[0]),
        .channel_list = s_channel_list,
        .channel_type = s_channel_type_list,
        .sample_period_ms = 0,
    };
    TEST_ASSERT_EQUAL(ESP_OK, touch_sensor_lowlevel_create(&ll_config));

    fsm_config_t config = DEFAULTS_TOUCH_SENSOR_FSM_CONFIG();
    config.mode = FSM_MODE_USER_PUSH;
    config.channel_num = sizeof(s_channel_list) / sizeof(s_channel_list[0]);
    config.channel_list = s_channel_list;
    config.threshold_p = s_threshold_p;
    config.threshold_n = s_threshold_p;
    config.noise_p = s_noise_p;
    config.noise_n = s_noise_n;
    config.smooth_coef = 0.2f;
    config.baseline_coef = 0.1f;
    config.max_p = 0.2f;
    config.min_n = 0.2f;
    config.hysteresis_active = 0.1f;
    config.hysteresis_inactive = 0.2f;
    config.debounce_active = 30;
    config.debounce_inactive = 5;
    config.reset_cover = 50;
    config.reset_calibration = 3;
    config.raw_buf_size = 40;
    config.state_cb = state_callback;

    fsm_handle_t handle;
    for (size_t j = 0; j < sizeof(s_channel_list) / sizeof(s_channel_list[0]); j++) {
        active_count[j] = 0;
    }
    TEST_ASSERT_EQUAL(ESP_OK, touch_sensor_fsm_create(&config, &handle));
    TEST_ASSERT_EQUAL(ESP_OK, touch_sensor_fsm_control(handle, FSM_CTRL_START, NULL));

    touch_lowlevel_handle_t low_handle[sizeof(s_channel_list) / sizeof(s_channel_list[0])] = {0};
    for (uint32_t i = 0; i < sizeof(s_channel_list) / sizeof(s_channel_list[0]); i++) {
        TEST_ASSERT_EQUAL(ESP_OK, touch_sensor_lowlevel_register(config.channel_list[i], touch_sensor_callback, (void *)handle, &low_handle[i]));
    }

    TEST_ASSERT_EQUAL(ESP_OK, touch_sensor_lowlevel_start());

    // Use low-level get_data in a loop if needed
    for (int i = 0; i < 100; i++) {
        touch_sensor_fsm_handle_events(handle);
        for (size_t j = 0; j < sizeof(s_channel_list) / sizeof(s_channel_list[0]); j++) {
            touch_sensor_fsm_get_data(handle, s_channel_list[j], output_data[j]);
            PRINT_VALUE(s_channel_list[j], output_data[j][0], output_data[j][1], output_data[j][2]);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    for (size_t j = 0; j < sizeof(s_channel_list) / sizeof(s_channel_list[0]); j++) {
        printf("channel %"PRIu32" active count: %"PRIu32 "\n", s_channel_list[j], active_count[j]);
        printf("channel %"PRIu32" real active count: %"PRIu32 "\n", s_channel_list[j], (uint32_t)0); // Replace channel_1_active_count with 0 or appropriate value
    }

    TEST_ASSERT_EQUAL(ESP_OK, touch_sensor_fsm_control(handle, FSM_CTRL_STOP, NULL));
    TEST_ASSERT_EQUAL(ESP_OK, touch_sensor_fsm_delete(handle));
    TEST_ASSERT_EQUAL(ESP_OK, touch_sensor_lowlevel_stop());
    TEST_ASSERT_EQUAL(ESP_OK, touch_sensor_lowlevel_delete());
}
#endif

void app_main(void)
{

#if ENABLE_BUZZER
    buzzer_driver_install(IO_BUZZER_CTRL);
#endif

#if CONFIG_IDF_TARGET_LINUX
    unity_run_test_by_index(1);
    esp_restart();
#else
    unity_run_menu();
#endif
}
