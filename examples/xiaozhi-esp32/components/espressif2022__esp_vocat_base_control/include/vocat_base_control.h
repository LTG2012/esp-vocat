/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

// Command codes - Send
/* VOCAT_BASE_CMD_SET_ANGLE: set base angle.
 * Data format: uint16_t, big-endian, valid range 0~65535.
 */
#define VOCAT_BASE_CMD_SET_ANGLE                          0x01

/* VOCAT_BASE_CMD_SET_ACTION: trigger predefined base action.
 * Data format: uint16_t, big-endian.
 */
#define VOCAT_BASE_CMD_SET_ACTION                         0x02
#define VOCAT_BASE_CMD_SET_ACTION_SHARK_HEAD              0x0000  /**< Shake head action */
#define VOCAT_BASE_CMD_SET_ACTION_SHARK_HEAD_DECAY        0x0001  /**< Gradual decreasing shake head */
#define VOCAT_BASE_CMD_SET_ACTION_LOOK_AROUND             0x0002  /**< Look around action */
#define VOCAT_BASE_CMD_SET_ACTION_BEAT_SWING              0x0003  /**< Beat-synchronized swing */
#define VOCAT_BASE_CMD_SET_ACTION_CAT_NUZZLE              0x0004  /**< Cat nuzzle action */
#define VOCAT_BASE_CMD_SET_ACTION_TURN_LEFT               0x0005  /**< Turn left action */
#define VOCAT_BASE_CMD_SET_ACTION_TURN_RIGHT              0x0006  /**< Turn right action */
#define VOCAT_BASE_CMD_SET_ACTION_TURN_STOP               0x0007  /**< Turn stop action */

/* VOCAT_BASE_CMD_SET_CALIBRATE: start base calibration flow.
 * Data format: uint16_t, big-endian.
 * Sub command identifies the calibration operation to execute.
 */
#define VOCAT_BASE_CMD_SET_CALIBRATE                      0x03
#define VOCAT_BASE_CMD_SET_CALIBRATE_START                0x0010  /**< Start calibration */

/* VOCAT_BASE_CMD_SET_RELATIVE_ANGLE: adjust the current base angle.
 * Data format: signed int16_t, big-endian, negative for left and positive for right.
 */
#define VOCAT_BASE_CMD_SET_RELATIVE_ANGLE                 0x04

/* VOCAT_BASE_CMD_SET_MAGNETIC_MONITOR: control base magnetic monitor stream. */
#define VOCAT_BASE_CMD_SET_MAGNETIC_MONITOR                0x05
#define VOCAT_BASE_CMD_SET_MAGNETIC_MONITOR_DISABLE        0x0000
#define VOCAT_BASE_CMD_SET_MAGNETIC_MONITOR_ENABLE         0x0001

// Command codes - Receive
/* VOCAT_BASE_CMD_RECV_SLIDE_SWITCH: magnetic slide switch event.
 * Data format: uint16_t, big-endian.
 */
#define VOCAT_BASE_CMD_RECV_SLIDE_SWITCH                  0x03
//sub commands for VOCAT_BASE_CMD_RECV_SLIDE_SWITCH
#define VOCAT_BASE_CMD_RECV_SWITCH_INIT                   0x0000  /**< Initial switch state */
#define VOCAT_BASE_CMD_RECV_SWITCH_SLIDE_DOWN             0x0001  /**< Slider moved down */
#define VOCAT_BASE_CMD_RECV_SWITCH_SLIDE_UP               0x0002  /**< Slider moved up */
#define VOCAT_BASE_CMD_RECV_SWITCH_REMOVE_FROM_UP         0x0003  /**< Slider removed from upper position */
#define VOCAT_BASE_CMD_RECV_SWITCH_REMOVE_FROM_DOWN       0x0004  /**< Slider removed from lower position */
#define VOCAT_BASE_CMD_RECV_SWITCH_PLACE_FROM_UP          0x0005  /**< Slider placed from upper position */
#define VOCAT_BASE_CMD_RECV_SWITCH_PLACE_FROM_DOWN        0x0006  /**< Slider placed from lower position */
#define VOCAT_BASE_CMD_RECV_SWITCH_SINGLE_CLICK           0x0007  /**< Single click detected */
#define VOCAT_BASE_CMD_RECV_SWITCH_FISH_ATTACHED          0x0008  /**< Fish accessory attached */
#define VOCAT_BASE_CMD_RECV_SWITCH_FISH_DETACH            0x0009  /**< Fish accessory detached */
#define VOCAT_BASE_CMD_RECV_SWITCH_PAIR_DETECT            0x000A  /**< Pairing detected */
#define VOCAT_BASE_CMD_RECV_SWITCH_PAIR_CANCEL            0x000B  /**< Pairing cancelled */
#define VOCAT_BASE_CMD_RECV_SWITCH_ICE_CREAM_ATTACHED     0x000C  /**< Ice cream accessory attached */
#define VOCAT_BASE_CMD_RECV_SWITCH_ICE_CREAM_DETACHED     0x000D  /**< Ice cream accessory detached */
#define VOCAT_BASE_CMD_RECV_SWITCH_DONUT_ATTACHED         0x000E  /**< Donut accessory attached */
#define VOCAT_BASE_CMD_RECV_SWITCH_DONUT_DETACHED         0x000F  /**< Donut accessory detached */
#define VOCAT_BASE_CMD_RECV_SWITCH_IPHONE_LEAN_FRONT      0x0010  /**< Phone leaned on base from the front */
#define VOCAT_BASE_CMD_RECV_SWITCH_IPHONE_LEAN_FRONT_DETACHED 0x0011  /**< Phone removed from front lean position */
#define VOCAT_BASE_CMD_RECV_SWITCH_IPHONE_UNDER_BASE      0x0012  /**< Phone placed under the base */
#define VOCAT_BASE_CMD_RECV_SWITCH_IPHONE_UNDER_BASE_DETACHED 0x0013  /**< Phone removed from under the base */

//Calibration notifications reported by the base during the calibrate flow
#define VOCAT_BASE_CMD_RECV_CALIBRATE_START               0x0081  /**< Calibration started */
#define VOCAT_BASE_CMD_RECV_CALIBRATE_STEP1               0x0082  /**< First calibration step completed */
#define VOCAT_BASE_CMD_RECV_CALIBRATE_STEP2               0x0083  /**< Second calibration step completed */

/* VOCAT_BASE_CMD_RECV_PERCEPTION: reserved, not implemented yet.
 * Data format: variable length.
 */
#define VOCAT_BASE_CMD_RECV_PERCEPTION                    0x06

/* VOCAT_BASE_CMD_RECV_MAGNETIC_MONITOR: version(1), X/Y/Z raw values (2 each),
 * filtered magnetic value(2), delta(2), position(1). */
#define VOCAT_BASE_CMD_RECV_MAGNETIC_MONITOR               0x05

/* VOCAT_BASE_CMD_RECV_ACTION: action status notification.
 * Data format: uint16_t, big-endian.
 */
#define VOCAT_BASE_CMD_RECV_ACTION                        0x02
//sub commands for VOCAT_BASE_CMD_RECV_ACTION
#define VOCAT_BASE_CMD_RECV_ACTION_DONE                   0x0010  /**< Action completed */

/* VOCAT_BASE_CMD_RECV_HEARTBEAT: base heartbeat notification.
 * Data format: uint16_t, big-endian.
 */
#define VOCAT_BASE_CMD_RECV_HEARTBEAT                     0x00
//sub commands for VOCAT_BASE_CMD_RECV_HEARTBEAT
#define VOCAT_BASE_CMD_RECV_HEARTBEAT_ALIVE               0x0001  /**< Base is alive */

/**
 * @brief Command response callback function type
 *
 * @param cmd      Command code
 * @param data     Pointer to data buffer
 * @param data_len Length of data
 * @param user_ctx User context pointer
 */
typedef void (*vocat_base_control_cmd_callback_t)(uint8_t cmd, uint8_t *data, int data_len, void *user_ctx);

/**
 * @brief Echo base control configuration structure
 */
typedef struct {
    uart_port_t uart_num;                        /**< UART port number */
    gpio_num_t tx_pin;                           /**< UART TX pin */
    gpio_num_t rx_pin;                           /**< UART RX pin */
    uint32_t baud_rate;                          /**< UART baud rate (default: 115200) */
    uint32_t rx_buffer_size;                     /**< UART RX buffer size (default: 1024) */
    vocat_base_control_cmd_callback_t cmd_cb;    /**< Command response callback (can be NULL) */
    void *user_ctx;                              /**< User context for callback */
} vocat_base_control_config_t;

/**
 * @brief  Initialize echo base control module
 *
 *         Configure and install UART driver for communication with echo base device.
 *         UART configuration: 115200 baud rate, 8 data bits, no parity, 1 stop bit.
 *         The handle is maintained internally.
 *
 * @param  config  Configuration structure containing UART settings
 *
 * @return
 *       - ESP_OK  Initialization successful
 *       - Other   Error code if initialization failed
 */
esp_err_t vocat_base_control_init(const vocat_base_control_config_t *config);

/**
 * @brief  Set echo base angle value
 *
 *         Send control frame via UART to set the target angle of echo base device.
 *
 * @param  angle   Angle value (range: 0~65535)
 *
 * @return
 *       - ESP_OK  Setting successful
 *       - Other   Error code if setting failed
 */
esp_err_t vocat_base_control_set_angle(int angle);

/**
 * @brief Adjust the current echo base angle by a relative amount
 *
 * @param delta_angle  Relative angle in degrees, negative for left and positive for right
 *
 * @return
 *       - ESP_OK  Command sent successfully
 *       - ESP_ERR_INVALID_ARG  Relative angle is outside -180~180 degrees
 */
esp_err_t vocat_base_control_adjust_angle(int delta_angle);

/**
 * @brief  Set echo base action value
 *
 *         Send control frame via UART to set the target action of echo base device.
 *
 * @param  action  Action value
 *
 * @return
 *       - ESP_OK  Setting successful
 *       - Other   Error code if setting failed
 */
esp_err_t vocat_base_control_set_action(int action);

/**
 * @brief  Set echo base calibrate
 *
 *         Send control frame via UART to calibrate echo base device.
 *
 * @return
 *       - ESP_OK  Setting successful
 *       - Other   Error code if setting failed
 */
esp_err_t vocat_base_control_set_calibrate(void);

/** Enable or disable the base 10 Hz magnetic monitor data stream. */
esp_err_t vocat_base_control_set_magnetic_monitor(bool enabled);

/**
 * @brief  Deinitialize echo base control module
 *
 * @return
 *       - ESP_OK  Deinitialization successful
 *       - Other   Error code if deinitialization failed
 */
esp_err_t vocat_base_control_deinit(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
