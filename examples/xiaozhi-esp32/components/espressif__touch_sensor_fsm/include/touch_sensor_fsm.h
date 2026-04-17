/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Touch sensor state definition
 *
 * This enum defines the state of the touch sensor for a specific channel.
 */
typedef enum {
    FSM_STATE_INACTIVE = 0,  /*!< Touch sensor is inactive */
    FSM_STATE_ACTIVE,        /*!< Touch sensor is active */
    FSM_STATE_DEBOUNCE,      /*!< Touch sensor is in debounce state */
} fsm_state_t;

/**
 * @brief Touch sensor control definition
 *
 * This enum defines the control command for touch sensor.
*/
typedef enum {
    FSM_CTRL_START = 0,
    FSM_CTRL_STOP,
    FSM_CTRL_RESET_BASELINE,
} fsm_ctrl_t;

/**
 * @brief Touch sensor data type definition
 *
 * This enum defines the data type for touch sensor.
*/
typedef enum {
    FSM_DATA_RAW = 0,
    FSM_DATA_SMOOTH,
    FSM_DATA_BASELINE,
    FSM_DATA_MAX,
} fsm_data_t;

/**
 * @brief Touch sensor mode definition
 *
 * This enum defines the mode for touch sensor.
 * Polling mode: The touch sensor data is updated by polling through callback
 *              registered by touch_sensor_fsm_create.
 * User push mode: The touch sensor data is updated by user push through touch_sensor_fsm_update_data.
*/
typedef enum {
    FSM_MODE_POLLING = 0,
    FSM_MODE_USER_PUSH,
} fsm_mode_t;

/**
 * @brief Touch sensor handle type
 */
typedef void *fsm_handle_t;

/**
 * @brief Touch sensor callback function type, which is called when touch sensor state changes
 */
typedef void(*fsm_state_cb_t)(fsm_handle_t handle, uint32_t channel, fsm_state_t state, uint32_t data, void *user_data);

/**
 * @brief Touch sensor polling callback function type, which is called when touch sensor data needs to be updated
 */
typedef void(*fsm_touch_polling_cb_t)(fsm_handle_t handle, uint32_t channel, uint32_t *raw_data, void *user_data);

/**
 * @brief Configuration structure for touch FSM (Finite State Machine)
 *
 * This structure defines the configuration parameters for a touch FSM, which controls the behavior of the touch FSM.
 */
typedef struct {
    fsm_mode_t mode;                                /*!< Touch sensor mode */
    uint32_t channel_num;                           /*!< Number of touch proximity sensor channels */
    uint32_t *channel_list;                         /*!< Touch proximity sensor channel list */
    float smooth_coef;                              /*!< Smoothing coefficient */
    float baseline_coef;                            /*!< Baseline coefficient */
    float gold_baseline_coef;                       /*!< Gold baseline update coefficient, controls adaptation to temperature/environmental changes */
    float max_p;                                    /*!< Maximum effective positive change rate */
    float min_n;                                    /*!< Minimum effective negative change rate */
    float *threshold_p;                             /*!< Positive threshold */
    float *threshold_n;                             /*!< Negative threshold, set if using reverse logic */
    float hysteresis_active;                        /*!< Hysteresis for active threshold */
    float hysteresis_inactive;                      /*!< Hysteresis for inactive threshold */
    float *noise_p;                                 /*!< Positive noise threshold */
    float *noise_n;                                 /*!< Negative noise threshold */
    uint32_t debounce_active;                       /*!< Debounce times for active detection */
    uint32_t debounce_inactive;                     /*!< Debounce times for inactive detection */
    uint32_t reset_cover;                           /*!< Baseline reset (after n times measure) if touch pad is covered during running */
    uint32_t reset_calibration;                     /*!< Baseline reset (after n times measure) if touch pad is covered during fsm calibration */
    uint32_t raw_buf_size;                          /*!< Size of the raw data buffer */
    uint32_t polling_interval;                      /*!< Polling interval in milliseconds */
    uint32_t scale_factor;                          /*!< Scale factor for calculations */
    uint32_t *gold_value;                           /*!< (Optional) Gold value used for baseline reset in special cases */
    uint32_t calibration_times;                     /*!< (Optional) Number of times to calibrate the touch sensor */
    fsm_state_cb_t state_cb;                        /*!< (Optional) Callback function to be called when touch sensor state changes */
    fsm_touch_polling_cb_t polling_cb;              /*!< (Optional) Callback function to be called when touch sensor data needs to be updated */
    void *user_data;                                /*!< (Optional) User data */
    bool active_low;                                /*!< (Optional) Whether the touch sensor is active low, true for ESP32 */
} fsm_config_t;

#define DEFAULTS_TOUCH_SENSOR_FSM_CONFIG()\
{\
    .mode = FSM_MODE_POLLING,\
    .channel_num = 0,\
    .channel_list = NULL,\
    .smooth_coef = 0.2f,\
    .baseline_coef = 0.1f,\
    .gold_baseline_coef = 0.01f,\
    .max_p = 0,\
    .min_n = 0,\
    .threshold_p = NULL,\
    .threshold_n = NULL,\
    .hysteresis_active = 0.1f,\
    .hysteresis_inactive = 0.1f,\
    .noise_p = NULL,\
    .noise_n = NULL,\
    .debounce_active = 2,\
    .debounce_inactive = 1,\
    .reset_cover = 1000,\
    .reset_calibration = 3,\
    .raw_buf_size = 20,\
    .polling_interval = 20,\
    .scale_factor = 100,\
    .gold_value = NULL,\
    .calibration_times = 20,\
    .state_cb = NULL,\
    .polling_cb = NULL,\
    .user_data = NULL,\
    .active_low = false,\
}

#ifndef CONFIG_FREERTOS
#define CONFIG_FREERTOS 1
#endif

#ifndef CONFIG_ESP_LOG
#define CONFIG_ESP_LOG 1
#endif

#ifdef CONFIG_FREERTOS
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#define FSM_MUTEX_CREATE() xSemaphoreCreateRecursiveMutex()
#define FSM_MUTEX_DELETE(mutex) vSemaphoreDelete(mutex)
#define FSM_MUTEX_LOCK(mutex) xSemaphoreTakeRecursive(mutex, portMAX_DELAY)
#define FSM_MUTEX_TRY_LOCK(mutex) xSemaphoreTakeRecursive(mutex, 0)
#define FSM_MUTEX_UNLOCK(mutex) xSemaphoreGiveRecursive(mutex)
#define FSM_GET_TICK_COUNT() xTaskGetTickCount()
#define MS_TO_TICKS(ms) ((ms) / portTICK_PERIOD_MS)
#else
#define FSM_MUTEX_CREATE() NULL
#define FSM_MUTEX_DELETE(mutex)
#define FSM_MUTEX_LOCK(mutex)
#define FSM_MUTEX_TRY_LOCK(mutex)
#define FSM_MUTEX_UNLOCK(mutex)
#define FSM_GET_TICK_COUNT() -1
#define MS_TO_TICKS(ms) (ms)
#endif

#ifdef CONFIG_ESP_LOG
#include "esp_log.h"
#define FSM_LOGD(tag, format, ...) ESP_LOGD(tag, format, ##__VA_ARGS__)
#define FSM_LOGI(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
#define FSM_LOGW(tag, format, ...) ESP_LOGW(tag, format, ##__VA_ARGS__)
#define FSM_LOGE(tag, format, ...) ESP_LOGE(tag, format, ##__VA_ARGS__)
#else
#include <stdio.h>
#define FSM_LOGD(tag, format, ...) printf("%s: " format "\n", tag, ##__VA_ARGS__)
#define FSM_LOGI(tag, format, ...) printf("%s: " format "\n", tag, ##__VA_ARGS__)
#define FSM_LOGW(tag, format, ...) printf("%s: " format "\n", tag, ##__VA_ARGS__)
#define FSM_LOGE(tag, format, ...) printf("%s: " format "\n", tag, ##__VA_ARGS__)
#endif

/**
 * @brief Create a touch sensor FSM
 *
 * This function creates a touch sensor FSM with the specified configuration.
 *
 * @param[in] config Pointer to the configuration structure
 * @param[out] handle Pointer to the touch sensor handle
 *
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_NO_MEM: Memory allocation failure
 */
esp_err_t touch_sensor_fsm_create(fsm_config_t *config, fsm_handle_t *handle);

/**
 * @brief Delete a touch sensor FSM
 *
 * This function deletes a touch sensor FSM.
 *
 * @param[in] handle Touch sensor handle
 *
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_STATE: Mutex is held by another task
 */
esp_err_t touch_sensor_fsm_delete(fsm_handle_t handle);

/**
 * @brief Update the data of a touch sensor FSM
 *
 * This function updates the data of a touch sensor FSM. The raw data is obtained from the touch sensor driver.
 *
 * @param[in] handle Touch sensor handle
 * @param[in] channel Touch sensor channel
 * @param[in] raw_data Raw data of the touch sensor
 * @param[in] from_isr Whether the function is called from an ISR
 *
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid arguments
 */
esp_err_t touch_sensor_fsm_update_data(fsm_handle_t handle, uint32_t channel, uint32_t raw_data, bool from_isr);

/**
 * @brief Get the data of a touch sensor FSM
 *
 * This function gets the data of a touch sensor FSM.
 *
 * @param[in] handle Touch sensor handle
 * @param[in] channel Touch sensor channel
 * @param[out] data Pointer to the data array
 *
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid arguments
 */
esp_err_t touch_sensor_fsm_get_data(fsm_handle_t handle, uint32_t channel, uint32_t *data);

/**
 * @brief Get the state of a touch sensor FSM
 *
 * This function gets the state of a touch sensor FSM.
 *
 * @param[in] handle Touch sensor handle
 * @param[in] channel Touch sensor channel
 * @param[out] state Pointer to the state of the touch sensor
 *
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid arguments
 */
esp_err_t touch_sensor_fsm_get_state(fsm_handle_t handle, uint32_t channel, fsm_state_t *state);

/**
 * @brief Control a touch sensor FSM
 *
 * This function controls a touch sensor FSM.
 *
 * @param[in] handle Touch sensor handle
 * @param[in] ctrl Control command
 * @param[in] ctrl_param Pointer to the control parameter
 *
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid arguments
 */
esp_err_t touch_sensor_fsm_control(fsm_handle_t handle, fsm_ctrl_t ctrl, void *ctrl_param);

/**
 * @brief Handle events of a touch sensor FSM
 *
 * This function handles events of a touch sensor FSM.
 * It should be called repeatedly in a task or loop context until it returns a value other than ESP_OK.
 * This ensures that all pending events are processed and the FSM operates correctly.
 *
 * @param[in] handle Touch sensor handle
 *
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid arguments
 *      - ESP_ERR_INVALID_STATE: FSM is not running
 */
esp_err_t touch_sensor_fsm_handle_events(fsm_handle_t handle);

#ifdef __cplusplus
}
#endif
