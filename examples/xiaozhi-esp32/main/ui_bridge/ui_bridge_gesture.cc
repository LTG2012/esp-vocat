#include "ui_bridge.h"
#include "ui_bridge_priv.h"
#include "config.h"
#include <esp_log.h>
#include <lvgl.h>
#include <math.h>

#define TAG "ui_bridge"

/* Define center coordinates based on display dimensions */
#define UI_BRIDGE_CENTER_X                       (DISPLAY_WIDTH / 2)   /* Center X coordinate */
#define UI_BRIDGE_CENTER_Y                       (DISPLAY_HEIGHT / 2)  /* Center Y coordinate */

/* Internal gesture detection state structure */
typedef struct {
    bool active;
    bool handled;
    lv_coord_t start_x;
    lv_coord_t start_y;
    uint32_t press_start_time;
    /* Path tracking */
    lv_coord_t last_x;
    lv_coord_t last_y;
    lv_coord_t total_dx;
    lv_coord_t total_dy;
    float      total_path;    /* Accumulated path length for detecting S-shaped/backtracking swipes */
    bool       dir_set;
    float      dir_len;
    lv_coord_t dir_x;
    lv_coord_t dir_y;
    bool       linear_invalid;
    lv_timer_t *update_timer;  /* Timer for real-time coordinate updates */
} ui_bridge_gesture_state_t;

/* Gesture detection state */
static ui_bridge_gesture_state_t s_gesture_state = {
    .active = false,
    .handled = false,
    .start_x = 0,
    .start_y = 0,
    .press_start_time = 0,
    .last_x = 0,
    .last_y = 0,
    .total_dx = 0,
    .total_dy = 0,
    .total_path = 0.0f,
    .dir_set = false,
    .dir_len = 0.0f,
    .dir_x = 0,
    .dir_y = 0,
    .linear_invalid = false,
    .update_timer = nullptr,
};

#define GESTURE_UPDATE_TIMER_PERIOD_MS  10  /* Update coordinate every 10ms for real-time tracking */

/* Forward declarations */
static const char* ui_bridge_gesture_to_string(ui_bridge_gesture_type_t gesture);
static void ui_bridge_gesture_update_timer_cb(lv_timer_t *timer);

static const char* ui_bridge_gesture_to_string(ui_bridge_gesture_type_t gesture)
{
    switch (gesture) {
    case UI_BRIDGE_GESTURE_SWIPE_UP:
        return "UP";
    case UI_BRIDGE_GESTURE_SWIPE_DOWN:
        return "DOWN";
    case UI_BRIDGE_GESTURE_SWIPE_LEFT:
        return "LEFT";
    case UI_BRIDGE_GESTURE_SWIPE_RIGHT:
        return "RIGHT";
    case UI_BRIDGE_GESTURE_SHORT_PRESS:
        return "SHORT_PRESS";
    case UI_BRIDGE_GESTURE_LONG_PRESS:
        return "LONG_PRESS";
    case UI_BRIDGE_GESTURE_NONE:
    default:
        return "NONE";
    }
}

/**
 * @brief Timer callback for real-time coordinate updates during gesture tracking
 */
static void ui_bridge_gesture_update_timer_cb(lv_timer_t *timer)
{
    ui_bridge_gesture_state_t *state = (ui_bridge_gesture_state_t *)lv_timer_get_user_data(timer);
    if (!state || !state->active || state->handled) {
        ESP_LOGW(TAG, "Timer callback: state is not active or handled");
        return;
    }

    // lv_indev_t *indev = lv_indev_get_act();
    lv_indev_t * indev = lv_indev_get_next(NULL);
    if (!indev) {
        ESP_LOGW(TAG, "Timer callback: indev is NULL");
        return;
    }

    lv_point_t p;
    lv_indev_get_point(indev, &p);

    /* Update cumulative displacement */
    state->total_dx = p.x - state->start_x;
    state->total_dy = p.y - state->start_y;

    /* Accumulate path length (for filtering S-shaped/backtracking swipes) */
    lv_coord_t step_dx = p.x - state->last_x;
    lv_coord_t step_dy = p.y - state->last_y;
    state->total_path += sqrtf((float)step_dx * step_dx + (float)step_dy * step_dy);

    /* Determine direction vector initially (earlier detection with lower threshold) */
    if (!state->dir_set) {
        if ((LV_ABS(state->total_dx) >= UI_BRIDGE_GESTURE_SWIPE_THRESHOLD / 3) ||
                (LV_ABS(state->total_dy) >= UI_BRIDGE_GESTURE_SWIPE_THRESHOLD / 3)) {
            state->dir_x = state->total_dx;
            state->dir_y = state->total_dy;
            state->dir_len = sqrtf((float)state->dir_x * state->dir_x + (float)state->dir_y * state->dir_y);
            state->dir_set = (state->dir_len > 0.0f);
        }
    }

    /* If direction is set, calculate perpendicular distance from current point to start-direction line, mark as non-linear if exceeds limit (stricter) */
    if (state->dir_set && state->dir_len > 0.0f) {
        float cross = (float)(p.x - state->start_x) * state->dir_y - (float)(p.y - state->start_y) * state->dir_x;
        float dist = fabsf(cross) / state->dir_len;
        /* Use actual swipe distance * 0.2 for threshold, minimum 5 pixels */
        // const float ORTH_LIMIT_RUNTIME = fmaxf(state->dir_len * 0.2f, 5.0f);
        const float ORTH_LIMIT_RUNTIME = state->dir_len * 0.2f;
        if (dist > ORTH_LIMIT_RUNTIME) {
            state->linear_invalid = true;
        }
    }

    state->last_x = p.x;
    state->last_y = p.y;
}

/* Touch gesture event callback */
static void ui_bridge_gesture_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();
    ui_bridge_gesture_state_t *state = &s_gesture_state;
    switch (code) {
    case LV_EVENT_PRESSED:
        if (indev) {
            lv_point_t p;
            lv_indev_get_point(indev, &p);
            state->active = true;
            state->handled = false;
            state->start_x = p.x;
            state->start_y = p.y;
            state->press_start_time = lv_tick_get();
            state->last_x = p.x;
            state->last_y = p.y;
            state->total_dx = 0;
            state->total_dy = 0;
            state->total_path = 0.0f;
            state->dir_set = false;
            state->dir_len = 0.0f;
            state->dir_x = 0;
            state->dir_y = 0;
            state->linear_invalid = false;

            /* Create and start timer for real-time coordinate updates */
            if (state->update_timer == nullptr) {
                state->update_timer = lv_timer_create(ui_bridge_gesture_update_timer_cb, GESTURE_UPDATE_TIMER_PERIOD_MS, state);
                lv_timer_set_repeat_count(state->update_timer, -1);  /* Repeat indefinitely */
            } else {
                lv_timer_resume(state->update_timer);
            }

            ESP_LOGD(TAG, "press at (%ld, %ld)", (long)p.x, (long)p.y);
        } else {
            ESP_LOGW(TAG, "press but indev is NULL");
        }
        break;
    case LV_EVENT_GESTURE:
    case LV_EVENT_PRESSING:
    case LV_EVENT_LONG_PRESSED_REPEAT:
        /* Coordinate updates are now handled by timer callback, no action needed here */
        break;

    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST: {
        /* Stop and pause timer for coordinate updates */
        if (state->update_timer != nullptr) {
            lv_timer_pause(state->update_timer);
        }

        if (!state->active || !indev) {
            state->active = false;
            state->handled = false;
            break;
        }

        lv_point_t p;
        lv_indev_get_point(indev, &p);

        /* Update final step in total_path calculation */
        lv_coord_t final_step_dx = p.x - state->last_x;
        lv_coord_t final_step_dy = p.y - state->last_y;
        state->total_path += sqrtf((float)final_step_dx * final_step_dx + (float)final_step_dy * final_step_dy);

        lv_coord_t dx = p.x - state->start_x;
        lv_coord_t dy = p.y - state->start_y;
        uint32_t press_duration = lv_tick_elaps(state->press_start_time);
        float straight_len = sqrtf((float)dx * dx + (float)dy * dy);

        ESP_LOGD(TAG, "release: dx=%ld, dy=%ld, duration=%lu ms, handled=%d",
                 (long)dx, (long)dy, press_duration, state->handled);

        /* Check for swipe gesture on release (if not already handled) */
        if (!state->handled) {
            bool magnitude_enough = (LV_ABS(dx) >= UI_BRIDGE_GESTURE_SWIPE_THRESHOLD) ||
                                    (LV_ABS(dy) >= UI_BRIDGE_GESTURE_SWIPE_THRESHOLD);

            /* Linear check: backtracking (path length cannot be much greater than straight line length, stricter) */
            /* Ensure total_path is at least straight_len (in case timer didn't capture all steps) */
            if (state->total_path < straight_len) {
                state->total_path = straight_len;
            }
            float path_limit = fmaxf(straight_len * 1.05f, (float)UI_BRIDGE_GESTURE_SWIPE_THRESHOLD);
            bool linear_enough = !state->linear_invalid &&
                                 (state->total_path <= path_limit);

            if (linear_enough && magnitude_enough) {
                ui_bridge_gesture_type_t gesture = UI_BRIDGE_GESTURE_NONE;

                /* Use dominant axis to decide final direction, allowing diagonal motion */
                if (LV_ABS(dx) >= LV_ABS(dy)) {
                    gesture = (dx < 0) ? UI_BRIDGE_GESTURE_SWIPE_LEFT : UI_BRIDGE_GESTURE_SWIPE_RIGHT;
                } else {
                    gesture = (dy < 0) ? UI_BRIDGE_GESTURE_SWIPE_UP : UI_BRIDGE_GESTURE_SWIPE_DOWN;
                }

                ESP_LOGE(TAG, "Detected: %s", ui_bridge_gesture_to_string(gesture));
                ui_bridge_handle_gesture_navigation(gesture);
                state->handled = true;
            } else if (!linear_enough && magnitude_enough) {
                ESP_LOGW(TAG, "Rejected: path not linear (path=%.1f, limit=%.1f, invalid=%d)",
                         state->total_path, path_limit, state->linear_invalid);
            } else {
                /* It's a press (not a swipe) */
                ui_bridge_gesture_type_t gesture;
                if (press_duration >= UI_BRIDGE_GESTURE_LONG_PRESS_TIME_MS) {
                    gesture = UI_BRIDGE_GESTURE_LONG_PRESS;
                } else {
                    gesture = UI_BRIDGE_GESTURE_SHORT_PRESS;
                }
                ESP_LOGD(TAG, "press detected: %d (duration: %lu ms, dx: %ld, dy: %ld)", gesture, press_duration, LV_ABS(dx), LV_ABS(dy));
            }
        }

        /* Update last position for next gesture */
        state->last_x = p.x;
        state->last_y = p.y;
        state->active = false;
        state->handled = false;
        break;
    }

    default:
        /* Ignore all other events (LV_EVENT_FLUSH_WAIT_START, LV_EVENT_VSYNC, etc.) */
        break;
    }
}

void ui_bridge_attach_gesture_handler(lv_indev_t *indev)
{
    ESP_LOGI(TAG, "Attaching gesture handler to input device: %p", (void*)indev);
    lv_indev_add_event_cb(indev, ui_bridge_gesture_event_cb, LV_EVENT_ALL, NULL);
}
