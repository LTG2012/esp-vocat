#pragma once

#include <lvgl.h>

#define UI_BRIDGE_PAGE_ATTITUDE_MONITOR "ATTITUDE_MONITOR"

lv_obj_t *attitude_monitor_create_with_parent(lv_obj_t *parent);
void attitude_monitor_show(void);
void attitude_monitor_hide(void);
