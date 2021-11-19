// This saves almost 10KB when the rest is compiled with -O3!
#pragma GCC optimize ("-O2")

#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <lupng.h>

#include "bitmaps/image_hourglass.h"
#include "fonts/fonts.h"
#include "rg_system.h"
#include "rg_gui.h"

static uint16_t *overlay_buffer = NULL;

static const rg_gui_theme_t default_theme = {
    .box_background = C_NAVY,
    .box_header = C_WHITE,
    .box_border = C_DIM_GRAY,
    .item_standard = C_WHITE,
    .item_disabled = C_GRAY, // C_DIM_GRAY
    .scrollbar = C_RED,
};

static rg_gui_theme_t theme;
static rg_gui_font_t font_info = {0, 8, 8, 8, &font_basic8x8};

static int screen_width = -1;
static int screen_height = -1;

// static const char *SETTING_FONTSIZE     = "FontSize";
static const char *SETTING_FONTTYPE     = "FontType";


void rg_gui_init(void)
{
    screen_width = rg_display_get_status()->screen.width;
    screen_height = rg_display_get_status()->screen.height;
    RG_ASSERT(screen_width && screen_height, "Bad screen res");

    overlay_buffer = (uint16_t *)rg_alloc(RG_MAX(screen_width, screen_height) * 32 * 2, MEM_SLOW);
    rg_gui_set_font_type(rg_settings_get_int32(SETTING_FONTTYPE, 0));
    rg_gui_set_theme(&default_theme);
}

bool rg_gui_set_theme(const rg_gui_theme_t *new_theme)
{
    if (!new_theme)
        return false;

    theme = *new_theme;
    return true;
}

static rg_glyph_t get_glyph(const rg_font_t *font, int points, int c)
{
    rg_glyph_t out = {
        .width  = font->width ? font->width : 8,
        .height = font->height,
        .bitmap = {0},
    };

    if (c == '\n') // Some glyphs are always zero width
    {
        out.width = 0;
    }
    else if (font->type == 0) // bitmap
    {
        if (c < font->chars)
        {
            for (int y = 0; y < font->height; y++) {
                out.bitmap[y] = font->data[(c * font->height) + y];
            }
        }
    }
    else // Proportional
    {
        // Based on code by Boris Lovosevic (https://github.com/loboris)
        int charCode, adjYOffset, width, height, xOffset, xDelta;
        const uint8_t *data = font->data;
        do {
            charCode = *data++;
            adjYOffset = *data++;
            width = *data++;
            height = *data++;
            xOffset = *data++;
            xOffset = xOffset < 0x80 ? xOffset : -(0xFF - xOffset);
            xDelta = *data++;

            if (c != charCode && charCode != 0xFF && width != 0) {
                data += (((width * height) - 1) / 8) + 1;
            }
        } while ((c != charCode) && (charCode != 0xFF));

        if (c == charCode)
        {
            out.width = ((width > xDelta) ? width : xDelta);
            // out.height = height;

            int ch = 0, mask = 0x80;

            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    if (((x + (y * width)) % 8) == 0) {
                        mask = 0x80;
                        ch = *data++;
                    }
                    if ((ch & mask) != 0) {
                        out.bitmap[adjYOffset + y] |= (1 << (xOffset + x));
                    }
                    mask >>= 1;
                }
            }
        }
    }

    if (points && points != font->height)
    {
        float scale = (float)points / font->height;
        rg_glyph_t out2 = out;
        out.height = points;
        for (int y = 0; y < out.height; y++)
            out.bitmap[y] = out2.bitmap[(int)(y / scale)];
    }

    return out;
}

bool rg_gui_set_font_type(int type)
{
    if (type < 0)
        type += fonts_count;

    if (type < 0 || type > fonts_count - 1)
        return false;

    font_info.type = type;
    font_info.font = fonts[font_info.type];
    font_info.points = (font_info.type < 3) ? (8 + font_info.type * 4) : font_info.font->height;
    font_info.width  = RG_MAX(font_info.font->width, 4);
    font_info.height = font_info.points;

    rg_settings_set_int32(SETTING_FONTTYPE, font_info.type);

    RG_LOGI("Font set to: points=%d, size=%dx%d, scaling=%.2f\n",
        font_info.points, font_info.width, font_info.height,
        (float)font_info.points / font_info.font->height);

    return true;
}

rg_gui_font_t rg_gui_get_font_info(void)
{
    return font_info;
}

rg_rect_t rg_gui_calc_text_size(const char *text, int wrap_width)
{
    if (wrap_width > 0)
        return rg_gui_draw_text(-wrap_width, 0, 0, text, 0, 0, RG_TEXT_MULTILINE|RG_TEXT_DUMMY_DRAW);
    else
        return rg_gui_draw_text(0, 0, 0, text, 0, 0, RG_TEXT_DUMMY_DRAW);
}

rg_rect_t rg_gui_draw_text(int x_pos, int y_pos, int width, const char *text,
                           rg_color_t color_fg, rg_color_t color_bg, uint32_t flags)
{
    if (x_pos < 0) x_pos += screen_width;
    if (y_pos < 0) y_pos += screen_height;
    if (!text || *text == 0) text = " ";

    // FIX ME: We should cache get_glyph().width to avoid calling it up to 3 times.

    if (width == 0)
    {
        // Find the longest line to determine our box width
        int line_width = 0;
        const char *ptr = text;
        while (*ptr)
        {
            int chr = *ptr++;
            line_width += get_glyph(font_info.font, font_info.points, chr).width;

            if (chr == '\n' || *ptr == 0)
            {
                width = RG_MAX(line_width, width);
                line_width = 0;
            }
        }
    }

    int draw_width = RG_MIN(width, screen_width - x_pos);
    int font_height = font_info.height;
    int y_offset = 0;
    const char *ptr = text;

    while (*ptr)
    {
        int x_offset = 0;

        size_t p = draw_width * font_height;
        while (p--)
            overlay_buffer[p] = color_bg;

        if (flags & (RG_TEXT_ALIGN_LEFT|RG_TEXT_ALIGN_CENTER))
        {
            // Find the current line's text width
            const char *line = ptr;
            while (x_offset < draw_width && *line && *line != '\n')
            {
                int width = get_glyph(font_info.font, font_info.points, *line++).width;
                if (draw_width - x_offset < width) // Do not truncate glyphs
                    break;
                x_offset += width;
            }
            if (flags & RG_TEXT_ALIGN_CENTER)
                x_offset = (draw_width - x_offset) / 2;
            else if (flags & RG_TEXT_ALIGN_LEFT)
                x_offset = draw_width - x_offset;
        }

        while (x_offset < draw_width)
        {
            rg_glyph_t glyph = get_glyph(font_info.font, font_info.points, *ptr++);

            if (draw_width - x_offset < glyph.width) // Do not truncate glyphs
            {
                if (flags & RG_TEXT_MULTILINE)
                    ptr--;
                break;
            }

            if (!(flags & RG_TEXT_DUMMY_DRAW))
            {
                for (int y = 0; y < font_height; y++)
                {
                    uint16_t *output = &overlay_buffer[x_offset + (draw_width * y)];

                    for (int x = 0; x < glyph.width; x++)
                    {
                        output[x] = (glyph.bitmap[y] & (1 << x)) ? color_fg : color_bg;
                    }
                }
            }

            x_offset += glyph.width;

            if (*ptr == 0 || *ptr == '\n')
                break;
        }

        if (!(flags & RG_TEXT_DUMMY_DRAW))
            rg_display_write(x_pos, y_pos + y_offset, draw_width, font_height, 0, overlay_buffer);

        y_offset += font_height;

        if (!(flags & RG_TEXT_MULTILINE))
            break;
    }

    return (rg_rect_t){draw_width, y_offset};
}

void rg_gui_draw_rect(int x_pos, int y_pos, int width, int height, int border_size,
                      rg_color_t border_color, rg_color_t fill_color)
{
    if (width <= 0 || height <= 0)
        return;

    if (x_pos < 0)
        x_pos += screen_width;
    if (y_pos < 0)
        y_pos += screen_height;

    if (border_size > 0)
    {
        size_t p = border_size * RG_MAX(width, height);
        while (p--)
            overlay_buffer[p] = border_color;

        rg_display_write(x_pos, y_pos, width, border_size, 0, overlay_buffer); // Top
        rg_display_write(x_pos, y_pos + height - border_size, width, border_size, 0, overlay_buffer); // Bottom
        rg_display_write(x_pos, y_pos, border_size, height, 0, overlay_buffer); // Left
        rg_display_write(x_pos + width - border_size, y_pos, border_size, height, 0, overlay_buffer); // Right

        x_pos += border_size;
        y_pos += border_size;
        width -= border_size * 2;
        height -= border_size * 2;
    }

    if (height > 0 && fill_color != -1)
    {
        size_t p = width * RG_MIN(height, 16);
        while (p--)
            overlay_buffer[p] = fill_color;

        for (int y = 0; y < height; y += 16)
            rg_display_write(x_pos, y_pos + y, width, RG_MIN(height - y, 16), 0, overlay_buffer);
    }
}

void rg_gui_draw_image(int x_pos, int y_pos, int width, int height, const rg_image_t *img)
{
    RG_ASSERT(img && img->data, "Bad image");

    if (width <= 0)
        width = img->width;
    if (height <= 0)
        height = img->height;

    rg_display_write(x_pos, y_pos, width, height, img->width * 2, img->data);
}

void rg_gui_draw_battery(int x_pos, int y_pos)
{
    int width = 20, height = 10;
    int width_fill = width;
    float percentage;
    rg_color_t color_fill = C_RED;
    rg_color_t color_border = C_SILVER;
    rg_color_t color_empty = C_BLACK;

    if (rg_input_read_battery(&percentage, NULL))
    {
        width_fill = width / 100.f * percentage;
        if (percentage < 20.f)
            color_fill = C_RED;
        else if (percentage < 40.f)
            color_fill = C_ORANGE;
        else
            color_fill = C_FOREST_GREEN;
    }

    if (x_pos < 0) x_pos += screen_width;
    if (y_pos < 0) y_pos += screen_height;

    rg_gui_draw_rect(x_pos, y_pos, width + 2, height, 1, color_border, -1);
    rg_gui_draw_rect(x_pos + width + 2, y_pos + 2, 2, height - 4, 1, color_border, -1);
    rg_gui_draw_rect(x_pos + 1, y_pos + 1, width_fill, height - 2, 0, 0, color_fill);
    rg_gui_draw_rect(x_pos + 1 + width_fill, y_pos + 1, width - width_fill, 8, 0, 0, color_empty);
}

void rg_gui_draw_hourglass(void)
{
    rg_display_write((screen_width / 2) - (image_hourglass.width / 2),
        (screen_height / 2) - (image_hourglass.height / 2),
        image_hourglass.width,
        image_hourglass.height,
        image_hourglass.width * 2,
        (uint16_t*)image_hourglass.pixel_data);
}

static int get_dialog_items_count(const dialog_option_t *options)
{
    if (options == NULL)
        return 0;

    for (int i = 0; i < 16; i++)
    {
        if (options[i].flags == RG_DIALOG_FLAG_LAST) {
            return i;
        }
    }
    return 0;
}

void rg_gui_draw_dialog(const char *header, const dialog_option_t *options, int sel)
{
    const int options_count = get_dialog_items_count(options);
    const int sep_width = rg_gui_calc_text_size(": ", 0).width;
    const int font_height = font_info.height;
    const int max_box_width = 0.82f * screen_width;
    const int max_box_height = 0.82f * screen_height;
    const int box_padding = 6;
    const int row_padding_y = 1;
    const int row_padding_x = 8;

    int box_width = box_padding * 2;
    int box_height = box_padding * 2 + (header ? font_height + 6 : 0);
    int inner_width = rg_gui_calc_text_size(header, 0).width;
    int max_inner_width = max_box_width - sep_width - (row_padding_x + box_padding) * 2;
    int col1_width = -1;
    int col2_width = -1;
    int row_height[options_count];

    for (int i = 0; i < options_count; i++)
    {
        rg_rect_t label = {0};
        rg_rect_t value = {0};

        if (options[i].label)
        {
            label = rg_gui_calc_text_size(options[i].label, max_inner_width);
            inner_width = RG_MAX(inner_width, label.width);
        }

        if (options[i].value)
        {
            value = rg_gui_calc_text_size(options[i].value, max_inner_width - label.width);
            col1_width = RG_MAX(col1_width, label.width);
            col2_width = RG_MAX(col2_width, value.width);
        }

        row_height[i] = RG_MAX(label.height, value.height) + row_padding_y * 2;
        box_height += row_height[i];
    }

    col1_width = RG_MIN(col1_width, max_box_width);
    col2_width = RG_MIN(col2_width, max_box_width);

    if (col2_width >= 0)
        inner_width = RG_MAX(inner_width, col1_width + col2_width + sep_width);

    inner_width = RG_MIN(inner_width, max_box_width);
    col2_width = inner_width - col1_width - sep_width;
    box_width += inner_width + row_padding_x * 2;
    box_height = RG_MIN(box_height, max_box_height);

    const int box_x = (screen_width - box_width) / 2;
    const int box_y = (screen_height - box_height) / 2;

    int x = box_x + box_padding;
    int y = box_y + box_padding;

    if (header)
    {
        int width = inner_width + row_padding_x * 2;
        rg_gui_draw_text(x, y, width, header, theme.box_header, theme.box_background, RG_TEXT_ALIGN_CENTER);
        rg_gui_draw_rect(x, y + font_height, width, 6, 0, 0, theme.box_background);
        y += font_height + 6;
    }

    int top_i = 0;

    if (sel >= 0 && sel < options_count)
    {
        int yy = y;

        for (int i = 0; i < options_count; i++)
        {
            yy += row_height[i];

            if (yy >= box_y + box_height)
            {
                if (sel < i)
                    break;
                yy = y;
                top_i = i;
            }
        }
    }

    int i = top_i;
    for (; i < options_count; i++)
    {
        uint16_t color = (options[i].flags == RG_DIALOG_FLAG_NORMAL) ? theme.item_standard : theme.item_disabled;
        uint16_t fg = (i == sel) ? theme.box_background : color;
        uint16_t bg = (i == sel) ? color : theme.box_background;
        int xx = x + row_padding_x;
        int yy = y + row_padding_y;
        int height = 8;

        if (y + row_height[i] >= box_y + box_height)
        {
            break;
        }

        if (options[i].value)
        {
            rg_gui_draw_text(xx, yy, col1_width, options[i].label, fg, bg, 0);
            rg_gui_draw_text(xx + col1_width, yy, sep_width, ": ", fg, bg, 0);
            height = rg_gui_draw_text(xx + col1_width + sep_width, yy, col2_width, options[i].value, fg, bg, RG_TEXT_MULTILINE).height;
            rg_gui_draw_rect(xx, yy + font_height, inner_width - col2_width, height - font_height, 0, 0, bg);
        }
        else
        {
            height = rg_gui_draw_text(xx, yy, inner_width, options[i].label, fg, bg, RG_TEXT_MULTILINE).height;
        }

        rg_gui_draw_rect(x, yy, row_padding_x, height, 0, 0, bg);
        rg_gui_draw_rect(xx + inner_width, yy, row_padding_x, height, 0, 0, bg);
        rg_gui_draw_rect(x, y, inner_width + row_padding_x * 2, row_padding_y, 0, 0, bg);
        rg_gui_draw_rect(x, yy + height, inner_width + row_padding_x * 2, row_padding_y, 0, 0, bg);

        y += height + row_padding_y * 2;
    }

    if (y < (box_y + box_height))
    {
        rg_gui_draw_rect(box_x, y, box_width, (box_y + box_height) - y, 0, 0, theme.box_background);
    }

    rg_gui_draw_rect(box_x, box_y, box_width, box_height, box_padding, theme.box_background, -1);
    rg_gui_draw_rect(box_x - 1, box_y - 1, box_width + 2, box_height + 2, 1, theme.box_border, -1);

    // Basic scroll indicators are overlayed at the end...
    if (top_i > 0)
    {
        int x = box_x + inner_width + box_padding;
        int y = box_y + box_padding - 1;
        rg_gui_draw_rect(x, y, 3, 3, 0, 0, theme.scrollbar);
        rg_gui_draw_rect(x + 6, y, 3, 3, 0, 0, theme.scrollbar);
        rg_gui_draw_rect(x + 12, y, 3, 3, 0, 0, theme.scrollbar);
    }

    if (i < options_count)
    {
        int x = box_x + inner_width + box_padding;
        int y = box_y + box_height - box_padding - 1;
        rg_gui_draw_rect(x, y, 3, 3, 0, 0, theme.scrollbar);
        rg_gui_draw_rect(x + 6, y, 3, 3, 0, 0, theme.scrollbar);
        rg_gui_draw_rect(x + 12, y, 3, 3, 0, 0, theme.scrollbar);
    }
}

int rg_gui_dialog(const char *header, const dialog_option_t *options_const, int selected)
{
    int options_count = get_dialog_items_count(options_const);
    int sel = selected < 0 ? (options_count + selected) : selected;
    int sel_old = sel;
    int last_key = -1;

    // We create a copy of options because the callbacks might modify it (ie option->value)
    dialog_option_t options[options_count + 1];

    for (int i = 0; i <= options_count; i++)
    {
        char value_buffer[128] = {0xFF, 0};

        options[i] = options_const[i];

        if (options[i].update_cb)
        {
            options[i].value = value_buffer;
            options[i].update_cb(&options[i], RG_DIALOG_INIT);
            if (value_buffer[0] == 0xFF) // Not updated, reset ptr
                options[i].value = options_const[i].value;
        }

        if (options[i].value)
        {
            char *new_value = malloc(strlen(options[i].value) + 16);
            strcpy(new_value, options[i].value);
            options[i].value = new_value;
        }
    }

    // Constrain initial cursor and skip FLAG_SKIP items
    sel = RG_MIN(RG_MAX(0, sel), options_count - 1);

    rg_input_wait_for_key(RG_KEY_ALL, false);
    rg_gui_draw_dialog(header, options, sel);

    while (1)
    {
        uint32_t joystick = rg_input_read_gamepad();

        if (last_key >= 0) {
            if (!(joystick & last_key)) {
                last_key = -1;
            }
        }
        else {
            dialog_return_t select = RG_DIALOG_IGNORE;

            if (joystick & RG_KEY_UP) {
                last_key = RG_KEY_UP;
                if (--sel < 0) sel = options_count - 1;
            }
            else if (joystick & RG_KEY_DOWN) {
                last_key = RG_KEY_DOWN;
                if (++sel > options_count - 1) sel = 0;
            }
            else if (joystick & RG_KEY_B) {
                last_key = RG_KEY_B;
                select = RG_DIALOG_CANCEL;
            }
            else if (joystick & RG_KEY_OPTION) {
                last_key = RG_KEY_OPTION;
                select = RG_DIALOG_CANCEL;
            }
            else if (joystick & RG_KEY_MENU) {
                last_key = RG_KEY_MENU;
                select = RG_DIALOG_CANCEL;
            }
            // if (options[sel].flags == RG_DIALOG_FLAG_NORMAL) {
            if (options[sel].flags != RG_DIALOG_FLAG_DISABLED && options[sel].flags != RG_DIALOG_FLAG_SKIP) {
                if (joystick & RG_KEY_LEFT) {
                    last_key = RG_KEY_LEFT;
                    if (options[sel].update_cb != NULL) {
                        select = options[sel].update_cb(&options[sel], RG_DIALOG_PREV);
                        sel_old = -1;
                    }
                }
                else if (joystick & RG_KEY_RIGHT) {
                    last_key = RG_KEY_RIGHT;
                    if (options[sel].update_cb != NULL) {
                        select = options[sel].update_cb(&options[sel], RG_DIALOG_NEXT);
                        sel_old = -1;
                    }
                }
                else if (joystick & RG_KEY_START) {
                    last_key = RG_KEY_START;
                    if (options[sel].update_cb != NULL) {
                        select = options[sel].update_cb(&options[sel], RG_DIALOG_ALT);
                        sel_old = -1;
                    }
                }
                else if (joystick & RG_KEY_A) {
                    last_key = RG_KEY_A;
                    if (options[sel].update_cb != NULL) {
                        select = options[sel].update_cb(&options[sel], RG_DIALOG_ENTER);
                        sel_old = -1;
                    } else {
                        select = RG_DIALOG_SELECT;
                    }
                }
            }

            if (select == RG_DIALOG_CANCEL) {
                sel = -1;
                break;
            }
            if (select == RG_DIALOG_SELECT) {
                break;
            }
        }
        if (sel_old != sel)
        {
            while (options[sel].flags == RG_DIALOG_FLAG_SKIP && sel_old != sel)
            {
                sel += (last_key == RG_KEY_DOWN) ? 1 : -1;

                if (sel < 0)
                    sel = options_count - 1;

                if (sel >= options_count)
                    sel = 0;
            }
            rg_gui_draw_dialog(header, options, sel);
            sel_old = sel;
        }

        usleep(20 * 1000UL);
    }

    rg_input_wait_for_key(last_key, false);

    rg_display_force_redraw();

    for (int i = 0; i < options_count; i++)
    {
        free(options[i].value);
    }

    return sel < 0 ? sel : options[sel].id;
}

bool rg_gui_confirm(const char *title, const char *message, bool yes_selected)
{
    const dialog_option_t options[] = {
        {0, (char *)message, NULL, -1, NULL},
        {0, "", NULL, -1, NULL},
        {1, "Yes", NULL, 1, NULL},
        {0, "No ", NULL, 1, NULL},
        RG_DIALOG_CHOICE_LAST
    };
    return rg_gui_dialog(title, message ? options : options + 1, yes_selected ? -2 : -1) == 1;
}

void rg_gui_alert(const char *title, const char *message)
{
    const dialog_option_t options[] = {
        {0, (char *)message, NULL, -1, NULL},
        {0, "", NULL, -1, NULL},
        {1, "OK", NULL, 1, NULL},
        RG_DIALOG_CHOICE_LAST
    };
    rg_gui_dialog(title, message ? options : options + 1, -1);
}

static dialog_return_t volume_update_cb(dialog_option_t *option, dialog_event_t event)
{
    int old_level = rg_audio_get_volume();
    int level = old_level;

    if (event == RG_DIALOG_PREV) level -= 1;
    if (event == RG_DIALOG_NEXT) level += 1;

    level = RG_MIN(RG_MAX(level, RG_AUDIO_VOL_MIN), RG_AUDIO_VOL_MAX);

    if (level != old_level)
        rg_audio_set_volume(level);

    sprintf(option->value, "%d%%", level * RG_AUDIO_VOL_MAX);

    return RG_DIALOG_IGNORE;
}

static dialog_return_t brightness_update_cb(dialog_option_t *option, dialog_event_t event)
{
    int old_level = rg_display_get_backlight();
    int level = old_level;

    if (event == RG_DIALOG_PREV) level -= 10;
    if (event == RG_DIALOG_NEXT) level += 10;

    level = RG_MIN(RG_MAX(level & ~1, 1), 100);

    if (level != old_level)
        rg_display_set_backlight(level);

    sprintf(option->value, "%d%%", level);

    return RG_DIALOG_IGNORE;
}

static dialog_return_t audio_update_cb(dialog_option_t *option, dialog_event_t event)
{
    int8_t sink = rg_audio_get_sink();

    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT) {
        sink = (sink == RG_AUDIO_SINK_SPEAKER) ? RG_AUDIO_SINK_EXT_DAC : RG_AUDIO_SINK_SPEAKER;
        rg_audio_set_sink(sink);
    }

    strcpy(option->value, (sink == RG_AUDIO_SINK_EXT_DAC) ? "Ext DAC" : "Speaker");

    return RG_DIALOG_IGNORE;
}

static dialog_return_t filter_update_cb(dialog_option_t *option, dialog_event_t event)
{
    int8_t max = RG_DISPLAY_FILTER_COUNT - 1;
    int8_t mode = rg_display_get_filter();
    int8_t prev = mode;

    if (event == RG_DIALOG_PREV && --mode < 0) mode = max; // 0;
    if (event == RG_DIALOG_NEXT && ++mode > max) mode = 0; // max;

    if (mode != prev)
    {
        rg_display_set_filter(mode);
    }

    if (mode == RG_DISPLAY_FILTER_OFF)   strcpy(option->value, "Off  ");
    if (mode == RG_DISPLAY_FILTER_HORIZ) strcpy(option->value, "Horiz");
    if (mode == RG_DISPLAY_FILTER_VERT)  strcpy(option->value, "Vert ");
    if (mode == RG_DISPLAY_FILTER_BOTH)  strcpy(option->value, "Both ");

    return RG_DIALOG_IGNORE;
}

static dialog_return_t scaling_update_cb(dialog_option_t *option, dialog_event_t event)
{
    int8_t max = RG_DISPLAY_SCALING_COUNT - 1;
    int8_t mode = rg_display_get_scaling();
    int8_t prev = mode;

    if (event == RG_DIALOG_PREV && --mode < 0) mode =  max; // 0;
    if (event == RG_DIALOG_NEXT && ++mode > max) mode = 0;  // max;

    if (mode != prev) {
        rg_display_set_scaling(mode);
    }

    if (mode == RG_DISPLAY_SCALING_OFF)  strcpy(option->value, "Off  ");
    if (mode == RG_DISPLAY_SCALING_FIT)  strcpy(option->value, "Fit ");
    if (mode == RG_DISPLAY_SCALING_FILL) strcpy(option->value, "Full ");

    return RG_DIALOG_IGNORE;
}

static dialog_return_t update_mode_update_cb(dialog_option_t *option, dialog_event_t event)
{
    int8_t max = RG_DISPLAY_UPDATE_COUNT - 1;
    int8_t mode = rg_display_get_update_mode();
    int8_t prev = mode;

    if (event == RG_DIALOG_PREV && --mode < 0) mode =  max; // 0;
    if (event == RG_DIALOG_NEXT && ++mode > max) mode = 0;  // max;

    if (mode != prev) {
        rg_display_set_update_mode(mode);
    }

    if (mode == RG_DISPLAY_UPDATE_PARTIAL)   strcpy(option->value, "Partial");
    if (mode == RG_DISPLAY_UPDATE_FULL)      strcpy(option->value, "Full   ");
    // if (mode == RG_DISPLAY_UPDATE_INTERLACE) strcpy(option->value, "Interlace");

    return RG_DIALOG_IGNORE;
}

static dialog_return_t speedup_update_cb(dialog_option_t *option, dialog_event_t event)
{
    rg_app_t *app = rg_system_get_app();
    if (event == RG_DIALOG_PREV && --app->speedupEnabled < 0) app->speedupEnabled = 2;
    if (event == RG_DIALOG_NEXT && ++app->speedupEnabled > 2) app->speedupEnabled = 0;

    sprintf(option->value, "%dx", app->speedupEnabled + 1);

    return RG_DIALOG_IGNORE;
}

static dialog_return_t more_settings_cb(dialog_option_t *option, dialog_event_t event)
{
    rg_app_t *app = rg_system_get_app();
    if (event == RG_DIALOG_ENTER && app->handlers.settings)
    {
        (app->handlers.settings)();
    }
    return RG_DIALOG_IGNORE;
}

int rg_gui_settings_menu(const dialog_option_t *extra_options)
{
    dialog_option_t options[16 + get_dialog_items_count(extra_options)];
    dialog_option_t *opt = &options[0];
    rg_app_t *app = rg_system_get_app();

    *opt++ = (dialog_option_t){0, "Brightness", "50%",  1, &brightness_update_cb};
    *opt++ = (dialog_option_t){0, "Volume    ", "50%",  1, &volume_update_cb};
    *opt++ = (dialog_option_t){0, "Audio out ", "Speaker", 1, &audio_update_cb};

    if (!app->isLauncher)
    {
        *opt++ = (dialog_option_t){0, "Scaling", "Full", 1, &scaling_update_cb};
        *opt++ = (dialog_option_t){0, "Filter", "None", 1, &filter_update_cb};
        *opt++ = (dialog_option_t){0, "Update", "Partial", 1, &update_mode_update_cb};
        *opt++ = (dialog_option_t){0, "Speed", "1x", 1, &speedup_update_cb};
    }

    while (extra_options && (*extra_options).flags != RG_DIALOG_FLAG_LAST)
    {
        *opt++ = *extra_options++;
    }

    if (app->handlers.settings)
    {
        *opt++ = (dialog_option_t){0, "More...", NULL, 1, &more_settings_cb};
    }

    *opt++ = (dialog_option_t)RG_DIALOG_CHOICE_LAST;

    int sel = rg_gui_dialog("Options", options, 0);

    rg_settings_save();

    return sel;
}

int rg_gui_about_menu(const dialog_option_t *extra_options)
{
    char build_ver[32], build_date[32], build_user[32];

    const dialog_option_t options[] = {
        {0, "Ver.", build_ver, 1, NULL},
        {0, "Date", build_date, 1, NULL},
        {0, "By", build_user, 1, NULL},
        RG_DIALOG_SEPARATOR,
        {1000, "Reboot to firmware", NULL, 1, NULL},
        {2000, "Reset settings", NULL, 1, NULL},
        {4000, "Debug", NULL, 1, NULL},
        {0000, "Close", NULL, 1, NULL},
        RG_DIALOG_CHOICE_LAST
    };

    const rg_app_t *app = rg_system_get_app();

    sprintf(build_ver, "%.30s", app->version);
    sprintf(build_date, "%s %.5s", app->buildDate, app->buildTime);
    sprintf(build_user, "%.30s", app->buildUser);

    char *rel_hash = strstr(build_ver, "-0-g");
    if (rel_hash)
    {
        rel_hash[0] = ' ';
        rel_hash[1] = ' ';
        rel_hash[2] = ' ';
        rel_hash[3] = '(';
        strcat(build_ver, ")");
    }

    int sel = rg_gui_dialog("Retro-Go", options, -1);

    switch (sel)
    {
        case 1000:
            rg_system_switch_app(RG_APP_FACTORY);
            break;
        case 2000:
            if (rg_gui_confirm("Reset all settings?", NULL, false)) {
                rg_settings_reset();
                rg_system_restart();
            }
            break;
        case 4000:
            rg_gui_debug_menu(NULL);
            break;
    }

    return sel;
}

int rg_gui_debug_menu(const dialog_option_t *extra_options)
{
    char screen_res[20], source_res[20], scaled_res[20];
    char stack_hwm[20], heap_free[20], block_free[20];
    char system_rtc[20], uptime[20];

    const dialog_option_t options[] = {
        {0, "Screen Res", screen_res, 1, NULL},
        {0, "Source Res", source_res, 1, NULL},
        {0, "Scaled Res", scaled_res, 1, NULL},
        {0, "Stack HWM ", stack_hwm, 1, NULL},
        {0, "Heap free ", heap_free, 1, NULL},
        {0, "Block free", block_free, 1, NULL},
        {0, "System RTC", system_rtc, 1, NULL},
        {0, "Uptime    ", uptime, 1, NULL},
        RG_DIALOG_SEPARATOR,
        {1000, "Save screenshot", NULL, 1, NULL},
        {2000, "Save trace", NULL, 1, NULL},
        {3000, "Cheats", NULL, 1, NULL},
        {4000, "Crash", NULL, 1, NULL},
        {5000, "Random time", NULL, 1, NULL},
        RG_DIALOG_CHOICE_LAST
    };

    const rg_stats_t stats = rg_system_get_stats();
    const rg_display_t *display = rg_display_get_status();
    time_t now = time(NULL);

    strftime(system_rtc, 20, "%F %T", gmtime(&now));
    sprintf(screen_res, "%dx%d", display->screen.width, display->screen.height);
    sprintf(source_res, "%dx%d", display->source.width, display->source.height);
    sprintf(scaled_res, "%dx%d", display->viewport.width, display->viewport.height);
    sprintf(stack_hwm, "%d", stats.freeStackMain);
    sprintf(heap_free, "%d+%d", stats.freeMemoryInt, stats.freeMemoryExt);
    sprintf(block_free, "%d+%d", stats.freeBlockInt, stats.freeBlockExt);
    sprintf(uptime, "%ds", (int)(get_elapsed_time() / 1000 / 1000));

    int sel = rg_gui_dialog("Debugging", options, 0);

    if (sel == 1000)
    {
        rg_emu_screenshot(RG_BASE_PATH "/screenshot.png", 0, 0);
    }
    else if (sel == 2000)
    {
        rg_system_save_trace(RG_BASE_PATH "/trace.txt", 0);
    }
    else if (sel == 4000)
    {
        RG_PANIC("Crash test!");
    }
    else if (sel == 5000)
    {
        struct timeval tv = {rand() % 1893474000, 0};
        settimeofday(&tv, NULL);
    }

    return sel;
}

static void draw_game_status_bars(void)
{
    int height = RG_MAX(font_info.height + 4, 16);
    int padding = (height - font_info.height) / 2;
    int max_len = RG_MIN(screen_width / font_info.width, 99);
    char header[100] = {0};
    char footer[100] = {0};

    const rg_stats_t stats = rg_system_get_stats();
    const rg_app_t *app = rg_system_get_app();

    snprintf(header, 100, "SPEED: %.0f%% (%.0f/%.0f) / BUSY: %.0f%%",
        round(stats.totalFPS / app->refreshRate * 100.f),
        round(stats.totalFPS - stats.skippedFPS),
        round(stats.totalFPS),
        round(stats.busyPercent));

    if (app->romPath && strlen(app->romPath) > max_len)
        snprintf(footer, 100, "...%s", app->romPath + (strlen(app->romPath) - (max_len - 3)));
    else if (app->romPath)
        snprintf(footer, 100, "%s", app->romPath);

    rg_input_wait_for_key(RG_KEY_ALL, false);

    rg_gui_draw_rect(0, 0, screen_width, height, 0, 0, C_BLACK);
    rg_gui_draw_rect(0, -height, screen_width, height, 0, 0, C_BLACK);
    rg_gui_draw_text(0, padding, screen_width, header, C_LIGHT_GRAY, C_BLACK, 0);
    rg_gui_draw_text(0, -height + padding, screen_width, footer, C_LIGHT_GRAY, C_BLACK, 0);
    rg_gui_draw_battery(-26, 3);
}

int rg_gui_game_settings_menu(void)
{
    rg_audio_set_mute(true);
    draw_game_status_bars();
    int sel = rg_gui_settings_menu(NULL);
    rg_audio_set_mute(false);
    return sel;
}

int rg_gui_game_menu(void)
{
    const dialog_option_t choices[] = {
        {1000, "Save & Continue", NULL,  1, NULL},
        {2000, "Save & Quit", NULL, 1, NULL},
        {3000, "Restart", NULL, 1, NULL},
        #ifdef ENABLE_NETPLAY
        {5000, "Netplay", NULL, 1, NULL},
        #endif
        #if !RG_GAMEPAD_OPTION_BTN
        {5500, "Options", NULL, 1, NULL},
        #endif
        {6000, "About", NULL, 1, NULL},
        {7000, "Quit", NULL, 1, NULL},
        RG_DIALOG_CHOICE_LAST
    };

    const dialog_option_t choices_restart[] = {
        {3001, "Reload save", NULL,  1, NULL},
        {3002, "Soft reset", NULL, 1, NULL},
        {3003, "Hard reset", NULL, 1, NULL},
        RG_DIALOG_CHOICE_LAST
    };

    rg_audio_set_mute(true);
    draw_game_status_bars();

    int sel = rg_gui_dialog("Retro-Go", choices, 0);

    if (sel == 3000)
    {
        sel = rg_gui_dialog("Restart", choices_restart, 0);
    }

    switch (sel)
    {
        case 1000: rg_emu_save_state(0); break;
        case 2000: if (rg_emu_save_state(0)) rg_system_switch_app(RG_APP_LAUNCHER); break;
        case 3001: rg_emu_load_state(0); break; // rg_system_restart();
        case 3002: rg_emu_reset(false); break;
        case 3003: rg_emu_reset(true); break;
    #ifdef ENABLE_NETPLAY
        case 5000: rg_netplay_quick_start(); break;
    #endif
    #if !RG_GAMEPAD_OPTION_BTN
        case 5500: rg_gui_game_settings_menu(); break;
    #endif
        case 6000: rg_gui_about_menu(NULL); break;
        case 7000: rg_system_switch_app(RG_APP_LAUNCHER); break;
    }

    rg_audio_set_mute(false);

    return sel;
}

rg_image_t *rg_image_load_from_file(const char *filename, uint32_t flags)
{
    RG_ASSERT(filename, "bad param");

    FILE *fp = fopen(filename, "rb");
    if (!fp)
    {
        RG_LOGE("Unable to open image file '%s'!\n", filename);
        return NULL;
    }

    fseek(fp, 0, SEEK_END);

    size_t data_len = ftell(fp);
    uint8_t *data = malloc(data_len);
    if (!data)
    {
        RG_LOGE("Memory allocation failed (%d bytes)!\n", data_len);
        fclose(fp);
        return NULL;
    }

    fseek(fp, 0, SEEK_SET);
    fread(data, data_len, 1, fp);
    fclose(fp);

    rg_image_t *img = rg_image_load_from_memory(data, data_len, flags);
    free(data);

    return img;
}

rg_image_t *rg_image_load_from_memory(const uint8_t *data, size_t data_len, uint32_t flags)
{
    RG_ASSERT(data && data_len >= 16, "bad param");

    if (memcmp(data, "\x89PNG", 4) == 0)
    {
        LuImage *png = luPngReadMem(data, data_len);
        if (!png)
        {
            RG_LOGE("PNG parsing failed!\n");
            return NULL;
        }

        rg_image_t *img = rg_image_alloc(png->width, png->height);
        if (img)
        {
            size_t pixel_count = img->width * img->height;
            size_t pixel_size = png->channels;
            const uint8_t *src = png->data;
            uint16_t *dest = img->data;

            // RGB888 or RGBA8888 to RGB565
            for (int i = 0; i < pixel_count; ++i)
            {
                dest[i] = (((src[0] >> 3) & 0x1F) << 11)
                        | (((src[1] >> 2) & 0x3F) << 5)
                        | (((src[2] >> 3) & 0x1F));
                src += pixel_size;
            }
        }
        luImageRelease(png, NULL);
        return img;
    }
    else // RAW565 (uint16 width, uint16 height, uint16 data[])
    {
        int img_width = ((uint16_t *)data)[0];
        int img_height = ((uint16_t *)data)[1];
        int size_diff = (img_width * img_height * 2 + 4) - data_len;

        if (size_diff >= 0 && size_diff <= 100)
        {
            rg_image_t *img = rg_image_alloc(img_width, img_height);
            if (img)
            {
                // Image is already RGB565 little endian, just copy it
                memcpy(img->data, data + 4, data_len - 4);
            }
            // else Maybe we could just return (rg_image_t *)buffer; ?
            return img;
        }
    }

    RG_LOGE("Image format not recognized!\n");
    return NULL;
}

bool rg_image_save_to_file(const char *filename, const rg_image_t *img, uint32_t flags)
{
    RG_ASSERT(filename && img, "bad param");

    LuImage *png = luImageCreate(img->width, img->height, 3, 8, 0, 0);
    if (!png)
    {
        RG_LOGE("LuImage allocation failed!\n");
        return false;
    }

    size_t pixel_count = img->width * img->height;
    const uint16_t *src = img->data;
    uint8_t *dest = png->data;

    // RGB565 to RGB888
    for (int i = 0; i < pixel_count; ++i)
    {
        dest[0] = ((src[i] >> 11) & 0x1F) << 3;
        dest[1] = ((src[i] >> 5) & 0x3F) << 2;
        dest[2] = ((src[i] & 0x1F) << 3);
        dest += 3;
    }

    if (luPngWriteFile(filename, png) != PNG_OK)
    {
        RG_LOGE("luPngWriteFile failed!\n");
        luImageRelease(png, 0);
        return false;
    }

    luImageRelease(png, 0);
    return true;
}

rg_image_t *rg_image_copy_resized(const rg_image_t *img, int new_width, int new_height)
{
    RG_ASSERT(img, "bad param");

    if (new_width <= 0 && new_height <= 0)
    {
        new_width = img->width;
        new_height = img->height;
    }
    else if (new_width <= 0)
    {
        new_width = img->width * ((float)new_height / img->height);
    }
    else if (new_height <= 0)
    {
        new_height = img->height * ((float)new_width / img->width);
    }

    rg_image_t *new_img = rg_image_alloc(new_width, new_height);
    if (new_img)
    {
        float step_x = (float)img->width / new_width;
        float step_y = (float)img->height / new_height;
        uint16_t *dst = new_img->data;
        for (int y = 0; y < new_height; y++)
        {
            for (int x = 0; x < new_width; x++)
            {
                *(dst++) = img->data[((int)(y * step_y) * img->width) + (int)(x * step_x)];
            }
        }
    }
    return new_img;
}

rg_image_t *rg_image_alloc(size_t width, size_t height)
{
    rg_image_t *img = malloc(sizeof(rg_image_t) + width * height * 2);
    if (!img)
    {
        RG_LOGE("Image alloc failed (%dx%d)\n", width, height);
        return NULL;
    }
    img->width = width;
    img->height = height;
    return img;
}

void rg_image_free(rg_image_t *img)
{
    free(img);
}
