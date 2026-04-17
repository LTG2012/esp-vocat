/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "lvgl.h"
#include "lv_eaf.h"
#include "esp_log.h"
#include <stdio.h>
#include <math.h>
#include <string.h>  /* For strcmp */
#include "application.h"
#include "assets/lang_config.h"
#include "alarm_api.h"
#include "alarm_manager.h"
#include "ui_helpers.h"

#define GONGDE_THRESHOLD        50
#define LOTTIE_SIZE_HOR_MIN             (90)
#define LOTTIE_SIZE_VER_MIN             (90)

static const char *TAG = "muyu";

typedef struct {
    lv_obj_t *container;
    lv_obj_t *muyu_img;
    lv_obj_t *gongde_txt;
    lv_obj_t *gongde_sum;
    int gongde_sum_value;
} muyuplay_ui_t;

typedef struct {
    lv_obj_t *container;
    lv_obj_t *fish_canvas;
} fish_ui_t;

static muyuplay_ui_t s_muyuplay_ui;
static bool screen_change_next = false;

static void gongde_anim_finish_cb(lv_anim_t * a)
{
    lv_obj_set_style_text_opa(s_muyuplay_ui.gongde_txt, LV_OPA_TRANSP, 0);
    //lv_obj_add_flag(s_muyuplay_ui.muyu_img, LV_OBJ_FLAG_CLICKABLE);
}

static lv_anim_t * Anime1_Animation(lv_obj_t * TargetObject, int delay)
{
    lv_anim_t * out_anim;
    ui_anim_user_data_t * PropertyAnimation_0_user_data = (ui_anim_user_data_t *)lv_malloc(sizeof(ui_anim_user_data_t));
    PropertyAnimation_0_user_data->target = TargetObject;
    PropertyAnimation_0_user_data->val = -1;
    lv_anim_t PropertyAnimation_0;
    lv_anim_init(&PropertyAnimation_0);
    lv_anim_set_time(&PropertyAnimation_0, 400);
    lv_anim_set_user_data(&PropertyAnimation_0, PropertyAnimation_0_user_data);
    lv_anim_set_custom_exec_cb(&PropertyAnimation_0, _ui_anim_callback_set_y);
    lv_anim_set_values(&PropertyAnimation_0, 0, -100);
    lv_anim_set_path_cb(&PropertyAnimation_0, lv_anim_path_linear);
    lv_anim_set_delay(&PropertyAnimation_0, delay + 0);
    lv_anim_set_deleted_cb(&PropertyAnimation_0, _ui_anim_callback_free_user_data);
    lv_anim_set_playback_time(&PropertyAnimation_0, 0);
    lv_anim_set_playback_delay(&PropertyAnimation_0, 0);
    lv_anim_set_repeat_count(&PropertyAnimation_0, 0);
    lv_anim_set_repeat_delay(&PropertyAnimation_0, 0);
    lv_anim_set_early_apply(&PropertyAnimation_0, false);
    lv_anim_set_get_value_cb(&PropertyAnimation_0, &_ui_anim_callback_get_y);
    out_anim = lv_anim_start(&PropertyAnimation_0);
    ui_anim_user_data_t * PropertyAnimation_1_user_data = (ui_anim_user_data_t *)lv_malloc(sizeof(ui_anim_user_data_t));
    PropertyAnimation_1_user_data->target = TargetObject;
    PropertyAnimation_1_user_data->val = -1;
    lv_anim_t PropertyAnimation_1;
    lv_anim_init(&PropertyAnimation_1);
    lv_anim_set_time(&PropertyAnimation_1, 400);
    lv_anim_set_user_data(&PropertyAnimation_1, PropertyAnimation_1_user_data);
    lv_anim_set_custom_exec_cb(&PropertyAnimation_1, _ui_anim_callback_set_opacity);
    lv_anim_set_values(&PropertyAnimation_1, 255, 0);
    lv_anim_set_path_cb(&PropertyAnimation_1, lv_anim_path_linear);
    lv_anim_set_delay(&PropertyAnimation_1, delay + 0);
    lv_anim_set_ready_cb(&PropertyAnimation_1, gongde_anim_finish_cb);
    lv_anim_set_deleted_cb(&PropertyAnimation_1, _ui_anim_callback_free_user_data);
    lv_anim_set_playback_time(&PropertyAnimation_1, 0);
    lv_anim_set_playback_delay(&PropertyAnimation_1, 0);
    lv_anim_set_repeat_count(&PropertyAnimation_1, 0);
    lv_anim_set_repeat_delay(&PropertyAnimation_1, 0);
    lv_anim_set_early_apply(&PropertyAnimation_1, false);
    lv_anim_set_get_value_cb(&PropertyAnimation_1, &_ui_anim_callback_get_opacity);
    out_anim = lv_anim_start(&PropertyAnimation_1);

    return out_anim;
}

static void muyushow_Animation(lv_obj_t *TargetObject, int delay)
{
    ui_anim_user_data_t *PropertyAnimation_0_user_data = (ui_anim_user_data_t *)lv_malloc(sizeof(ui_anim_user_data_t));;
    PropertyAnimation_0_user_data->target = TargetObject;
    lv_anim_t PropertyAnimation_0;
    lv_anim_init(&PropertyAnimation_0);
    lv_anim_set_time(&PropertyAnimation_0, 300);
    lv_anim_set_user_data(&PropertyAnimation_0, PropertyAnimation_0_user_data);
    lv_anim_set_custom_exec_cb(&PropertyAnimation_0, _ui_anim_callback_set_image_zoom);
    lv_anim_set_values(&PropertyAnimation_0, 120, 150);
    lv_anim_set_path_cb(&PropertyAnimation_0, lv_anim_path_overshoot);
    lv_anim_set_delay(&PropertyAnimation_0, delay);
    lv_anim_set_deleted_cb(&PropertyAnimation_0, _ui_anim_callback_free_user_data);
    lv_anim_start(&PropertyAnimation_0);

}

void muyu_click_event(){
    auto& app = Application::GetInstance();
    ESP_LOGI(TAG, "Muyu clicked - switching start/pause");
    lv_obj_set_style_text_opa(s_muyuplay_ui.gongde_txt, LV_OPA_MAX, 0);
    lv_obj_align(s_muyuplay_ui.gongde_txt, LV_ALIGN_CENTER, -54, -50); 
    app.PlaySound(Lang::Sounds::OGG_MUYU);
    Anime1_Animation(s_muyuplay_ui.gongde_txt, 0);
    s_muyuplay_ui.gongde_sum_value = (s_muyuplay_ui.gongde_sum_value < GONGDE_THRESHOLD) ?  s_muyuplay_ui.gongde_sum_value + 1 : GONGDE_THRESHOLD;
    if (s_muyuplay_ui.gongde_sum_value >= GONGDE_THRESHOLD && !screen_change_next) {
        muyushow_Animation(s_muyuplay_ui.muyu_img, 0);
        lv_label_set_text_fmt(s_muyuplay_ui.gongde_sum, "#f1c40f 功德圆满: %d#", s_muyuplay_ui.gongde_sum_value);
        screen_change_next = true;
    } else if (s_muyuplay_ui.gongde_sum_value >= GONGDE_THRESHOLD && screen_change_next) {
        //_ui_screen_change(&ui_fish, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_fish_screen_init);
        lv_label_set_text_fmt(s_muyuplay_ui.gongde_sum, "#f1c40f 功德圆满: %d#", s_muyuplay_ui.gongde_sum_value);
    } else {
        muyushow_Animation(s_muyuplay_ui.muyu_img, 0);
        lv_label_set_text_fmt(s_muyuplay_ui.gongde_sum, "#f1c40f 今日功德: %d#", s_muyuplay_ui.gongde_sum_value);
    }
}

void lvgl_muyu_click() 
{
    esp_lv_adapter_lock(-1);
    muyu_click_event(); 
    esp_lv_adapter_unlock();
}

static void muyu_img_event_handler(lv_event_t *e)
{   
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        muyu_click_event();
    }
}

lv_obj_t *alarm_muyu_create_with_parent(lv_obj_t *parent)
{
    // 初始化状态
    s_muyuplay_ui.gongde_sum_value = 0;

    s_muyuplay_ui.container = lv_obj_create(parent);
    lv_obj_set_size(s_muyuplay_ui.container, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(s_muyuplay_ui.container, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(s_muyuplay_ui.container, 0, 0);
    lv_obj_set_style_pad_all(s_muyuplay_ui.container, 0, 0);
    lv_obj_clear_flag(s_muyuplay_ui.container, LV_OBJ_FLAG_SCROLLABLE);

    /* ================= 木鱼图片 ================= */
    s_muyuplay_ui.muyu_img = lv_img_create(s_muyuplay_ui.container);
    lv_img_set_src(s_muyuplay_ui.muyu_img, &muyu_white);
    lv_obj_align(s_muyuplay_ui.muyu_img, LV_ALIGN_CENTER, 0, 40);
    lv_obj_add_flag(s_muyuplay_ui.muyu_img, LV_OBJ_FLAG_CLICKABLE); 
    lv_obj_add_flag(s_muyuplay_ui.muyu_img, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(s_muyuplay_ui.muyu_img, LV_OBJ_FLAG_SCROLLABLE);
    lv_img_set_zoom(s_muyuplay_ui.muyu_img, 150);

    /* ================= “功德+1” 文本 ================= */
    s_muyuplay_ui.gongde_txt = lv_label_create(s_muyuplay_ui.container);
    lv_label_set_text(s_muyuplay_ui.gongde_txt, "功德+1");
    lv_obj_align(s_muyuplay_ui.gongde_txt, LV_ALIGN_CENTER, -54, -50);

    lv_obj_set_style_text_color(s_muyuplay_ui.gongde_txt, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(s_muyuplay_ui.gongde_txt, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_font(s_muyuplay_ui.gongde_txt,
                               &ui_font_Heiti24, 0);
    lv_obj_set_style_blend_mode(s_muyuplay_ui.gongde_txt,
                                LV_BLEND_MODE_NORMAL, 0);

    /* ================= 今日功德 ================= */
    s_muyuplay_ui.gongde_sum = lv_label_create(s_muyuplay_ui.container);
    lv_label_set_recolor(s_muyuplay_ui.gongde_sum, true);
    lv_label_set_text_fmt(
        s_muyuplay_ui.gongde_sum,
        "#f1c40f 今日功德: %d#",
        s_muyuplay_ui.gongde_sum_value
    );
    lv_obj_set_style_text_font(s_muyuplay_ui.gongde_sum,
                               &ui_font_Heiti18, 0);
    lv_obj_align_to(s_muyuplay_ui.gongde_sum,
                    s_muyuplay_ui.muyu_img,
                    LV_ALIGN_TOP_RIGHT, 10, 15);

    lv_obj_add_event_cb(s_muyuplay_ui.muyu_img, muyu_img_event_handler, LV_EVENT_CLICKED, NULL);
    lv_obj_update_layout(s_muyuplay_ui.container);

    lv_obj_add_flag(s_muyuplay_ui.container, LV_OBJ_FLAG_HIDDEN);

    return s_muyuplay_ui.container;
}