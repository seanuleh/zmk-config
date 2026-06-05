/*
 * Sofle portrait custom status screen.
 *
 * Phase 1: rotation + per-half stub label. Confirms the screen is wired,
 * orientation matches the physical (vertically mounted) display, and the
 * left/right shields each pick up their respective branch.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* nice!view native dimensions */
#define DISP_NATIVE_W 160
#define DISP_NATIVE_H 68

/* After 90deg rotation, the canvas is portrait. */
#define CANVAS_W DISP_NATIVE_H  /* 68  */
#define CANVAS_H DISP_NATIVE_W  /* 160 */

static void apply_portrait_rotation(void) {
    /* Rotate the LVGL display 90 degrees so we author content in portrait
     * coordinates (CANVAS_W x CANVAS_H = 68 x 160). */
    lv_disp_t *disp = lv_disp_get_default();
    if (disp != NULL) {
        lv_disp_set_rotation(disp, LV_DISP_ROTATION_90);
    }
}

static lv_obj_t *build_stub_screen(const char *half_label) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);

    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, half_label);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

    /* Tiny corner marker so we know which corner is "top" after rotation. */
    lv_obj_t *marker = lv_obj_create(screen);
    lv_obj_set_size(marker, 4, 4);
    lv_obj_set_style_bg_color(marker, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_width(marker, 0, LV_PART_MAIN);
    lv_obj_align(marker, LV_ALIGN_TOP_LEFT, 2, 2);

    return screen;
}

lv_obj_t *zmk_display_status_screen(void) {
    apply_portrait_rotation();

#if IS_ENABLED(CONFIG_SHIELD_SOFLED_LEFT)
    return build_stub_screen("LEFT");
#elif IS_ENABLED(CONFIG_SHIELD_SOFLED_RIGHT)
    return build_stub_screen("RIGHT");
#else
    return build_stub_screen("?");
#endif
}
