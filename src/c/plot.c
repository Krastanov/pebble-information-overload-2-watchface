#include "plot.h"

static int16_t plot_min_i16(int16_t a, int16_t b) { return a < b ? a : b; }
static int16_t plot_max_i16(int16_t a, int16_t b) { return a > b ? a : b; }
static uint16_t plot_min_u16(uint16_t a, uint16_t b) { return a < b ? a : b; }

static uint16_t plot_visible_count(const PlotLayout* layout, uint16_t length,
                                   uint16_t start_index) {
    if (start_index >= length) { return 0; }
    return plot_min_u16(length-start_index, layout->area.size.w);
}

static int16_t plot_x_for_index(const PlotLayout* layout, uint16_t index,
                                uint16_t count) {
    if (count <= 1 || layout->area.size.w <= 1) { return layout->area.origin.x; }
    return layout->area.origin.x +
        ((int32_t)index * (layout->area.size.w-1)) / (count-1);
}

static int16_t plot_y_for_value(const PlotLayout* layout, int16_t value) {
    int16_t clipped = plot_min_i16(layout->y_max,
                                   plot_max_i16(layout->y_min, value));
    int16_t bottom = layout->area.origin.y + layout->area.size.h - 1;
    if (layout->area.size.h <= 1 || layout->y_min == layout->y_max) {
        return bottom;
    }
    return bottom - ((int32_t)(clipped-layout->y_min) *
                     (layout->area.size.h-1)) /
                     (layout->y_max-layout->y_min);
}

static bool plot_read_u8(const uint8_t* values, uint16_t length,
                         uint16_t start_index, uint16_t index,
                         uint8_t missing_value, int16_t decode_offset,
                         bool hide_zero, int16_t* out_value) {
    if (start_index+index >= length) { return false; }
    uint8_t raw = values[start_index+index];
    int16_t decoded = (int16_t)raw + decode_offset;
    if (raw == missing_value || (hide_zero && decoded == 0)) { return false; }
    *out_value = decoded;
    return true;
}

static void plot_fill_column(GContext* ctx, const PlotLayout* layout,
                             uint16_t index, uint16_t count, int16_t value) {
    int16_t baseline = plot_y_for_value(layout, layout->y_min);
    int16_t y = plot_y_for_value(layout, value);
    int16_t top = plot_min_i16(y, baseline);
    int16_t height = (baseline > y ? baseline-y : y-baseline) + 1;
    graphics_fill_rect(ctx, GRect(plot_x_for_index(layout, index, count),
                                  top, 1, height), 0, GCornerNone);
}

PlotLayout plot_layout(GRect frame, int16_t left, int16_t top,
                       int16_t right, int16_t bottom,
                       int16_t y_min, int16_t y_max) {
    PlotLayout layout = {
        .frame = frame,
        .area = GRect(frame.origin.x+left, frame.origin.y+top,
                      plot_max_i16(1, frame.size.w-left-right),
                      plot_max_i16(1, frame.size.h-top-bottom))
    };
    plot_set_y_range(&layout, y_min, y_max);
    return layout;
}

void plot_set_y_range(PlotLayout* layout, int16_t y_min, int16_t y_max) {
    if (y_min > y_max) {
        int16_t tmp = y_min;
        y_min = y_max;
        y_max = tmp;
    }
    if (y_min == y_max) {
        y_min -= 1;
        y_max += 1;
    }
    layout->y_min = y_min;
    layout->y_max = y_max;
}

bool plot_set_y_range_from_u8(PlotLayout* layout, const uint8_t* values,
                              uint16_t length, uint16_t start_index,
                              uint8_t missing_value, int16_t decode_offset) {
    int16_t y_min = 32767;
    int16_t y_max = -32768;
    uint16_t count = plot_visible_count(layout, length, start_index);

    for (uint16_t i=0; i<count; i++) {
        int16_t value;
        if (!plot_read_u8(values, length, start_index, i, missing_value,
                          decode_offset, false, &value)) {
            continue;
        }
        y_min = plot_min_i16(y_min, value);
        y_max = plot_max_i16(y_max, value);
    }
    if (y_min > y_max) { return false; }
    plot_set_y_range(layout, y_min, y_max);
    return true;
}

bool plot_has_u8_values(const PlotLayout* layout, const uint8_t* values,
                        uint16_t length, uint16_t start_index,
                        uint8_t missing_value) {
    uint16_t count = plot_visible_count(layout, length, start_index);
    for (uint16_t i=0; i<count; i++) {
        if (values[start_index+i] != missing_value) { return true; }
    }
    return false;
}

uint16_t plot_visible_u8_count(const PlotLayout* layout, uint16_t length,
                               uint16_t start_index) {
    return plot_visible_count(layout, length, start_index);
}

void plot_draw_frame(GContext* ctx, const PlotLayout* layout, GColor color) {
    graphics_context_set_stroke_color(ctx, color);
    graphics_draw_rect(ctx, layout->frame);
}

void plot_draw_horizontal_line(GContext* ctx, const PlotLayout* layout,
                               int16_t value, GColor color,
                               uint8_t dash_length, uint8_t gap_length) {
    int16_t y = plot_y_for_value(layout, value);
    int16_t x1 = layout->area.origin.x;
    int16_t x2 = layout->area.origin.x + layout->area.size.w - 1;
    graphics_context_set_stroke_color(ctx, color);
    if (dash_length == 0 || gap_length == 0) {
        graphics_draw_line(ctx, GPoint(x1, y), GPoint(x2, y));
        return;
    }
    for (int16_t x=x1; x<=x2; x += dash_length+gap_length) {
        graphics_draw_line(ctx, GPoint(x, y),
                           GPoint(plot_min_i16(x+dash_length-1, x2), y));
    }
}

void plot_draw_vertical_line(GContext* ctx, const PlotLayout* layout,
                             int16_t x_offset, GColor color,
                             uint8_t dash_length, uint8_t gap_length) {
    if (x_offset < 0 || x_offset >= layout->area.size.w) { return; }
    int16_t x = layout->area.origin.x + x_offset;
    int16_t y1 = layout->area.origin.y;
    int16_t y2 = layout->area.origin.y + layout->area.size.h - 1;
    graphics_context_set_stroke_color(ctx, color);
    if (dash_length == 0 || gap_length == 0) {
        graphics_draw_line(ctx, GPoint(x, y1), GPoint(x, y2));
        return;
    }
    for (int16_t y=y1; y<=y2; y += dash_length+gap_length) {
        graphics_draw_line(ctx, GPoint(x, y),
                           GPoint(x, plot_min_i16(y+dash_length-1, y2)));
    }
}

void plot_draw_vertical_lines(GContext* ctx, const PlotLayout* layout,
                              const int16_t* x_offsets, uint16_t count,
                              GColor color, uint8_t dash_length,
                              uint8_t gap_length) {
    for (uint16_t i=0; i<count; i++) {
        plot_draw_vertical_line(ctx, layout, x_offsets[i], color,
                                dash_length, gap_length);
    }
}

uint16_t plot_draw_filled_line(GContext* ctx, const PlotLayout* layout,
                               const int16_t* values, uint16_t count,
                               GColor color) {
    graphics_context_set_fill_color(ctx, color);
    for (uint16_t i=0; i<count; i++) {
        plot_fill_column(ctx, layout, i, count, values[i]);
    }
    return count;
}

uint16_t plot_draw_u8_line(GContext* ctx, const PlotLayout* layout,
                           const uint8_t* values, uint16_t length,
                           uint16_t start_index, uint8_t missing_value,
                           int16_t decode_offset, GColor color) {
    int16_t last_x = 0;
    int16_t last_y = 0;
    bool has_last = false;
    uint16_t drawn = 0;
    uint16_t count = layout->area.size.w;

    graphics_context_set_stroke_color(ctx, color);
    for (uint16_t i=0; i<count; i++) {
        int16_t value;
        if (!plot_read_u8(values, length, start_index, i, missing_value,
                          decode_offset, false, &value)) {
            has_last = false;
            continue;
        }
        int16_t x = plot_x_for_index(layout, i, count);
        int16_t y = plot_y_for_value(layout, value);
        graphics_draw_line(ctx, GPoint(has_last ? last_x : x, has_last ? last_y : y),
                           GPoint(x, y));
        last_x = x;
        last_y = y;
        has_last = true;
        drawn += 1;
    }
    return drawn;
}

uint16_t plot_draw_u8_filled_line(GContext* ctx, const PlotLayout* layout,
                                  const uint8_t* values, uint16_t length,
                                  uint16_t start_index, uint8_t missing_value,
                                  int16_t decode_offset, bool hide_zero,
                                  GColor color) {
    uint16_t drawn = 0;
    uint16_t count = layout->area.size.w;
    graphics_context_set_fill_color(ctx, color);
    for (uint16_t i=0; i<count; i++) {
        int16_t value;
        if (plot_read_u8(values, length, start_index, i, missing_value,
                         decode_offset, hide_zero, &value)) {
            plot_fill_column(ctx, layout, i, count, value);
            drawn += 1;
        }
    }
    return drawn;
}

void plot_fill_tail(GContext* ctx, const PlotLayout* layout,
                    uint16_t first_missing_index, uint16_t total_count,
                    int16_t height, GColor color) {
    if (total_count == 0 || first_missing_index > total_count) { return; }
    int16_t x = first_missing_index == total_count ?
        layout->area.origin.x + layout->area.size.w - 1 :
        plot_x_for_index(layout, first_missing_index, total_count) - 1;
    int16_t x2 = layout->area.origin.x + layout->area.size.w - 1;
    if (x > x2) { return; }

    int16_t fill_height = plot_min_i16(height, layout->area.size.h);
    int16_t y_offset = plot_max_i16(0, layout->area.size.h-fill_height-1);
    graphics_context_set_fill_color(ctx, color);
    graphics_fill_rect(ctx, GRect(x, layout->area.origin.y+y_offset,
                                  x2-x+1, fill_height), 0, GCornerNone);
}
