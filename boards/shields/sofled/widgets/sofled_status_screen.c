/*
 * Sofle custom status screen for nice!view halves (160x68 landscape).
 *
 * Note: native display orientation is landscape. The halves are physically
 * mounted vertically, so content is authored sideways here on purpose —
 * the user reads it by tilting their head, or we'll address orientation
 * via devicetree rotation in a follow-up (LVGL software rotation needs a
 * larger render buffer than what's allocated for nice!view).
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if IS_ENABLED(CONFIG_SHIELD_SOFLED_LEFT)
#include "dude_widget.h"
static struct dude_widget left_widget;
#endif

#if IS_ENABLED(CONFIG_SHIELD_SOFLED_RIGHT)
#include "wpm_graph_widget.h"
static struct wpm_graph_widget right_widget;
#endif

lv_obj_t *zmk_display_status_screen(void) {
    /* Halves are physically mounted vertically. LVGL software rotation
     * doesn't take on the sharp ls0xx driver, so we pre-rotate sprite
     * data instead and lay widgets along the long axis (X = top-bottom
     * from the user's perspective). */
    lv_obj_t *screen = lv_obj_create(NULL);
    /* The mono theme applies an opaque background to the screen object
     * which LVGL repaints on every refresh. On a Sharp Memory LCD that
     * shows up as a continuous flicker. Strip ALL styles from the screen
     * so it stays naturally reflective (no draw) wherever no widget is
     * placed. Widgets get their own styles re-applied as needed. */
    lv_obj_remove_style_all(screen);

#if IS_ENABLED(CONFIG_SHIELD_SOFLED_LEFT)
    dude_widget_init(&left_widget, screen);
#elif IS_ENABLED(CONFIG_SHIELD_SOFLED_RIGHT)
    wpm_graph_widget_init(&right_widget, screen);
#endif

    return screen;
}
