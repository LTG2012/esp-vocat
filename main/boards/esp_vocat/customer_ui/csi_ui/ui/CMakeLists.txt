SET(SOURCES screens/ui_ScreenW.c
    screens/ui_csi_behav_Screen.c
    ui.c
    components/ui_comp_hook.c
    # ui_helpers.c - esp-brookesia: removed to avoid conflicts, use core's ui_helpers instead
    ui_events.c
    images/ui_img_272184077.c)

add_library(ui ${SOURCES})
