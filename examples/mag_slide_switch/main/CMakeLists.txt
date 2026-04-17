# SquareLine Studio UI 文件配置
set(UI_DIR mag_slide_switch_ui)

# 收集所有 UI 相关的源文件
set(UI_SOURCES
    ${UI_DIR}/ui.c
    ${UI_DIR}/ui_helpers.c
    ${UI_DIR}/ui_events.c
    ${UI_DIR}/screens/ui_MagSlideSwitchScreen.c
    ${UI_DIR}/components/ui_comp_hook.c
    ${UI_DIR}/images/ui_img_echoear_1_png.c
    ${UI_DIR}/images/ui_img_bell_2_png.c
    ${UI_DIR}/images/ui_img_fish_png.c
    ${UI_DIR}/images/ui_img_pairing_png.c
    ${UI_DIR}/images/ui_img_ice_cream_png.c
    ${UI_DIR}/images/ui_img_donut_png.c
    ${UI_DIR}/images/ui_img_iphone_flat_png.c
    ${UI_DIR}/images/ui_img_iphone_upright_png.c
)

idf_component_register(
    SRCS main.c ${UI_SOURCES}
    INCLUDE_DIRS . ${UI_DIR} ${UI_DIR}/screens ${UI_DIR}/components ${UI_DIR}/images
)
