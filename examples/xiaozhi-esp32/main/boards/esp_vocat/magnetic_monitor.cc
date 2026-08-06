#include "magnetic_monitor.h"

#include <esp_lv_adapter.h>
#include <esp_timer.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

#include "customer_ui/alarm_manager.h"

namespace {

constexpr uint32_t kHistorySize = 60;
constexpr int64_t kDataTimeoutUs = 1000 * 1000;

struct MagneticMonitorUi {
    lv_obj_t *container = nullptr;
    lv_obj_t *title_label = nullptr;
    lv_obj_t *connection_label = nullptr;
    lv_obj_t *value_label = nullptr;
    lv_obj_t *delta_label = nullptr;
    lv_obj_t *chart = nullptr;
    lv_chart_series_t *series = nullptr;
    lv_obj_t *raw_label = nullptr;
    lv_obj_t *position_label = nullptr;
    lv_obj_t *event_label = nullptr;
    int16_t history[kHistorySize] = {};
    uint32_t history_count = 0;
    int64_t last_sample_us = 0;
    bool active = false;
    bool calibration_mode = false;
};

MagneticMonitorUi s_ui;

const char *position_text(uint8_t position)
{
    switch (position) {
    case 1: return "已取下";
    case 2: return "上位";
    case 3: return "下位";
    default: return "识别中";
    }
}

const char *event_text(uint16_t event)
{
    switch (event) {
    case 0x0001: return "下滑";
    case 0x0002: return "上滑";
    case 0x0003: return "从上位取下";
    case 0x0004: return "从下位取下";
    case 0x0005: return "从上位放入";
    case 0x0006: return "从下位放入";
    case 0x0007: return "单击";
    case 0x0008: return "喂鱼配件吸附";
    case 0x0009: return "喂鱼配件取下";
    case 0x000A: return "进入配对状态";
    case 0x000B: return "退出配对状态";
    default: return "等待动作";
    }
}

const char *calibration_step_text(uint8_t state)
{
    switch (state) {
    case 1: return "第1步：保持当前位置";
    case 2: return "第2步：移动到反向位置";
    case 3: return "第2步：保持反向位置";
    case 4: return "第3步：取下滑块";
    case 5: return "第3步：保持取下状态";
    case 6: return "校准完成";
    default: return "等待底座数据";
    }
}

const char *calibration_reason_text(uint8_t state, uint8_t flags)
{
    if ((flags & (1 << 1)) == 0) return "状态：等待数据";
    if ((flags & (1 << 5)) != 0) return "状态：校准完成";
    if ((flags & (1 << 2)) == 0) return "状态：请静止";
    if ((state == 2 && (flags & (1 << 3)) == 0) ||
        (state >= 4 && (flags & (1 << 4)) == 0)) {
        return "状态：差值不足";
    }
    return "状态：稳定中";
}

void update_chart_locked(int16_t filtered_value)
{
    if (s_ui.history_count < kHistorySize) {
        s_ui.history[s_ui.history_count++] = filtered_value;
    } else {
        memmove(s_ui.history, s_ui.history + 1, (kHistorySize - 1) * sizeof(s_ui.history[0]));
        s_ui.history[kHistorySize - 1] = filtered_value;
    }

    int16_t min_value = s_ui.history[0];
    int16_t max_value = s_ui.history[0];
    for (uint32_t index = 1; index < s_ui.history_count; ++index) {
        if (s_ui.history[index] < min_value) min_value = s_ui.history[index];
        if (s_ui.history[index] > max_value) max_value = s_ui.history[index];
    }
    const int32_t padding = 60;
    lv_chart_set_range(s_ui.chart, LV_CHART_AXIS_PRIMARY_Y,
                       LV_MAX(0, min_value - padding), max_value + padding);
    lv_chart_set_next_value(s_ui.chart, s_ui.series, filtered_value);
    lv_chart_refresh(s_ui.chart);
}

void reset_ui_locked()
{
    s_ui.history_count = 0;
    s_ui.last_sample_us = 0;
    memset(s_ui.history, 0, sizeof(s_ui.history));
    lv_chart_set_all_values(s_ui.chart, s_ui.series, LV_CHART_POINT_NONE);
    lv_chart_set_range(s_ui.chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_obj_set_style_text_font(s_ui.title_label,
                               s_ui.calibration_mode ? &ui_font_magnetic_calibration_24
                                                     : &ui_font_magnetic_monitor_24, 0);
    lv_obj_set_style_text_font(s_ui.connection_label,
                               s_ui.calibration_mode ? &ui_font_magnetic_calibration_18
                                                     : &ui_font_magnetic_monitor_18, 0);
    lv_obj_set_style_text_font(s_ui.delta_label,
                               s_ui.calibration_mode ? &ui_font_magnetic_calibration_18
                                                     : &ui_font_magnetic_monitor_18, 0);
    lv_obj_set_style_text_font(s_ui.raw_label,
                               s_ui.calibration_mode ? &ui_font_magnetic_calibration_18
                                                     : &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_font(s_ui.position_label,
                               s_ui.calibration_mode ? &ui_font_magnetic_calibration_18
                                                     : &ui_font_magnetic_monitor_18, 0);
    lv_obj_set_style_text_font(s_ui.event_label,
                               s_ui.calibration_mode ? &ui_font_magnetic_calibration_18
                                                     : &ui_font_magnetic_monitor_18, 0);
    lv_label_set_text(s_ui.title_label, s_ui.calibration_mode ? "磁吸校准" : "滑块监测");
    lv_label_set_text(s_ui.connection_label,
                      s_ui.calibration_mode ? "第1步：保持当前位置" : "等待数据");
    lv_label_set_text(s_ui.value_label, "--");
    lv_label_set_text(s_ui.delta_label, s_ui.calibration_mode ? "变化  --" : "变化  --");
    lv_label_set_text(s_ui.raw_label,
                      s_ui.calibration_mode ? "稳定 --/500 ms  波动 --" : "X: --   Y: --   Z: --");
    lv_label_set_text(s_ui.position_label,
                      s_ui.calibration_mode ? "D1 --/70  D2 --/70" : "当前状态：识别中");
    lv_label_set_text(s_ui.event_label,
                      s_ui.calibration_mode ? "状态：等待数据" : "最近动作：等待动作");
    lv_chart_refresh(s_ui.chart);
}

void stale_check_timer(lv_timer_t *timer)
{
    (void)timer;
    if (!s_ui.active || s_ui.last_sample_us == 0) {
        return;
    }
    if (esp_timer_get_time() - s_ui.last_sample_us > kDataTimeoutUs) {
        lv_label_set_text(s_ui.connection_label, "等待数据");
    }
}

} // namespace

lv_obj_t *magnetic_monitor_create_with_parent(lv_obj_t *parent)
{
    s_ui.container = lv_obj_create(parent);
    lv_obj_set_size(s_ui.container, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(s_ui.container, lv_color_hex(0x0D1520), 0);
    lv_obj_set_style_border_width(s_ui.container, 0, 0);
    lv_obj_set_style_pad_all(s_ui.container, 0, 0);
    lv_obj_clear_flag(s_ui.container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_ui.container);
    s_ui.title_label = title;
    lv_label_set_text(title, "滑块监测");
    lv_obj_set_style_text_color(title, lv_color_hex(0xF4F7FB), 0);
    lv_obj_set_style_text_font(title, &ui_font_magnetic_monitor_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    s_ui.connection_label = lv_label_create(s_ui.container);
    lv_label_set_text(s_ui.connection_label, "等待数据");
    lv_obj_set_style_text_color(s_ui.connection_label, lv_color_hex(0x7FB3D5), 0);
    lv_obj_set_style_text_font(s_ui.connection_label, &ui_font_magnetic_monitor_18, 0);
    lv_obj_align(s_ui.connection_label, LV_ALIGN_TOP_MID, 0, 46);

    s_ui.value_label = lv_label_create(s_ui.container);
    lv_label_set_text(s_ui.value_label, "--");
    lv_obj_set_style_text_color(s_ui.value_label, lv_color_hex(0x4CE0B3), 0);
    lv_obj_set_style_text_font(s_ui.value_label, &lv_font_montserrat_32, 0);
    lv_obj_align(s_ui.value_label, LV_ALIGN_TOP_MID, -68, 74);

    s_ui.delta_label = lv_label_create(s_ui.container);
    lv_label_set_text(s_ui.delta_label, "变化  --");
    lv_obj_set_style_text_color(s_ui.delta_label, lv_color_hex(0xB8C4D3), 0);
    lv_obj_set_style_text_font(s_ui.delta_label, &ui_font_magnetic_monitor_18, 0);
    lv_obj_align(s_ui.delta_label, LV_ALIGN_TOP_MID, 70, 84);

    s_ui.chart = lv_chart_create(s_ui.container);
    lv_obj_set_size(s_ui.chart, 280, 104);
    lv_obj_align(s_ui.chart, LV_ALIGN_TOP_MID, 0, 118);
    lv_chart_set_type(s_ui.chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_ui.chart, kHistorySize);
    lv_chart_set_update_mode(s_ui.chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_div_line_count(s_ui.chart, 3, 5);
    lv_chart_set_range(s_ui.chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_obj_set_style_bg_color(s_ui.chart, lv_color_hex(0x152435), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui.chart, lv_color_hex(0x2A435B), LV_PART_MAIN);
    lv_obj_set_style_line_color(s_ui.chart, lv_color_hex(0x2A435B), LV_PART_MAIN);
    lv_obj_set_style_width(s_ui.chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_height(s_ui.chart, 0, LV_PART_INDICATOR);
    s_ui.series = lv_chart_add_series(s_ui.chart, lv_color_hex(0x4CE0B3), LV_CHART_AXIS_PRIMARY_Y);

    s_ui.raw_label = lv_label_create(s_ui.container);
    lv_label_set_text(s_ui.raw_label, "X: --   Y: --   Z: --");
    lv_obj_set_style_text_color(s_ui.raw_label, lv_color_hex(0xE3EAF2), 0);
    lv_obj_set_style_text_font(s_ui.raw_label, &lv_font_montserrat_20, 0);
    lv_obj_align(s_ui.raw_label, LV_ALIGN_TOP_MID, 0, 232);

    s_ui.position_label = lv_label_create(s_ui.container);
    lv_label_set_text(s_ui.position_label, "当前状态：识别中");
    lv_obj_set_style_text_color(s_ui.position_label, lv_color_hex(0xF9C74F), 0);
    lv_obj_set_style_text_font(s_ui.position_label, &ui_font_magnetic_monitor_18, 0);
    lv_obj_align(s_ui.position_label, LV_ALIGN_TOP_MID, 0, 258);

    s_ui.event_label = lv_label_create(s_ui.container);
    lv_label_set_text(s_ui.event_label, "最近动作：等待动作");
    lv_obj_set_style_text_color(s_ui.event_label, lv_color_hex(0xF4F7FB), 0);
    lv_obj_set_style_text_font(s_ui.event_label, &ui_font_magnetic_monitor_18, 0);
    lv_obj_align(s_ui.event_label, LV_ALIGN_TOP_MID, 0, 284);

    lv_obj_add_flag(s_ui.container, LV_OBJ_FLAG_HIDDEN);
    lv_timer_create(stale_check_timer, 250, nullptr);
    return s_ui.container;
}

void magnetic_monitor_show(void)
{
    if (s_ui.container == nullptr) return;
    esp_lv_adapter_lock(-1);
    s_ui.active = true;
    reset_ui_locked();
    esp_lv_adapter_unlock();
}

void magnetic_monitor_hide(void)
{
    esp_lv_adapter_lock(-1);
    s_ui.active = false;
    s_ui.calibration_mode = false;
    esp_lv_adapter_unlock();
}

bool magnetic_monitor_is_active(void)
{
    return s_ui.active;
}

void magnetic_monitor_begin_calibration(void)
{
    if (s_ui.container == nullptr) return;

    esp_lv_adapter_lock(-1);
    s_ui.calibration_mode = true;
    if (s_ui.active) {
        reset_ui_locked();
    }
    esp_lv_adapter_unlock();
}

void magnetic_monitor_handle_sample(int16_t x, int16_t y, int16_t z,
                                    int16_t filtered_value, int16_t delta,
                                    uint8_t position)
{
    if (!s_ui.active || s_ui.calibration_mode || s_ui.chart == nullptr) return;
    esp_lv_adapter_lock(-1);
    if (!s_ui.active || s_ui.calibration_mode) {
        esp_lv_adapter_unlock();
        return;
    }

    update_chart_locked(filtered_value);

    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%d", filtered_value);
    lv_label_set_text(s_ui.value_label, buffer);
    snprintf(buffer, sizeof(buffer), "变化  %+d", delta);
    lv_label_set_text(s_ui.delta_label, buffer);
    snprintf(buffer, sizeof(buffer), "X: %d   Y: %d   Z: %d", x, y, z);
    lv_label_set_text(s_ui.raw_label, buffer);
    snprintf(buffer, sizeof(buffer), "当前状态：%s", position_text(position));
    lv_label_set_text(s_ui.position_label, buffer);
    lv_label_set_text(s_ui.connection_label, "数据正常");
    s_ui.last_sample_us = esp_timer_get_time();
    esp_lv_adapter_unlock();
}

void magnetic_monitor_handle_calibration_status(uint8_t state, uint8_t flags,
                                                 int16_t filtered_value, int16_t delta,
                                                 int16_t variation,
                                                 uint16_t stable_elapsed_ms,
                                                 uint16_t stable_required_ms,
                                                 int16_t diff_first, int16_t diff_second,
                                                 uint16_t min_difference,
                                                 int16_t point_first, int16_t point_second,
                                                 int16_t point_third)
{
    if (!s_ui.active || !s_ui.calibration_mode || s_ui.chart == nullptr) return;

    esp_lv_adapter_lock(-1);
    if (!s_ui.active || !s_ui.calibration_mode) {
        esp_lv_adapter_unlock();
        return;
    }

    update_chart_locked(filtered_value);

    char buffer[80];
    lv_label_set_text(s_ui.connection_label, calibration_step_text(state));
    snprintf(buffer, sizeof(buffer), "%d", filtered_value);
    lv_label_set_text(s_ui.value_label, buffer);
    snprintf(buffer, sizeof(buffer), "变化  %+d", delta);
    lv_label_set_text(s_ui.delta_label, buffer);

    if ((flags & (1 << 5)) != 0) {
        snprintf(buffer, sizeof(buffer), "记录：%d / %d / %d",
                 point_first, point_second, point_third);
    } else {
        snprintf(buffer, sizeof(buffer), "稳定 %u/%u ms  波动 %d",
                 stable_elapsed_ms, stable_required_ms, variation);
    }
    lv_label_set_text(s_ui.raw_label, buffer);
    snprintf(buffer, sizeof(buffer), "D1 %d/%u  D2 %d/%u",
             diff_first, min_difference, diff_second, min_difference);
    lv_label_set_text(s_ui.position_label, buffer);
    lv_label_set_text(s_ui.event_label, calibration_reason_text(state, flags));
    s_ui.last_sample_us = esp_timer_get_time();
    esp_lv_adapter_unlock();
}

void magnetic_monitor_record_event(uint16_t event)
{
    if (!s_ui.active || s_ui.calibration_mode || s_ui.event_label == nullptr) return;
    esp_lv_adapter_lock(-1);
    if (s_ui.active && !s_ui.calibration_mode) {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "最近动作：%s", event_text(event));
        lv_label_set_text(s_ui.event_label, buffer);
    }
    esp_lv_adapter_unlock();
}
