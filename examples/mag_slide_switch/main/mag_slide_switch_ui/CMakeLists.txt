SET(SOURCES screens/ui_MagSlideSwitchScreen.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    ui_events.c
    images/ui_img_iphone_flat_png.c
    images/ui_img_echoear_1_png.c
    images/ui_img_bell_2_png.c
    images/ui_img_fish_png.c
    images/ui_img_pairing_png.c
    images/ui_img_donut_png.c
    images/ui_img_ice_cream_png.c
    images/ui_img_iphone_upright_png.c)

add_library(ui ${SOURCES})
