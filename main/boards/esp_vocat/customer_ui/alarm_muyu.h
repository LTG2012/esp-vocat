/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#pragma once

#include "lvgl.h"

#define PAGE_MUYU      "MUYU"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *alarm_muyu_create_with_parent(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif
