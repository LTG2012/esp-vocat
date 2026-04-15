#include "emote_display.h"

// Standard C++ headers
#include <cstring>
#include <memory>
#include <unordered_map>
#include <tuple>

// Standard C headers
#include <sys/time.h>
#include <time.h>

// ESP-IDF headers
#include <esp_log.h>
#include <esp_lcd_panel_io.h>
#include <esp_timer.h>
#include <esp_lv_adapter.h>
#include <lvgl.h>
#include <esp_psram.h>

// FreeRTOS headers
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Project headers
#include "assets.h"
#include "assets/lang_config.h"
#include "board.h"
#include "gfx.h"
#include "vocat_base_control.h"
#include "expression_emote.h"
#if CONFIG_BOARD_TYPE_ESP_VOCAT
#include "ui_bridge.h"
#endif

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);

namespace emote {

// ============================================================================
// Constants and Type Definitions
// ============================================================================

static const char* TAG = "EmoteDisplay";

// ============================================================================
// Forward Declarations
// ============================================================================

class EmoteDisplay;

// ============================================================================
// Helper Functions
// ============================================================================

// Flush callback for emote
static void OnFlushCallback(int x_start, int y_start, int x_end, int y_end, const void* data, emote_handle_t manager)
{
    (void)manager;

    lv_display_t *disp = lv_display_get_default();
    if (disp != nullptr) {
        bool state = esp_lv_adapter_get_dummy_draw_enabled(disp);
        if (state) {
            esp_lv_adapter_dummy_draw_blit(
                disp, x_start, y_start, x_end, y_end, data, true);
        }
    }
    // ESP_LOGI(TAG, "OnFlushCallback: x_start: %d, y_start: %d, x_end: %d, y_end: %d", x_start, y_start, x_end, y_end);
    emote_notify_flush_finished(manager);
}

// ============================================================================
// Graphics Initialization Functions
// ============================================================================

static emote_handle_t InitializeEmote(const esp_lcd_panel_handle_t panel, const int width, const int height)
{
    if (!panel) {
        ESP_LOGE(TAG, "InitializeEmote: Invalid parameters");
        return nullptr;
    }

    emote_config_t emote_cfg = {
        .flags = {
            .swap = true,
            .double_buffer = true,
            .buff_dma = false,
        },
        .gfx_emote = {
            .h_res = width,
            .v_res = height,
            .fps = 30,
        },
        .buffers = {
            .buf_pixels = static_cast<size_t>(width * 16),
        },
        .task = {
            .task_priority = 5,
            .task_stack = 6 * 1024,
            .task_affinity = 0,
            .task_stack_in_ext = true,
        },
        .flush_cb = OnFlushCallback,
    };

    emote_handle_t emote_handle = emote_init(&emote_cfg);
    if (!emote_handle) {
        ESP_LOGE(TAG, "Failed to initialize emote");
        return nullptr;
    }

    return emote_handle;
}

// ============================================================================
// EmoteDisplay Class Implementation
// ============================================================================

EmoteDisplay::EmoteDisplay(const esp_lcd_panel_handle_t panel, const esp_lcd_panel_io_handle_t panel_io,
    const int width, const int height)
{
    emote_handle_ = InitializeEmote(panel, width, height);
}

EmoteDisplay::~EmoteDisplay()
{
    if (emote_handle_) {
        emote_deinit(emote_handle_);
        emote_handle_ = nullptr;
    }
}

void EmoteDisplay::SetEmotion(const char* const emotion)
{
    ESP_LOGI(TAG, "SetEmotion: %s", emotion);

    if (emote_handle_ && emotion && strlen(emotion) > 0) {
        emote_set_anim_emoji(emote_handle_, emotion);
    }

    static const std::unordered_map<std::string, int> emotion_to_action_map = {
        {"happy",       VOCAT_BASE_CMD_SET_ACTION_SHARK_HEAD},
        {"laughing",    VOCAT_BASE_CMD_SET_ACTION_SHARK_HEAD},
        {"funny",       VOCAT_BASE_CMD_SET_ACTION_SHARK_HEAD},
        {"loving",      VOCAT_BASE_CMD_SET_ACTION_SHARK_HEAD},
        {"confident",   VOCAT_BASE_CMD_SET_ACTION_SHARK_HEAD},
        {"delicious",   VOCAT_BASE_CMD_SET_ACTION_SHARK_HEAD},
        {"thinking",    VOCAT_BASE_CMD_SET_ACTION_SHARK_HEAD},

        {"embarrassed", VOCAT_BASE_CMD_SET_ACTION_CAT_NUZZLE},

        {"sad",         VOCAT_BASE_CMD_SET_ACTION_SHARK_HEAD_DECAY},
        {"crying",      VOCAT_BASE_CMD_SET_ACTION_SHARK_HEAD_DECAY},
        {"sleepy",      VOCAT_BASE_CMD_SET_ACTION_SHARK_HEAD_DECAY},

        {"silly",       VOCAT_BASE_CMD_SET_ACTION_LOOK_AROUND},
        {"confused",    VOCAT_BASE_CMD_SET_ACTION_LOOK_AROUND},

        {"angry",       VOCAT_BASE_CMD_SET_ACTION_BEAT_SWING},

        {"surprised",   VOCAT_BASE_CMD_SET_ACTION_LOOK_AROUND},
        {"shocked",     VOCAT_BASE_CMD_SET_ACTION_LOOK_AROUND},

        {"winking",     VOCAT_BASE_CMD_SET_ACTION_CAT_NUZZLE},

        {"relaxed",     VOCAT_BASE_CMD_SET_ACTION_LOOK_AROUND},
    };

    if (emotion) {
        auto it = emotion_to_action_map.find(emotion);
        if (it != emotion_to_action_map.end()) {
            vocat_base_control_set_action(it->second);
        }
    }
}


void EmoteDisplay::SetChatMessage(const char* const role, const char* const content)
{
    ESP_LOGI(TAG, "SetChatMessage: %s, %s", role, content);
    if (emote_handle_ && content && strlen(content) > 0) {
        if ((std::strcmp(role, "system") == 0) && std::strstr(content, "xiaozhi.me")) {
            size_t len = strlen(content);
            char* new_content = new char[len + 1];
            strcpy(new_content, content);
            std::replace(new_content, new_content + len, static_cast<char>(0x0A), static_cast<char>(0x20));
            emote_set_event_msg(emote_handle_, EMOTE_MGR_EVT_SYS, new_content);
            delete[] new_content;
        } else {
            emote_set_event_msg(emote_handle_, EMOTE_MGR_EVT_SPEAK, content);
        }
    }
}

void EmoteDisplay::SetStatus(const char* const status)
{
    ESP_LOGI(TAG, "SetStatus: %s", status);
    if (emote_handle_ && status && strlen(status) > 0) {
        if (std::strcmp(status, Lang::Strings::LISTENING) == 0) {
            emote_set_event_msg(emote_handle_, EMOTE_MGR_EVT_LISTEN, NULL);
        } else if (std::strcmp(status, Lang::Strings::STANDBY) == 0) {
            emote_set_event_msg(emote_handle_, EMOTE_MGR_EVT_IDLE, NULL);
        } else if (std::strcmp(status, Lang::Strings::SPEAKING) == 0) {
            emote_set_event_msg(emote_handle_, EMOTE_MGR_EVT_SPEAK, NULL);
        } else if (std::strcmp(status, Lang::Strings::ERROR) == 0) {
            emote_set_event_msg(emote_handle_, EMOTE_MGR_EVT_SET, NULL);
        }
    }
}

void EmoteDisplay::ShowNotification(const char* notification, int duration_ms)
{
    ESP_LOGI(TAG, "ShowNotification: %s", notification);
    if (emote_handle_ && notification && strlen(notification) > 0) {
        emote_set_event_msg(emote_handle_, EMOTE_MGR_EVT_SYS, notification);
    }
}

void EmoteDisplay::UpdateStatusBar(bool update_all)
{
    ESP_LOGD(TAG, "UpdateStatusBar: %s", update_all ? "true" : "false");
    if (!emote_handle_) {
        return;
    }
}

void EmoteDisplay::SetPowerSaveMode(bool on)
{
    ESP_LOGI(TAG, "SetPowerSaveMode: %s", on ? "ON" : "OFF");
    if (!emote_handle_) {
        return;
    }
}

void EmoteDisplay::SetPreviewImage(const void* image)
{
    if (image) {
        ESP_LOGI(TAG, "SetPreviewImage: Preview image not supported, using default icon");
    }
}

void EmoteDisplay::SetTheme(Theme* const theme)
{
    ESP_LOGI(TAG, "SetTheme: %p", theme);
}

bool EmoteDisplay::Lock(const int timeout_ms)
{
    (void)timeout_ms;
    return true;
}

void EmoteDisplay::Unlock()
{
}

bool EmoteDisplay::StopAnimDialog()
{
    ESP_LOGI(TAG, "StopAnimDialog");
    if (emote_handle_) {
        return emote_stop_anim_dialog(emote_handle_);
    }
    return false;
}

bool EmoteDisplay::InsertAnimDialog(const char* emoji_name, uint32_t duration_ms)
{
    ESP_LOGI(TAG, "InsertAnimDialog: %s, %d", emoji_name, duration_ms);
    if (emote_handle_ && emoji_name) {
        return emote_insert_anim_dialog(emote_handle_, emoji_name, duration_ms);
    }
    return false;
}

void EmoteDisplay::RefreshAll()
{
    if (emote_handle_) {
        emote_notify_all_refresh(emote_handle_);
        return;
    }
}

void EmoteDisplay::SetupBaseIconOverlay()
{
    if (!emote_handle_) {
        return;
    }

    if (image_base_obj_) {
        gfx_obj_set_visible(image_base_obj_, true);
        return;
    }

    icon_data_t* icon_data = nullptr;
    esp_err_t ret = emote_get_icon_data_by_name(emote_handle_, "base_off", &icon_data);
    if (ret != ESP_OK || !icon_data || !icon_data->data || icon_data->size <= sizeof(gfx_image_header_t)) {
        ESP_LOGW(TAG, "base_off icon not ready, ret=%d", ret);
        return;
    }

    image_base_obj_ = emote_create_obj_by_type(emote_handle_, EMOTE_OBJ_TYPE_IMAGE, "img_base");
    if (!image_base_obj_) {
        ESP_LOGE(TAG, "Failed to create img_base object");
        return;
    }

    memcpy(&image_base_dsc_.header, icon_data->data, sizeof(gfx_image_header_t));
    image_base_dsc_.data = (const uint8_t*)icon_data->data + sizeof(gfx_image_header_t);
    image_base_dsc_.data_size = icon_data->size - sizeof(gfx_image_header_t);

    if (gfx_img_set_src(image_base_obj_, &image_base_dsc_) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set img_base source");
        return;
    }

    gfx_obj_set_visible(image_base_obj_, false);
    gfx_obj_align(image_base_obj_, GFX_ALIGN_CENTER, 0, 0);
    ESP_LOGI(TAG, "img_base overlay created");
}

void EmoteDisplay::InitCustomUI(esp_lcd_panel_io_handle_t panel_io, 
                                   esp_lcd_panel_handle_t panel,
                                   int width, int height, 
                                   int offset_x, int offset_y, 
                                   bool mirror_x, bool mirror_y, bool swap_xy,
                                   EmoteDisplay *display)
{
    lv_init();

#if CONFIG_SPIRAM
    // LV image cache, currently only PNG is supported
    size_t psram_size_mb = esp_psram_get_size() / 1024 / 1024;
    if (psram_size_mb >= 8) {
        lv_image_cache_resize(2 * 1024 * 1024, true);
        ESP_LOGI(TAG, "Use 2MB of PSRAM for image cache");
    } else if (psram_size_mb >= 2) {
        lv_image_cache_resize(512 * 1024, true);
        ESP_LOGI(TAG, "Use 512KB of PSRAM for image cache");
    }
#endif

    ESP_LOGI(TAG, "Initializing LVGL adapter, width:%d, height:%d", width, height);
    esp_lv_adapter_config_t adapter_config = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter_config.task_priority = 6;
    adapter_config.task_core_id = 0;
    adapter_config.tick_period_ms = 5;
    adapter_config.task_min_delay_ms = 10;
    adapter_config.task_max_delay_ms = 100;
    adapter_config.stack_in_psram = false;
    ESP_ERROR_CHECK(esp_lv_adapter_init(&adapter_config));

    esp_lv_adapter_display_config_t display_config = ESP_LV_ADAPTER_DISPLAY_SPI_WITH_PSRAM_DEFAULT_CONFIG(
                                                         panel,
                                                         panel_io,
                                                         static_cast<uint16_t>(width),
                                                         static_cast<uint16_t>(height),
                                                         ESP_LV_ADAPTER_ROTATE_0);
    display_config.profile.use_psram = true;
    display_config.profile.require_double_buffer = true;

    lv_display_t *lv_display = esp_lv_adapter_register_display(&display_config);
    if (lv_display == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    if (offset_x != 0 || offset_y != 0) {
        lv_display_set_offset(lv_display, offset_x, offset_y);
    }

    ESP_LOGI(TAG, "Starting LVGL adapter");
    esp_lv_adapter_set_dummy_draw(lv_display, true);
    esp_lv_adapter_start();

#if CONFIG_BOARD_TYPE_ESP_VOCAT
    esp_lv_adapter_lock(-1);
    /* Pass the display pointer directly to avoid Board::GetInstance() call */
    ui_bridge_init(display);
    esp_lv_adapter_unlock();
#endif
}

} // namespace emote
