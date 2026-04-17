#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_err.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "lvgl.h"
#include "mag_slide_switch_ui/ui.h"
#include "vocat_base_control.h"

static const char *TAG = "mag_slide_switch";

static void handle_mag_switch_event(uint16_t event)
{
    switch (event) {
        case VOCAT_BASE_CMD_RECV_SWITCH_SLIDE_DOWN:
            ESP_LOGI(TAG, "Slider moved down");
            app_ui_event_mag_slide_down(NULL);
            break;
        case VOCAT_BASE_CMD_RECV_SWITCH_SLIDE_UP:
            ESP_LOGI(TAG, "Slider moved up");
            app_ui_event_mag_slide_up(NULL);
            break;
        case VOCAT_BASE_CMD_RECV_SWITCH_REMOVE_FROM_UP:
            ESP_LOGI(TAG, "Slider removed from UP position");
            app_ui_event_mag_remove_from_up(NULL);
            break;
        case VOCAT_BASE_CMD_RECV_SWITCH_REMOVE_FROM_DOWN:
            ESP_LOGI(TAG, "Slider removed from DOWN position");
            app_ui_event_mag_remove_from_down(NULL);
            break;
        case VOCAT_BASE_CMD_RECV_SWITCH_PLACE_FROM_UP:
            ESP_LOGI(TAG, "Slider placed from UP position");
            app_ui_event_mag_place_from_up(NULL);
            break;
        case VOCAT_BASE_CMD_RECV_SWITCH_PLACE_FROM_DOWN:
            ESP_LOGI(TAG, "Slider placed from DOWN position");
            app_ui_event_mag_place_from_down(NULL);
            break;
        case VOCAT_BASE_CMD_RECV_SWITCH_SINGLE_CLICK:
            ESP_LOGI(TAG, "Single click detected");
            app_ui_event_mag_single_click(NULL);
            break;
        case VOCAT_BASE_CMD_RECV_SWITCH_FISH_ATTACHED:
            ESP_LOGI(TAG, "Fish attached detected");
            app_ui_event_mag_fish_attached(NULL);
            break;
        case VOCAT_BASE_CMD_RECV_SWITCH_FISH_DETACH:
            ESP_LOGI(TAG, "Fish detached detected");
            app_ui_event_mag_fish_detached(NULL);
            break;
        case VOCAT_BASE_CMD_RECV_SWITCH_PAIR_DETECT:
            ESP_LOGI(TAG, "Pairing detected");
            app_ui_event_mag_pairing(NULL);
            break;
        case VOCAT_BASE_CMD_RECV_SWITCH_PAIR_CANCEL:
            ESP_LOGI(TAG, "Pairing cancelled detected");
            app_ui_event_mag_pairing_cancelled(NULL);
            break;
        case VOCAT_BASE_CMD_RECV_SWITCH_ICE_CREAM_ATTACHED:
            ESP_LOGI(TAG, "Ice cream attached");
            app_ui_event_mag_ice_cream_attached(NULL);
            break;
        case VOCAT_BASE_CMD_RECV_SWITCH_ICE_CREAM_DETACHED:
            ESP_LOGI(TAG, "Ice cream detached");
            app_ui_event_mag_ice_cream_detached(NULL);
            break;
        case VOCAT_BASE_CMD_RECV_SWITCH_DONUT_ATTACHED:
            ESP_LOGI(TAG, "Donut attached");
            app_ui_event_mag_donut_attached(NULL);
            break;
        case VOCAT_BASE_CMD_RECV_SWITCH_DONUT_DETACHED:
            ESP_LOGI(TAG, "Donut detached");
            app_ui_event_mag_donut_detached(NULL);
            break;
        case VOCAT_BASE_CMD_RECV_SWITCH_IPHONE_LEAN_FRONT:
            ESP_LOGI(TAG, "iPhone leaned from front (Z increase >= threshold)");
            app_ui_event_mag_iphone_lean_front(NULL);
            break;
        case VOCAT_BASE_CMD_RECV_SWITCH_IPHONE_LEAN_FRONT_DETACHED:
            ESP_LOGI(TAG, "iPhone detached from front position");
            app_ui_event_mag_iphone_lean_front_detached(NULL);
            break;
        case VOCAT_BASE_CMD_RECV_SWITCH_IPHONE_UNDER_BASE:
            ESP_LOGI(TAG, "iPhone under base (Z < 0)");
            app_ui_event_mag_iphone_under_base(NULL);
            break;
        case VOCAT_BASE_CMD_RECV_SWITCH_IPHONE_UNDER_BASE_DETACHED:
            ESP_LOGI(TAG, "iPhone detached from under base");
            app_ui_event_mag_iphone_under_base_detached(NULL);
            break;
        case VOCAT_BASE_CMD_RECV_CALIBRATE_START:
            ESP_LOGI(TAG, "Base calibration started");
            break;
        case VOCAT_BASE_CMD_RECV_CALIBRATE_STEP1:
            ESP_LOGI(TAG, "Base second position calibration done");
            break;
        case VOCAT_BASE_CMD_RECV_CALIBRATE_STEP2:
            ESP_LOGI(TAG, "Base calibration fully complete");
            break;
        default:
            ESP_LOGD(TAG, "Unhandled magnetic switch event: %u", event);
            break;
    }
}

static void vocat_base_cmd_callback(uint8_t cmd, uint8_t *data, int data_len, void *user_ctx)
{
    (void)user_ctx;

    if (cmd == VOCAT_BASE_CMD_RECV_SLIDE_SWITCH) {
        uint16_t mag_event = 0;

        if (data == NULL || data_len < 2) {
            ESP_LOGW(TAG, "Invalid slide switch payload: len=%d", data_len);
            return;
        }

        mag_event = ((uint16_t)data[0] << 8) | data[1];
        ESP_LOGI(TAG, "Received slide switch event: 0x%04X", mag_event);

        bsp_display_lock(0);
        handle_mag_switch_event(mag_event);
        bsp_display_unlock();
        return;
    }

    if (cmd == VOCAT_BASE_CMD_RECV_ACTION) {
        ESP_LOGI(TAG, "Received action notification");
        return;
    }

    if (cmd == VOCAT_BASE_CMD_RECV_HEARTBEAT) {
        ESP_LOGD(TAG, "Base heartbeat received");
        return;
    }

    if (cmd == VOCAT_BASE_CMD_RECV_PERCEPTION) {
        ESP_LOGI(TAG, "Received perception payload, len=%d", data_len);
        return;
    }

    ESP_LOGD(TAG, "Received unsupported command: 0x%02X, len=%d", cmd, data_len);
}

static esp_err_t control_serial_init(void)
{
    vocat_base_control_config_t config = {
        .uart_num = UART_NUM_1,
        .tx_pin = GPIO_NUM_5,
        .rx_pin = GPIO_NUM_4,
        .baud_rate = 115200,
        .rx_buffer_size = 1024,
        .cmd_cb = vocat_base_cmd_callback,
        .user_ctx = NULL,
    };

    esp_err_t ret = vocat_base_control_init(&config);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Base control initialized");
    }
    return ret;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Display initialized");
    bsp_display_start();
    bsp_display_backlight_on();

    // Lock LVGL to initialize UI
    bsp_display_lock(0);

    // Initialize UI generated by SquareLine Studio
    ui_init();
    ESP_LOGI(TAG, "UI initialized successfully");

    lv_obj_t *objs[] = {ui_BellImage, ui_EventLabel, ui_FishImage, ui_PairingImage, ui_accessoryImage};
    for (int i = 0; i < sizeof(objs)/sizeof(objs[0]); i++) {
        if (objs[i] != NULL) {
            lv_obj_set_style_opa(objs[i], LV_OPA_TRANSP, 0);
        }
    }

    bsp_display_unlock();

    ESP_ERROR_CHECK(control_serial_init());
    
    ESP_LOGI(TAG, "Magnetic slide switch UI is running");
    ESP_LOGI(TAG, "Waiting for magnetic switch events...");
}
