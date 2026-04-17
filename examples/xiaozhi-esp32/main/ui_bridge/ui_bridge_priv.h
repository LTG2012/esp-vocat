#ifndef UI_BRIDGE_PRIV_H
#define UI_BRIDGE_PRIV_H

#include "ui_bridge.h"

#ifdef __cplusplus
/* Forward declaration */
namespace emote {
class EmoteDisplay;
}
extern "C" {
#endif

/**
 * @file ui_bridge_priv.h
 * @brief Internal/private functions and constants for ui_bridge module
 *
 * This header contains internal function declarations and constants that are used
 * between different implementation files of the ui_bridge module.
 * These should NOT be used by external code.
 */

/**
 * @brief Handle gesture-based page navigation (internal)
 *
 * This function is called by the gesture detection module when a valid
 * navigation gesture is detected.
 *
 * @param gesture_type The detected gesture type
 */
void ui_bridge_handle_gesture_navigation(ui_bridge_gesture_type_t gesture_type);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
/**
 * @brief Set cached emote display pointer (internal, C++ only)
 *
 * This function is called by ui_bridge_init to cache the emote display
 * pointer for later use.
 *
 * @param display Pointer to the EmoteDisplay instance
 */
void ui_bridge_set_emote_display(emote::EmoteDisplay *display);
#endif

#endif // UI_BRIDGE_PRIV_H
