#include "attitude_monitor.h"

#include <algorithm>
#include <cmath>

#include <esp_lv_adapter.h>

#include "board.h"
#include "esp_vocat.h"

namespace {

constexpr int kScreenSize = 360;
constexpr int kBallSize = 36;
constexpr int kCenter = kScreenSize / 2;
constexpr int kTravel = 105;
constexpr uint32_t kRefreshMs = 50;

struct AttitudeUi {
    lv_obj_t *container = nullptr;
    lv_obj_t *status = nullptr;
    lv_obj_t *ball = nullptr;
    lv_timer_t *timer = nullptr;
    float x = 0.0f;
    float y = 0.0f;
    bool active = false;
};

AttitudeUi s_ui;

Bmi270Imu *get_imu()
{
    auto *board = dynamic_cast<EspS3Cat *>(&Board::GetInstance());
    return board ? board->GetImu() : nullptr;
}

void update_timer(lv_timer_t *)
{
    if (!s_ui.active) {
        return;
    }

    Bmi270Imu *imu = get_imu();
    const AttitudeSnapshot snapshot = imu ? imu->GetSnapshot() : AttitudeSnapshot{};
    if (snapshot.state != ImuState::kRunning || snapshot.sample_age_ms >= 500) {
        lv_label_set_text(s_ui.status, "BMI270 ERROR");
        lv_obj_set_style_text_color(s_ui.status, lv_color_hex(0xF25F5C), 0);
        return;
    }

    lv_label_set_text(s_ui.status, "BMI270");
    lv_obj_set_style_text_color(s_ui.status, lv_color_hex(0x4CE0B3), 0);

    const float gravity_x = -snapshot.accel_g[0];
    const float gravity_y = -snapshot.accel_g[1];
    const float target_x = gravity_x * kTravel;
    const float target_y = gravity_y * kTravel;
    s_ui.x += (target_x - s_ui.x) * 0.45f;
    s_ui.y += (target_y - s_ui.y) * 0.45f;

    const float distance = std::sqrt(s_ui.x * s_ui.x + s_ui.y * s_ui.y);
    if (distance > kTravel) {
        s_ui.x = s_ui.x / distance * kTravel;
        s_ui.y = s_ui.y / distance * kTravel;
    }

    lv_obj_set_pos(s_ui.ball,
                   kCenter - kBallSize / 2 + static_cast<int>(s_ui.x),
                   kCenter - kBallSize / 2 + static_cast<int>(s_ui.y));
}

}  // namespace

lv_obj_t *attitude_monitor_create_with_parent(lv_obj_t *parent)
{
    s_ui.container = lv_obj_create(parent);
    lv_obj_set_size(s_ui.container, kScreenSize, kScreenSize);
    lv_obj_set_style_bg_color(s_ui.container, lv_color_hex(0x08131F), 0);
    lv_obj_set_style_border_width(s_ui.container, 0, 0);
    lv_obj_set_style_pad_all(s_ui.container, 0, 0);
    lv_obj_clear_flag(s_ui.container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_ui.container);
    lv_label_set_text(title, "ATTITUDE BALL");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    s_ui.status = lv_label_create(s_ui.container);
    lv_label_set_text(s_ui.status, "BMI270");
    lv_obj_set_style_text_color(s_ui.status, lv_color_hex(0x4CE0B3), 0);
    lv_obj_set_style_text_font(s_ui.status, &lv_font_montserrat_12, 0);
    lv_obj_align(s_ui.status, LV_ALIGN_BOTTOM_MID, 0, -28);

    lv_obj_t *center_mark = lv_obj_create(s_ui.container);
    lv_obj_set_size(center_mark, 8, 8);
    lv_obj_set_pos(center_mark, kCenter - 4, kCenter - 4);
    lv_obj_set_style_radius(center_mark, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(center_mark, lv_color_hex(0x536878), 0);
    lv_obj_set_style_border_width(center_mark, 0, 0);

    s_ui.ball = lv_obj_create(s_ui.container);
    lv_obj_set_size(s_ui.ball, kBallSize, kBallSize);
    lv_obj_set_pos(s_ui.ball, kCenter - kBallSize / 2, kCenter - kBallSize / 2);
    lv_obj_set_style_radius(s_ui.ball, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_ui.ball, lv_color_hex(0x18D9ED), 0);
    lv_obj_set_style_border_color(s_ui.ball, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_ui.ball, 2, 0);

    s_ui.timer = lv_timer_create(update_timer, kRefreshMs, nullptr);
    lv_timer_pause(s_ui.timer);
    lv_obj_add_flag(s_ui.container, LV_OBJ_FLAG_HIDDEN);
    return s_ui.container;
}

void attitude_monitor_show(void)
{
    esp_lv_adapter_lock(-1);
    s_ui.active = true;
    s_ui.x = 0.0f;
    s_ui.y = kTravel;
    lv_obj_set_pos(s_ui.ball,
                   kCenter - kBallSize / 2,
                   kCenter - kBallSize / 2 + kTravel);
    if (s_ui.timer) {
        lv_timer_resume(s_ui.timer);
    }
    esp_lv_adapter_unlock();
    if (Bmi270Imu *imu = get_imu()) {
        imu->SetHighRate(true);
    }
}

void attitude_monitor_hide(void)
{
    esp_lv_adapter_lock(-1);
    s_ui.active = false;
    if (s_ui.timer) {
        lv_timer_pause(s_ui.timer);
    }
    esp_lv_adapter_unlock();
    if (Bmi270Imu *imu = get_imu()) {
        imu->SetHighRate(false);
    }
}
