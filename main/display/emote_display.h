#pragma once

#include "display.h"
#include <memory>
#include <string>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include "expression_emote.h"

namespace emote {

class EmoteDisplay : public Display {
public:
    EmoteDisplay(esp_lcd_panel_handle_t panel, esp_lcd_panel_io_handle_t panel_io, int width, int height);
    virtual ~EmoteDisplay();

    virtual void SetEmotion(const char* emotion) override;
    virtual void SetStatus(const char* status) override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void SetTheme(Theme* theme) override;
    virtual void ShowNotification(const char* notification, int duration_ms = 3000) override;
    virtual void UpdateStatusBar(bool update_all = false) override;
    virtual void SetPowerSaveMode(bool on) override;
    virtual void SetPreviewImage(const void* image);

    // Anim dialog methods
    bool StopAnimDialog();
    bool InsertAnimDialog(const char* emoji_name, uint32_t duration_ms);

    void RefreshAll();
    void SetupBaseIconOverlay();

    // Get emote handle for internal use
    emote_handle_t GetEmoteHandle() const { return emote_handle_; }

    /**
     * @brief Initialize LVGL display system
     * 
     * This static method initializes the LVGL graphics library and adapter,
     * registers the display, and initializes the UI bridge.
     * 
     * @param panel_io LCD panel IO handle
     * @param panel LCD panel handle
     * @param width Display width in pixels
     * @param height Display height in pixels
     * @param offset_x Display X offset
     * @param offset_y Display Y offset
     * @param mirror_x Mirror display horizontally
     * @param mirror_y Mirror display vertically
     * @param swap_xy Swap X and Y axes
     * @param display Pointer to EmoteDisplay instance (for UI bridge initialization)
     */
    static void InitCustomUI(esp_lcd_panel_io_handle_t panel_io, 
                                esp_lcd_panel_handle_t panel,
                                int width, int height, 
                                int offset_x, int offset_y, 
                                bool mirror_x, bool mirror_y, bool swap_xy,
                                EmoteDisplay *display);

private:
    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;

    emote_handle_t emote_handle_ = nullptr;
    gfx_obj_t* image_base_obj_ = nullptr;
    gfx_image_dsc_t image_base_dsc_ = {};

};

} // namespace emote
