idf_component_register(
    SRCS "main.c"
         "ui_pixel.c"
         "ui_pixel_math.c"
         "demo_party.c"
         "demo_shengbei.c"
         "demo_breakout.c"
         "demo_snake.c"
         "demo_memory.c"
         "app_boot.c"
         "app_bgm.c"
         "app_settings.c"
         "font_zh14.c"
         "font_zh20.c"
         "font_game.c"
         "party_award_b.c"
         "fap_screenshot.c"
    INCLUDE_DIRS "."
    REQUIRES bsp spiffs esp_timer
)

# 固件版本宏(取自 CMake project VERSION,供设备页显示)
target_compile_definitions(${COMPONENT_LIB} PUBLIC FW_VERSION="${PROJECT_VERSION}")
