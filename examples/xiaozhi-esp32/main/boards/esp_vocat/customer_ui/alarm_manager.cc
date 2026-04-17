#include <string.h>  /* For strcmp */
#include <esp_log.h>
#include <esp_lv_adapter.h>
#include "alarm_manager.h"
#include "alarm_api.h"
#include "csi_ui/ui/ui.h"
#include "csi_ui/esp_radar_csi.h"

#define TAG "alarm_manager"

using esp_brookesia::apps::RadarCSI;

static constexpr const char* PAGE_SCREEN_W = "SCREEN_W";
static constexpr const char* PAGE_CSI_BEHAV = "CSI_BEHAV";

// ============================================================================
// Type Definitions
// ============================================================================

// ============================================================================
// Static Variables
// ============================================================================

/* UI container objects (main_ui specific) */
static lv_obj_t *container_pomodoro = NULL;
static lv_obj_t *container_sleep = NULL;
static lv_obj_t *container_time_up = NULL;
static lv_obj_t *container_muyu = NULL;
static lv_obj_t *container_screenW = NULL;
static lv_obj_t *container_csi_behav = NULL;


// ============================================================================
// Global Variables
// ============================================================================

void main_ui_switch_page(const char *page_name)
{
    ESP_LOGD(TAG, "Switching to page: %s", page_name);
    ui_bridge_switch_page(page_name);
}

static bool main_ui_page_switch_callback(const char *target_page, void *user_data)
{
    const char *current_page = ui_bridge_get_current_page();
    ESP_LOGI(TAG, "Page switch: %s -> %s", current_page ? current_page : "NULL", target_page ? target_page : "NULL");

    /* Special handling for pomodoro page */
    if (target_page != NULL && strcmp(target_page, PAGE_POMODORO) == 0 &&
            (current_page == NULL || strcmp(current_page, PAGE_POMODORO) != 0)) {
        alarm_start_pomodoro(5);
        return true;  /* Handled, skip default switch */
    }

    /* CSI waveform page lifecycle */
    if (target_page != NULL && strcmp(target_page, PAGE_SCREEN_W) == 0 &&
            (current_page == NULL || strcmp(current_page, PAGE_SCREEN_W) != 0)) {
        RadarCSI *radar = RadarCSI::getInstance();
        if (radar != nullptr) {
            ESP_LOGI(TAG, "Enter SCREEN_W: start CSI pipeline");
            radar->init();
            radar->initCharts();
            radar->resumeDataProcessing();
            radar->startPing();
            radar->startPipeline();
        }
    }

    if (current_page != NULL && strcmp(current_page, PAGE_SCREEN_W) == 0 &&
            (target_page == NULL || strcmp(target_page, PAGE_SCREEN_W) != 0)) {
        RadarCSI *radar = RadarCSI::getInstance();
        if (radar != nullptr) {
            ESP_LOGI(TAG, "Leave SCREEN_W: stop ping");
            radar->stopPing();
        }
    }

    return false;  /* Use default switch */
}

void alarm_create_ui()
{
    lv_obj_t *scr = lv_scr_act();
    /* Create and register pomodoro container */
    container_pomodoro = alarm_pomodoro_create_with_parent(scr);
    ui_bridge_register_page_with_cycle(PAGE_POMODORO, &container_pomodoro, true);

    /* Create and register sleep container */
    container_sleep = alarm_sleep_24h_create_with_parent(scr);
    ui_bridge_register_page_with_cycle(PAGE_SLEEP, &container_sleep, true);

    /* Create and register muuyu container */
    container_muyu = alarm_muyu_create_with_parent(scr);
    ui_bridge_register_page_with_cycle(PAGE_MUYU, &container_muyu, true);

    /* Create and register time up container */
    container_time_up = alarm_time_up_create_with_parent(scr);
    ui_bridge_register_page_with_cycle(PAGE_TIME_UP, &container_time_up, false);

    // /* Create and register CSI pages */
    // container_screenW = ui_ScreenW_screen_init(scr);
    // ui_bridge_register_page_with_cycle(PAGE_SCREEN_W, &container_screenW, true);

    // container_csi_behav = ui_csi_behav_Screen_screen_init(scr);
    // ui_bridge_register_page_with_cycle(PAGE_CSI_BEHAV, &container_csi_behav, true);

    // /* Register page switch callback for custom handling (e.g., pomodoro) */
    // ui_bridge_set_page_switch_callback(main_ui_page_switch_callback, NULL);
}

void alarm_start_pomodoro(int32_t minutes)
{
    if (minutes <= 0) {
        minutes = 5;
    }

    ESP_LOGI(TAG, "Show pomodoro page with %ld minutes", (long)minutes);

    /* Configure pomodoro timer first, then switch page */
    alarm_pomodoro_reset_to_zero();
    alarm_pomodoro_adjust_end_point(minutes);
    main_ui_switch_page(PAGE_POMODORO);
}

bool alarm_resume_pomodoro(void)
{
    ESP_LOGI(TAG, "Start pomodoro timer");
    const char *current_page = ui_bridge_get_current_page();
    if (current_page == NULL || strcmp(current_page, PAGE_POMODORO) != 0) {
        ESP_LOGI(TAG, "Not on pomodoro page (current=%s)",
                 current_page ? current_page : "NULL");
        return false;
    } else {
        alarm_pomodoro_start();
    }
    return true;
}

bool alarm_pause_pomodoro(void)
{
    ESP_LOGI(TAG, "Pause pomodoro timer");
    const char *current_page = ui_bridge_get_current_page();
    if (current_page == NULL || strcmp(current_page, PAGE_POMODORO) != 0) {
        ESP_LOGI(TAG, "Not on pomodoro page (current=%s)",
                 current_page ? current_page : "NULL");
        return false;
    } else {
        alarm_pomodoro_pause();
    }
    return true;
}

void alarm_start_sleep(int32_t end_hour, int32_t end_min)
{
    /* Validate and clamp values */
    const int32_t DEFAULT_END_HOUR = 8;

    if (end_hour < 0 || end_hour >= 24) {
        ESP_LOGW(TAG, "Invalid end_hour: %ld, using default %ld", (long)end_hour, (long)DEFAULT_END_HOUR);
        end_hour = DEFAULT_END_HOUR;
    }
    if (end_min < 0 || end_min >= 60) {
        ESP_LOGW(TAG, "Invalid end_min: %ld, using 0", (long)end_min);
        end_min = 0;
    }

    ESP_LOGI(TAG, "Show sleep page with end time: %02ld:%02ld (start time will be current time)",
             (long)end_hour, (long)end_min);

    /* Configure sleep timer first (start time is current time), then switch page */
    alarm_sleep_24h_set_end_time(end_hour, end_min);
    main_ui_switch_page(PAGE_SLEEP);
}

void alarm_set_sleep_end_time(int32_t end_hour, int32_t end_min)
{
    /* Validate and clamp values */
    const int32_t DEFAULT_END_HOUR = 8;

    if (end_hour < 0 || end_hour >= 24) {
        ESP_LOGW(TAG, "Invalid end_hour: %ld, using default %ld", (long)end_hour, (long)DEFAULT_END_HOUR);
        end_hour = DEFAULT_END_HOUR;
    }
    if (end_min < 0 || end_min >= 60) {
        ESP_LOGW(TAG, "Invalid end_min: %ld, using 0", (long)end_min);
        end_min = 0;
    }

    /* Delegate to sleep_24h module */
    alarm_sleep_24h_set_end_time(end_hour, end_min);
}

bool alarm_get_sleep_end_time(int32_t *end_hour, int32_t *end_min)
{
    /* Delegate to sleep_24h module */
    return alarm_sleep_24h_get_end_time(end_hour, end_min);
}

bool alarm_toggle_sleep_duration_display(void)
{
    ESP_LOGI(TAG, "Toggle sleep duration display");
    const char *current_page = ui_bridge_get_current_page();
    if (current_page == NULL || strcmp(current_page, PAGE_SLEEP) != 0) {
        ESP_LOGI(TAG, "Not on sleep page (current=%s)",
                 current_page ? current_page : "NULL");
        return false;
    } else {
        alarm_sleep_24h_trigger_center_btn();
    }
    return true;
}

bool alarm_time_up_snooze(void)
{
    ESP_LOGI(TAG, "Time up snooze");
    const char *current_page = ui_bridge_get_current_page();
    if (current_page == NULL || strcmp(current_page, PAGE_TIME_UP) != 0) {
        ESP_LOGI(TAG, "Not on time up page (current=%s)",
                 current_page ? current_page : "NULL");
        return false;
    } else {
        alarm_time_up_snooze_impl();
    }
    return true;
}
