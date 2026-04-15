/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#pragma once
#include "lvgl.h"

#define PAGE_TIME_UP    "TIME_UP"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create alarm time up UI with parent container
 *
 * @param parent  Parent object to create the UI in
 * @return Created container object
 */
lv_obj_t *alarm_time_up_create_with_parent(lv_obj_t *parent);

/**
 * @brief Set the origin page for time up UI (for navigation back)
 *
 * @param origin_page  The page name that triggered the time up (e.g., "SLEEP", "POMODORO")
 */
void alarm_time_up_set_origin(const char *origin_page);

/**
 * @brief Trigger snooze action (remind later 5 minutes)
 *
 * Performs the same action as clicking the "Snooze 5 min" button:
 * - If origin is SLEEP: snooze sleep timer by 5 minutes
 * - If origin is POMODORO: start new 5-minute pomodoro timer
 * - Otherwise: switch to home page
 *
 * @note This is an internal function. External modules should use alarm_api.h::alarm_time_up_snooze() instead.
 */
void alarm_time_up_snooze_impl(void);

#ifdef __cplusplus
}
#endif
