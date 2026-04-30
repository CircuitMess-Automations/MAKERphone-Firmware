#ifndef MAKERPHONE_PALETTE_H
#define MAKERPHONE_PALETTE_H

#include <misc/lv_color.h>

/*
 * Shared MAKERphone 2.0 palette.
 *
 * These six colours are the visual signature of the retro feature-phone
 * skin and are referenced from every Phone* widget plus ChatterTheme.cpp.
 * Keep them identical wherever they appear so that mixed grids/screens
 * read as one coherent UI.
 *
 *   MP_BG_DARK     deep purple   (page background, status-bar fill)
 *   MP_ACCENT      sunset orange (focus, primary fills, "Sent" bubble)
 *   MP_HIGHLIGHT   cyan          (icon strokes, "edited" highlights)
 *   MP_DIM         muted purple  (cards, idle borders, "Received" fill)
 *   MP_TEXT        warm cream    (default text colour)
 *   MP_LABEL_DIM   dim lavender  (timestamps, placeholders)
 *
 * RGB values are kept in lv_color_make(R, G, B) form rather than baked
 * into the LV_COLOR_DEPTH-specific lv_color_hex(...) so this header stays
 * portable across LVGL builds.
 */

#define MP_BG_DARK    lv_color_make( 20,  12,  36)
#define MP_ACCENT     lv_color_make(255, 140,  30)
#define MP_HIGHLIGHT  lv_color_make(122, 232, 255)
#define MP_DIM        lv_color_make( 70,  56, 100)
#define MP_TEXT       lv_color_make(255, 220, 180)
#define MP_LABEL_DIM  lv_color_make(170, 140, 200)

#endif // MAKERPHONE_PALETTE_H
