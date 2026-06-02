#pragma once

#include <pebble.h>

typedef struct {
    GRect frame;
    GRect area;
    int16_t y_min;
    int16_t y_max;
} PlotLayout;

PlotLayout plot_layout(GRect frame, int16_t left, int16_t top,
                       int16_t right, int16_t bottom,
                       int16_t y_min, int16_t y_max);
void plot_set_y_range(PlotLayout* layout, int16_t y_min, int16_t y_max);
bool plot_set_y_range_from_u8(PlotLayout* layout, const uint8_t* values,
                              uint16_t length, uint16_t start_index,
                              uint8_t missing_value, int16_t decode_offset);
bool plot_has_u8_values(const PlotLayout* layout, const uint8_t* values,
                        uint16_t length, uint16_t start_index,
                        uint8_t missing_value);
uint16_t plot_visible_u8_count(const PlotLayout* layout, uint16_t length,
                               uint16_t start_index);

void plot_draw_frame(GContext* ctx, const PlotLayout* layout, GColor color);
void plot_draw_horizontal_line(GContext* ctx, const PlotLayout* layout,
                               int16_t value, GColor color,
                               uint8_t dash_length, uint8_t gap_length);
void plot_draw_vertical_line(GContext* ctx, const PlotLayout* layout,
                             int16_t x_offset, GColor color,
                             uint8_t dash_length, uint8_t gap_length);
void plot_draw_vertical_lines(GContext* ctx, const PlotLayout* layout,
                              const int16_t* x_offsets, uint16_t count,
                              GColor color, uint8_t dash_length,
                              uint8_t gap_length);

uint16_t plot_draw_filled_line(GContext* ctx, const PlotLayout* layout,
                               const int16_t* values, uint16_t count,
                               GColor color);
uint16_t plot_draw_u8_line(GContext* ctx, const PlotLayout* layout,
                           const uint8_t* values, uint16_t length,
                           uint16_t start_index, uint8_t missing_value,
                           int16_t decode_offset, GColor color);
uint16_t plot_draw_u8_filled_line(GContext* ctx, const PlotLayout* layout,
                                  const uint8_t* values, uint16_t length,
                                  uint16_t start_index, uint8_t missing_value,
                                  int16_t decode_offset, bool hide_zero,
                                  GColor color);
void plot_fill_tail(GContext* ctx, const PlotLayout* layout,
                    uint16_t first_missing_index, uint16_t total_count,
                    int16_t height, GColor color);
