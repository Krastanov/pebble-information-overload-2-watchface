#include <pebble.h>
#include <pebble-fctx/fctx.h>
#include <pebble-fctx/fpath.h>
#include <pebble-fctx/ffont.h>
#include "plot.h"

// message buffer size:
#define MESSAGE_BUF 1024
#define WEATHER_DAY_GRAPH_SAMPLES 48
#define WEATHER_DAY_GRAPH_UNKNOWN 255
#define WEATHER_TEMP_UNKNOWN ((int8_t)-128)
#define WEATHER_TEMP_LEGACY_UNKNOWN ((int8_t)101)
#define WEATHER_DETAIL_UNKNOWN 255
#define WEATHER_PERCENT_UNKNOWN 101
#define WEATHER_PRECIP_GRAPH_WIDTH 49
#define WEATHER_PRECIP_GRAPH_INNER_WIDTH (WEATHER_PRECIP_GRAPH_WIDTH - 4)
#define TOP_DATA_BOTTOM 113
#define REPORT_DATA_Y 17
#define REPORT_DATA_HEIGHT (TOP_DATA_BOTTOM - REPORT_DATA_Y)
#define CALENDAR_DATA_Y 0
#define CALENDAR_DATA_HEIGHT (TOP_DATA_BOTTOM - CALENDAR_DATA_Y)
#define REPORT_TEXT_LENGTH 220
#define CALENDAR_TEXT_LENGTH 256
#define CALENDAR_ENTRY_COUNT 8
#define CALENDAR_ROW_HEIGHT 14
#define CALENDAR_BAR_WIDTH 3

// TODO Add `const` where appropriate!
// TODO not all memory is released?

#define max(a,b) \
    ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
       _a > _b ? _a : _b; })
#define min(a,b) \
    ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
       _a < _b ? _a : _b; })

// --------------------------------------------------------------------------
// Types and global variables.
// --------------------------------------------------------------------------

static Window* g_window;
static TextLayer* g_time_layer;               // Layer updated on minute events.
static TextLayer* g_date_layer;               // Layer updated on day events.
static Layer* g_battery_layer;                // Layer updated on battery events.
static Layer* g_connection_layer;             // Layer updated on connection events.
static TextLayer* g_health_cals_text_layer;   // Layer updated on health events.
static TextLayer* g_health_meters_text_layer; // Layer updated on health events.
static TextLayer* g_health_sleep_text_layer;  // Layer updated on health events.
static Layer* g_health_bpm_heart_layer;       // Static heart symbol in the heart-rate row.
static TextLayer* g_health_bpm_text_layer;    // Layer updated on heart beat events.
static Layer* g_health_bpm_graph_layer;       // Layer updated on heart beat events or on minute ticks.
static Layer* g_weather_temp_layer;           // Layer updated on weather events from PebbleKit messages.
static Layer* g_weather_icon_layer;           // Layer updated on weather events from PebbleKit messages.
static Layer* g_weather_precipprob_layer;     // Layer updated on weather events from PebbleKit messages.
static Layer* g_weather_precipgraph_layer;    // Layer updated on weather events from PebbleKit messages or on minute ticks.
static Layer* g_weather_detail_layer;         // Layer updated on weather events from PebbleKit messages or on minute ticks.
static Layer* g_weather_day_graph_layer;      // Layer updated on weather events from PebbleKit messages or on minute ticks.
static TextLayer* g_weather_humidity_layer;   // Layer updated on weather events from PebbleKit messages.
static TextLayer* g_weather_wind_layer;       // Layer updated on weather events from PebbleKit messages.
static TextLayer* g_report_layer;             // General purpose report layer - text generated in javascript.
static Layer* g_calendar_layer;               // Upcoming calendar events generated in javascript.
static struct tm g_local_time;
static uint8_t g_battery_level;
static int8_t g_connected; // TODO Should be bool!
static int8_t g_atemp = WEATHER_TEMP_UNKNOWN;
static int8_t g_atempmax = WEATHER_TEMP_UNKNOWN;
static int8_t g_atempmin = WEATHER_TEMP_UNKNOWN;
static int8_t g_temp = WEATHER_TEMP_UNKNOWN;
static int8_t g_tempmax = WEATHER_TEMP_UNKNOWN;
static int8_t g_tempmin = WEATHER_TEMP_UNKNOWN;
static uint8_t g_precipprob;
static uint8_t g_weather_icon; // TODO Use less obfuscated data type!
static uint8_t g_weather_precip_array[60];
static uint8_t g_ticks_since_weather_array_update;
static uint8_t g_weather_day_atemp_array[WEATHER_DAY_GRAPH_SAMPLES];
static uint8_t g_weather_day_precip_array[WEATHER_DAY_GRAPH_SAMPLES];
static uint16_t g_ticks_since_weather_day_graph_update;
static uint8_t g_weather_uv_index = WEATHER_DETAIL_UNKNOWN;
static uint8_t g_weather_cloud_cover = WEATHER_PERCENT_UNKNOWN;
static uint8_t g_weather_visibility_km = WEATHER_DETAIL_UNKNOWN;
static bool g_show_short_precipgraph;
static char g_report_string[REPORT_TEXT_LENGTH];
static char g_calendar_string[CALENDAR_TEXT_LENGTH];
static uint8_t g_calendar_color_array[CALENDAR_ENTRY_COUNT];
static AppSync g_sync;
static uint8_t g_sync_buffer[MESSAGE_BUF];

enum CommKey {
  WEATHER_ICON_KEY = 0x0,
  WEATHER_ATEMPERATURE_KEY = 0x1,
  WEATHER_ATEMPERATUREMAX_KEY = 0x2,
  WEATHER_ATEMPERATUREMIN_KEY = 0x3,
  WEATHER_TEMPERATURE_KEY = 0x4,
  WEATHER_TEMPERATUREMAX_KEY = 0x5,
  WEATHER_TEMPERATUREMIN_KEY = 0x6,
  WEATHER_PRECIP_PROB_KEY = 0x7,
  WEATHER_PRECIP_ARRAY_KEY = 0x8,
  WEATHER_HUMIDITY_KEY = 0x9,
  WEATHER_WIND_SPEED_KEY = 0xA,
  REPORT_KEY = 0xB,
  WEATHER_DAY_ATEMP_ARRAY_KEY = 0xC,
  WEATHER_DAY_PRECIP_ARRAY_KEY = 0xD,
  WEATHER_UV_INDEX_KEY = 0xE,
  WEATHER_CLOUD_COVER_KEY = 0xF,
  WEATHER_VISIBILITY_KEY = 0x10,
  CALENDAR_KEY = 0x11,
  CALENDAR_COLORS_KEY = 0x12
};

static GColor weather_icon_color(uint8_t weather_icon) {
    switch (weather_icon) {
        case 1:  // clear day
        case 9:  // partly cloudy day
            return PBL_IF_COLOR_ELSE(GColorYellow, GColorWhite);
        case 3:  // rain/thunderstorm
        case 4:  // snow
        case 5:  // sleet
            return PBL_IF_COLOR_ELSE(GColorCyan, GColorWhite);
        case 2:  // clear night
        case 6:  // wind
        case 7:  // fog/haze/dust
        case 8:  // cloudy
        case 10: // partly cloudy night
        default:
            return GColorWhite;
    }
}

static GColor calendar_color(uint8_t color_id) {
    switch (color_id) {
        case 1: return PBL_IF_COLOR_ELSE(GColorGreen, GColorWhite);
        case 2: return PBL_IF_COLOR_ELSE(GColorBlue, GColorWhite);
        case 3: return PBL_IF_COLOR_ELSE(GColorRed, GColorWhite);
        case 4: return PBL_IF_COLOR_ELSE(GColorYellow, GColorWhite);
        case 5: return PBL_IF_COLOR_ELSE(GColorCyan, GColorWhite);
        case 6: return PBL_IF_COLOR_ELSE(GColorMagenta, GColorWhite);
        case 7: return PBL_IF_COLOR_ELSE(GColorOrange, GColorWhite);
        case 8: return PBL_IF_COLOR_ELSE(GColorPurple, GColorWhite);
        default: return GColorWhite;
    }
}

static bool weather_temp_is_known(int8_t temp) {
    return temp != WEATHER_TEMP_UNKNOWN && temp != WEATHER_TEMP_LEGACY_UNKNOWN;
}

static void draw_weather_temp(GContext* ctx, int8_t temp, GColor color,
                              GRect rect, GTextAlignment alignment) {
    if (!weather_temp_is_known(temp)) {return;}

    char temp_string[5];
    snprintf(temp_string, sizeof temp_string, "%d", temp);
    graphics_context_set_text_color(ctx, color);
    graphics_draw_text(ctx, temp_string,
                       fonts_get_system_font(FONT_KEY_GOTHIC_14),
                       rect, GTextOverflowModeWordWrap, alignment, NULL);
}

static bool weather_precipgraph_has_visible_values(void) {
    uint16_t start = g_ticks_since_weather_array_update;
    uint16_t count = sizeof(g_weather_precip_array);
    if (start >= count) { return false; }

    uint16_t end = start + WEATHER_PRECIP_GRAPH_INNER_WIDTH;
    if (end > count) { end = count; }
    for (uint16_t i=start; i<end; i++) {
        if (g_weather_precip_array[i] != 0) { return true; }
    }
    return false;
}

static bool weather_short_precip_has_visible_data(void) {
    return g_precipprob > 0 || weather_precipgraph_has_visible_values();
}

static bool weather_short_precip_visible(void) {
    return g_show_short_precipgraph && weather_short_precip_has_visible_data();
}

static uint8_t palette_size_for_format(GBitmapFormat format) {
    switch (format) {
        case GBitmapFormat1BitPalette: return 2;
        case GBitmapFormat2BitPalette: return 4;
        case GBitmapFormat4BitPalette: return 16;
        default: return 0;
    }
}

static void tint_weather_icon_bitmap(GBitmap* bitmap, GColor tint_color) {
    GBitmapFormat format = gbitmap_get_format(bitmap);
    uint8_t palette_size = palette_size_for_format(format);
    if (palette_size) {
        GColor* palette = gbitmap_get_palette(bitmap);
        if (!palette) {return;}
        for (int i=0; i<palette_size; i++) {
            if (palette[i].a != 0) {
                uint8_t alpha = palette[i].a;
                palette[i] = tint_color;
                palette[i].a = alpha;
            }
        }
        return;
    }

    if (format != GBitmapFormat8Bit) {return;}

    GRect bounds = gbitmap_get_bounds(bitmap);
    uint8_t* data = gbitmap_get_data(bitmap);
    uint16_t bytes_per_row = gbitmap_get_bytes_per_row(bitmap);
    if (!data || bytes_per_row == 0) {return;}

    for (int y=0; y<bounds.size.h; y++) {
        for (int x=0; x<bounds.size.w; x++) {
            uint8_t* pixel = data + y * bytes_per_row + x;
            GColor pixel_color = (GColor){.argb = *pixel};
            if (pixel_color.a != 0) {
                GColor tinted_pixel = tint_color;
                tinted_pixel.a = pixel_color.a;
                *pixel = tinted_pixel.argb;
            }
        }
    }
}

// --------------------------------------------------------------------------
// Drawing function.
// --------------------------------------------------------------------------

static void on_battery_layer_update(Layer* layer, GContext* ctx) {
    GRect bounds = layer_get_bounds(layer);
    int BAT_W = bounds.size.w;
    int BAT_H = bounds.size.h-2;
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_draw_rect(ctx, GRect(0, 2, BAT_W, BAT_H));
    graphics_draw_rect(ctx, GRect(BAT_W/2-2, 0, 4, 2));
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_draw_rect(ctx, GRect(BAT_W/2-1, 1, 2, 2));
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, GRect(2, 4+(BAT_H-4)*(100-g_battery_level)/100, BAT_W-4, (BAT_H-4)*g_battery_level/100), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(2, BAT_H-1, BAT_W-4, 1), 0, GCornerNone); // XXX Due to rounding errors.
    if (g_battery_level<=5) {
        graphics_context_set_fill_color(ctx, GColorDarkGray);
        graphics_fill_rect(ctx, GRect(2, BAT_H-1, BAT_W-4, 1), 0, GCornerNone);
    }
}

static void on_connection_layer_update(Layer* layer, GContext* ctx) {
    graphics_context_set_stroke_color(ctx, GColorWhite);
    if (g_connected != 0) {
        graphics_draw_line(ctx, GPoint(3, 0), GPoint(3, 12));
        graphics_draw_line(ctx, GPoint(3, 0), GPoint(6, 3));
        graphics_draw_line(ctx, GPoint(3, 12), GPoint(6, 9));
    }
    graphics_draw_line(ctx, GPoint(0, 3), GPoint(6, 9));
    graphics_draw_line(ctx, GPoint(0, 9), GPoint(6, 3));
}

static void on_health_bpm_graph_layer_update(Layer* layer, GContext* ctx) {
    HealthMinuteData minute_data[60];
    int16_t bpm_values[30];
    int16_t last_bpm = 50;
    time_t t2 = time(NULL);
    time_t t1 = t2 - SECONDS_PER_HOUR;
    // TODO Why not health_service_get_minute_history(minute_data, sizeof(minute_data), &t1, &t2));
    health_service_get_minute_history(&minute_data[0], 60, &t1, &t2);

    for (int i=0; i<60; i++) {
        if (!minute_data[i].is_invalid && minute_data[i].heart_rate_bpm != 0) {
            last_bpm = minute_data[i].heart_rate_bpm;
        }
        // Read 60 minutes so the plotted 30-minute window can start from the
        // most recent valid point before it.
        if (i >= 30) {
            bpm_values[i-30] = last_bpm;
        }
    }

    PlotLayout plot = plot_layout(layer_get_bounds(layer), 2, 1, 2, 1, 50, 145);
    plot_draw_horizontal_line(ctx, &plot, 95, GColorDarkGray, 0, 0);
    plot_draw_filled_line(ctx, &plot, bpm_values, ARRAY_LENGTH(bpm_values),
                          PBL_IF_COLOR_ELSE(GColorRed, GColorWhite));
    plot_draw_frame(ctx, &plot, GColorWhite);
    plot_draw_vertical_line(ctx, &plot, 14, GColorDarkGray, 0, 0);
}

static void on_health_bpm_heart_layer_update(Layer* layer, GContext* ctx) {
    graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorRed, GColorWhite));
    graphics_fill_rect(ctx, GRect(1, 3, 2, 1), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(5, 3, 2, 1), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(0, 4, 8, 2), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(1, 6, 6, 1), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(2, 7, 4, 1), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(3, 8, 2, 1), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(4, 9, 1, 1), 0, GCornerNone);
}

static void on_weather_temp_layer_update(Layer* layer, GContext* ctx) {
    draw_weather_temp(ctx, g_temp, GColorWhite,
                      GRect(0,7,18,15), GTextAlignmentRight);
    draw_weather_temp(ctx, g_tempmax, PBL_IF_COLOR_ELSE(GColorRed, GColorWhite),
                      GRect(16-((g_tempmax<0)?5:0),0,18,15),
                      GTextAlignmentLeft);
    draw_weather_temp(ctx, g_tempmin, PBL_IF_COLOR_ELSE(GColorCyan, GColorWhite),
                      GRect(16-((g_tempmin<0)?5:0),14,18,15),
                      GTextAlignmentLeft);
    draw_weather_temp(ctx, g_atemp, GColorWhite,
                      GRect(20,7,18,15), GTextAlignmentRight);
    draw_weather_temp(ctx, g_atempmax, PBL_IF_COLOR_ELSE(GColorRed, GColorWhite),
                      GRect(38-((g_atempmax<0)?5:0),0,18,15),
                      GTextAlignmentLeft);
    draw_weather_temp(ctx, g_atempmin, PBL_IF_COLOR_ELSE(GColorCyan, GColorWhite),
                      GRect(38-((g_atempmin<0)?5:0),14,18,15),
                      GTextAlignmentLeft);
}

static void on_weather_icon_layer_update(Layer* layer, GContext* ctx) {
    static GBitmap *s_bitmap;
    if (s_bitmap) {
      gbitmap_destroy(s_bitmap);
    }
    uint32_t resource_id = 0;
    if (g_weather_icon) {
        switch (g_weather_icon) { // TODO Mark in readme https://icons8.com/ as icons' source!
            case  1: resource_id = RESOURCE_ID_Sun_25; break;
            case  2: resource_id = RESOURCE_ID_Bright_Moon_25; break;
            case  3: resource_id = RESOURCE_ID_Rain_25; break;
            case  4: resource_id = RESOURCE_ID_Snow_25; break;
            case  5: resource_id = RESOURCE_ID_Sleet_25; break;
            case  6: resource_id = RESOURCE_ID_Air_Element_25; break;
            case  7: resource_id = RESOURCE_ID_Dust_25; break;
            case  8: resource_id = RESOURCE_ID_Clouds_25; break;
            case  9: resource_id = RESOURCE_ID_Partly_Cloudy_Day_25; break;
            case 10: resource_id = RESOURCE_ID_Partly_Cloudy_Night_25; break;
        }
        s_bitmap  = gbitmap_create_with_resource(resource_id);
        if (!s_bitmap) {return;}
        tint_weather_icon_bitmap(s_bitmap, weather_icon_color(g_weather_icon));
        graphics_context_set_compositing_mode(ctx, GCompOpSet);
        graphics_draw_bitmap_in_rect(ctx, s_bitmap, GRect(0,0,25,25));
    }
}

static void on_weather_precipprob_layer_update(Layer* layer, GContext* ctx) { // TODO make text layer with wrapping.
    GRect bounds = layer_get_bounds(layer);
    char percent_string[4];
    if (g_show_short_precipgraph && g_precipprob > 0) {
        snprintf(percent_string, sizeof percent_string, "%d", g_precipprob);
        graphics_context_set_text_color(ctx, PBL_IF_COLOR_ELSE(GColorCyan, GColorWhite));
        graphics_draw_text(ctx, percent_string,
                           fonts_get_system_font(FONT_KEY_GOTHIC_14),
                           GRect(0,2,bounds.size.w,15), GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
        graphics_draw_text(ctx, "%",
                           fonts_get_system_font(FONT_KEY_GOTHIC_14),
                           GRect(0,13,bounds.size.w,15), GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
    }
}

static void on_weather_precipgraph_layer_update(Layer* layer, GContext* ctx) {
    if (!g_show_short_precipgraph) { return; }

    static const int16_t grid_lines[] = {15, 30};
    PlotLayout plot = plot_layout(layer_get_bounds(layer), 2, 1, 2, 1, 0, 240);

    uint16_t drawn = plot_draw_u8_filled_line(
        ctx, &plot, g_weather_precip_array, sizeof(g_weather_precip_array),
        g_ticks_since_weather_array_update, 0, 0, true,
        PBL_IF_COLOR_ELSE(GColorCyan, GColorWhite));
    if (drawn == 0) {
        return;
    }

    plot_draw_frame(ctx, &plot, GColorWhite);
    plot_draw_vertical_lines(ctx, &plot, grid_lines, ARRAY_LENGTH(grid_lines),
                             GColorDarkGray, 0, 0);
    if (g_ticks_since_weather_array_update > 15) {
        uint16_t first_missing = plot_visible_u8_count(
            &plot, sizeof(g_weather_precip_array),
            g_ticks_since_weather_array_update);
        plot_fill_tail(ctx, &plot, first_missing, plot.area.size.w, 2,
                       GColorDarkGray);
    }
}

static GColor weather_uv_color(uint8_t uv_index) {
    if (uv_index >= 7) { return PBL_IF_COLOR_ELSE(GColorRed, GColorWhite); }
    if (uv_index >= 3) { return PBL_IF_COLOR_ELSE(GColorYellow, GColorWhite); }
    return GColorWhite;
}

static GColor weather_visibility_color(uint8_t visibility_km) {
    if (visibility_km < 5) { return PBL_IF_COLOR_ELSE(GColorRed, GColorWhite); }
    if (visibility_km < 9) { return PBL_IF_COLOR_ELSE(GColorYellow, GColorWhite); }
    return GColorWhite;
}

static void draw_weather_detail(GContext* ctx, const char* value,
                                const char* label, GRect rect,
                                GColor color) {
    graphics_context_set_text_color(ctx, color);
    graphics_draw_text(ctx, value,
                       fonts_get_system_font(FONT_KEY_GOTHIC_14),
                       GRect(rect.origin.x, 2, rect.size.w, 15),
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    graphics_draw_text(ctx, label,
                       fonts_get_system_font(FONT_KEY_GOTHIC_14),
                       GRect(rect.origin.x, 13, rect.size.w, 15),
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static GRect weather_detail_cell(GRect bounds, uint8_t index) {
    int cell_width = bounds.size.w/3;
    int extra = bounds.size.w - cell_width*3;
    int x = 0;
    for (uint8_t i=0; i<index; i++) {
        x += cell_width + (i < extra ? 1 : 0);
    }
    return GRect(x, 0, cell_width + (index < extra ? 1 : 0), bounds.size.h);
}

static void on_weather_detail_layer_update(Layer* layer, GContext* ctx) {
    GRect bounds = layer_get_bounds(layer);
    if (weather_short_precip_visible()) { return; }

    char value_string[5];

    if (g_weather_uv_index != WEATHER_DETAIL_UNKNOWN) {
        snprintf(value_string, sizeof value_string, "%d", g_weather_uv_index);
        draw_weather_detail(ctx, value_string, "UV",
                            weather_detail_cell(bounds, 0),
                            weather_uv_color(g_weather_uv_index));
    }

    if (g_weather_cloud_cover < WEATHER_PERCENT_UNKNOWN) {
        snprintf(value_string, sizeof value_string, "%d", g_weather_cloud_cover);
        draw_weather_detail(ctx, value_string, "%",
                            weather_detail_cell(bounds, 1), GColorWhite);
    }

    if (g_weather_visibility_km != WEATHER_DETAIL_UNKNOWN) {
        snprintf(value_string, sizeof value_string, "%d", g_weather_visibility_km);
        draw_weather_detail(ctx, value_string, "km",
                            weather_detail_cell(bounds, 2),
                            weather_visibility_color(g_weather_visibility_km));
    }
}

static void on_weather_day_graph_layer_update(Layer* layer, GContext* ctx) {
    static const int16_t grid_lines[] = {12, 24, 36};
    PlotLayout plot = plot_layout(layer_get_bounds(layer), 1, 1, 1, 1, 0, 100);
    uint8_t half_hour_offset = min(WEATHER_DAY_GRAPH_SAMPLES,
                                   g_ticks_since_weather_day_graph_update/30);
    bool has_temp = plot_has_u8_values(&plot, g_weather_day_atemp_array,
                                       WEATHER_DAY_GRAPH_SAMPLES,
                                       half_hour_offset,
                                       WEATHER_DAY_GRAPH_UNKNOWN);
    bool has_precip = plot_has_u8_values(&plot, g_weather_day_precip_array,
                                         WEATHER_DAY_GRAPH_SAMPLES,
                                         half_hour_offset,
                                         WEATHER_DAY_GRAPH_UNKNOWN);

    if (!has_temp && !has_precip) {
        return;
    }

    plot_draw_frame(ctx, &plot, GColorWhite);
    plot_draw_vertical_lines(ctx, &plot, grid_lines, ARRAY_LENGTH(grid_lines),
                             GColorDarkGray, 0, 0);

    if (has_precip) {
        plot_set_y_range(&plot, 0, 100);
        plot_draw_u8_filled_line(ctx, &plot, g_weather_day_precip_array,
                                 WEATHER_DAY_GRAPH_SAMPLES, half_hour_offset,
                                 WEATHER_DAY_GRAPH_UNKNOWN, 0, true,
                                 PBL_IF_COLOR_ELSE(GColorCyan, GColorWhite));
        plot_draw_vertical_lines(ctx, &plot, grid_lines, ARRAY_LENGTH(grid_lines),
                                 GColorDarkGray, 0, 0);
    }

    if (has_temp) {
        plot_set_y_range_from_u8(&plot, g_weather_day_atemp_array,
                                 WEATHER_DAY_GRAPH_SAMPLES,
                                 half_hour_offset,
                                 WEATHER_DAY_GRAPH_UNKNOWN,
                                 -100);
        plot_draw_u8_line(ctx, &plot, g_weather_day_atemp_array,
                          WEATHER_DAY_GRAPH_SAMPLES, half_hour_offset,
                          WEATHER_DAY_GRAPH_UNKNOWN, -100,
                          PBL_IF_COLOR_ELSE(GColorRed, GColorWhite));
    }

    plot_draw_frame(ctx, &plot, GColorWhite);
}

static void on_calendar_layer_update(Layer* layer, GContext* ctx) {
    GRect bounds = layer_get_bounds(layer);
    int16_t y = 0;
    uint8_t row_index = 0;
    const char* cursor = g_calendar_string;
    GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14);

    graphics_context_set_text_color(ctx, GColorWhite);
    while (*cursor && row_index < CALENDAR_ENTRY_COUNT &&
           y + CALENDAR_ROW_HEIGHT <= bounds.size.h) {
        char row[CALENDAR_TEXT_LENGTH];
        uint16_t line_len = 0;
        uint16_t copy_len = 0;

        while (cursor[line_len] && cursor[line_len] != '\n') {
            if (copy_len < sizeof(row) - 1) {
                row[copy_len++] = cursor[line_len];
            }
            line_len++;
        }
        row[copy_len] = '\0';

        if (copy_len > 0) {
            graphics_context_set_fill_color(ctx,
                                            calendar_color(g_calendar_color_array[row_index]));
            graphics_fill_rect(ctx, GRect(2, y + 2, CALENDAR_BAR_WIDTH,
                                          CALENDAR_ROW_HEIGHT - 4),
                               0, GCornerNone);
            graphics_context_set_text_color(ctx, GColorWhite);
            graphics_draw_text(ctx, row, font,
                               GRect(2 + CALENDAR_BAR_WIDTH + 2, y,
                                     bounds.size.w - CALENDAR_BAR_WIDTH - 6,
                                     CALENDAR_ROW_HEIGHT),
                               GTextOverflowModeTrailingEllipsis,
                               GTextAlignmentLeft, NULL);
        }

        cursor += line_len;
        if (*cursor == '\n') { cursor++; }
        y += CALENDAR_ROW_HEIGHT;
        row_index++;
    }
}

// --------------------------------------------------------------------------
// System event handlers.
// --------------------------------------------------------------------------

static void on_tick_timer(struct tm* tick_time, TimeUnits units_changed) {
    g_local_time = *tick_time;
    g_ticks_since_weather_array_update += 1;
    g_ticks_since_weather_day_graph_update += 1;
    static char time_string[6];
    static char date_string[7];
    strftime(time_string, sizeof time_string, "%H:%M", &g_local_time);
    text_layer_set_text(g_time_layer, time_string);
    strftime(date_string, sizeof date_string, "%b %d", &g_local_time);
    text_layer_set_text(g_date_layer, date_string);
    layer_mark_dirty(g_health_bpm_graph_layer);
    layer_mark_dirty(g_weather_precipgraph_layer);
    layer_mark_dirty(g_weather_detail_layer);
    layer_mark_dirty(g_weather_day_graph_layer);
}

static void on_battery_state(BatteryChargeState state) {
    g_battery_level = state.charge_percent;
    layer_mark_dirty(g_battery_layer);
}

static void on_connection(bool connected) {
    g_connected = connected ? 1 : 0; // TODO weird data type conversion
    layer_mark_dirty(g_connection_layer);
}

static void on_health_heartrate() {
    static char bpm_string[8];
    snprintf(bpm_string, sizeof bpm_string, "%d", (int)health_service_peek_current_value(HealthMetricHeartRateBPM));
    text_layer_set_text(g_health_bpm_text_layer, bpm_string);
}

static void on_health_movement() {
    //static char Cal_string[14];
    static char meter_string[7];
    //snprintf(Cal_string, sizeof Cal_string, "%dCal(%d)", (int)(health_service_sum_today(HealthMetricRestingKCalories)+health_service_sum_today(HealthMetricActiveKCalories)), (int)health_service_sum_today(HealthMetricActiveKCalories)); 
    //text_layer_set_text(g_health_cals_text_layer, Cal_string);
    int walked_meters = health_service_sum_today(HealthMetricWalkedDistanceMeters);
    snprintf(meter_string, sizeof meter_string, "%d.%dkm", walked_meters/1000, (walked_meters%1000)/100);
    text_layer_set_text(g_health_meters_text_layer, meter_string);
}

static void on_health_sleep() {
    static char sleep_string[13];
    int32_t sleep = health_service_sum_today(HealthMetricSleepSeconds);
    int32_t restful = health_service_sum_today(HealthMetricSleepRestfulSeconds);
    snprintf(sleep_string, sizeof sleep_string, "%d%%/%d.%dh", (int)(restful*100/sleep), (int)(sleep/3600), (int)((sleep%3600)*10/3600)); 
    text_layer_set_text(g_health_sleep_text_layer, sleep_string);
}

static void on_health(const HealthEventType event, void* context) {
    switch (event) {
        case HealthEventHeartRateUpdate: on_health_heartrate(); break;
        case HealthEventMovementUpdate: on_health_movement(); break;
        case HealthEventSleepUpdate: on_health_sleep(); break;
        case HealthEventSignificantUpdate:
            on_health_heartrate();
            on_health_movement();
            on_health_sleep();
            break;
        default: break;
    }
}

static void on_tap(AccelAxisType axis, int32_t direction) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "tap: %d %d", axis, direction);
}

static void on_sync_error(DictionaryResult dict_error, AppMessageResult app_message_error, void *context) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Dict Error: %d; App Message Sync Error: %d", dict_error, app_message_error);
}

static void on_sync_tuple_change(const uint32_t key, const Tuple* new_tuple, const Tuple* old_tuple, void* context) {
    static char humidity_string[5];
    static char wind_string[8];
    switch (key) {
        case WEATHER_ICON_KEY:
            g_weather_icon = new_tuple->value->uint8;
            layer_mark_dirty(g_weather_icon_layer);
            break;
        case WEATHER_ATEMPERATURE_KEY:
            g_atemp = new_tuple->value->int8;
            layer_mark_dirty(g_weather_temp_layer);
            break;
        case WEATHER_ATEMPERATUREMAX_KEY:
            g_atempmax = new_tuple->value->int8;
            layer_mark_dirty(g_weather_temp_layer);
            break;
        case WEATHER_ATEMPERATUREMIN_KEY:
            g_atempmin = new_tuple->value->int8;
            layer_mark_dirty(g_weather_temp_layer);
            break;
        case WEATHER_TEMPERATURE_KEY:
            g_temp = new_tuple->value->int8;
            layer_mark_dirty(g_weather_temp_layer);
            break;
        case WEATHER_TEMPERATUREMAX_KEY:
            g_tempmax = new_tuple->value->int8;
            layer_mark_dirty(g_weather_temp_layer);
            break;
        case WEATHER_TEMPERATUREMIN_KEY:
            g_tempmin = new_tuple->value->int8;
            layer_mark_dirty(g_weather_temp_layer);
            break;
        case WEATHER_PRECIP_PROB_KEY:
            g_precipprob = new_tuple->value->uint8;
            layer_mark_dirty(g_weather_precipprob_layer);
            layer_mark_dirty(g_weather_detail_layer);
            break;
        case WEATHER_PRECIP_ARRAY_KEY: {
            for (int i=0; i<(int)sizeof(g_weather_precip_array); i++) {
                g_weather_precip_array[i] = 0;
            }
            int copy_len = min(new_tuple->length, sizeof(g_weather_precip_array));
            if (copy_len > 0) {
                memcpy(g_weather_precip_array, new_tuple->value->data, copy_len);
            }
            g_ticks_since_weather_array_update = 0;
            layer_mark_dirty(g_weather_precipgraph_layer);
            layer_mark_dirty(g_weather_detail_layer);
            break;
        }
        case WEATHER_HUMIDITY_KEY:
            if (new_tuple->value->uint8 < 101) {
                snprintf(humidity_string, sizeof humidity_string, "%d%%", new_tuple->value->uint8); 
                text_layer_set_text(g_weather_humidity_layer, humidity_string);
            }
            break;
        case WEATHER_WIND_SPEED_KEY:
            if (new_tuple->value->uint16/10 < 100) {
                snprintf(wind_string, sizeof wind_string, "%dm/s", new_tuple->value->uint16/10); 
                text_layer_set_text(g_weather_wind_layer, wind_string);
            }
            break;
        case REPORT_KEY:
            strncpy(g_report_string, new_tuple->value->cstring, sizeof(g_report_string) - 1);
            g_report_string[sizeof(g_report_string) - 1] = '\0';
            text_layer_set_text(g_report_layer, g_report_string);
            break;
        case CALENDAR_KEY:
            strncpy(g_calendar_string, new_tuple->value->cstring, sizeof(g_calendar_string) - 1);
            g_calendar_string[sizeof(g_calendar_string) - 1] = '\0';
            layer_mark_dirty(g_calendar_layer);
            break;
        case CALENDAR_COLORS_KEY: {
            for (int i=0; i<CALENDAR_ENTRY_COUNT; i++) {
                g_calendar_color_array[i] = 0;
            }
            int copy_len = min(new_tuple->length, sizeof(g_calendar_color_array));
            if (copy_len > 0) {
                memcpy(g_calendar_color_array, new_tuple->value->data, copy_len);
            }
            layer_mark_dirty(g_calendar_layer);
            break;
        }
        case WEATHER_DAY_ATEMP_ARRAY_KEY: {
            for (int i=0; i<WEATHER_DAY_GRAPH_SAMPLES; i++) {
                g_weather_day_atemp_array[i] = WEATHER_DAY_GRAPH_UNKNOWN;
            }
            int copy_len = min(new_tuple->length, sizeof(g_weather_day_atemp_array));
            if (copy_len > 0) {
                memcpy(g_weather_day_atemp_array, new_tuple->value->data, copy_len);
            }
            g_ticks_since_weather_day_graph_update = 0;
            layer_mark_dirty(g_weather_day_graph_layer);
            break;
        }
        case WEATHER_DAY_PRECIP_ARRAY_KEY: {
            for (int i=0; i<WEATHER_DAY_GRAPH_SAMPLES; i++) {
                g_weather_day_precip_array[i] = WEATHER_DAY_GRAPH_UNKNOWN;
            }
            int copy_len = min(new_tuple->length, sizeof(g_weather_day_precip_array));
            if (copy_len > 0) {
                memcpy(g_weather_day_precip_array, new_tuple->value->data, copy_len);
            }
            g_ticks_since_weather_day_graph_update = 0;
            layer_mark_dirty(g_weather_day_graph_layer);
            break;
        }
        case WEATHER_UV_INDEX_KEY:
            g_weather_uv_index = new_tuple->value->uint8;
            layer_mark_dirty(g_weather_detail_layer);
            break;
        case WEATHER_CLOUD_COVER_KEY:
            g_weather_cloud_cover = new_tuple->value->uint8;
            layer_mark_dirty(g_weather_detail_layer);
            break;
        case WEATHER_VISIBILITY_KEY:
            g_weather_visibility_km = new_tuple->value->uint8;
            layer_mark_dirty(g_weather_detail_layer);
            break;
        default:
            break;
    }
}


// --------------------------------------------------------------------------
// Initialization and teardown.
// --------------------------------------------------------------------------

static void init() {
    g_window = window_create();
    window_stack_push(g_window, true);
    window_set_background_color(g_window, GColorBlack);
    Layer* window_layer = window_get_root_layer(g_window);
    GRect bounds = layer_get_frame(window_layer);

    g_time_layer = text_layer_create(GRect(bounds.size.w-134, bounds.size.h-43-40-32, 132, 42));
    layer_add_child(window_layer, text_layer_get_layer(g_time_layer));
    text_layer_set_background_color(g_time_layer, GColorBlack);
    text_layer_set_text_color(g_time_layer, GColorWhite);
    text_layer_set_font(g_time_layer, fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS));
    text_layer_set_text_alignment(g_time_layer, GTextAlignmentRight);

    g_date_layer = text_layer_create(GRect(bounds.size.w-70, bounds.size.h-43-40+10, 66, 30));
    layer_add_child(window_layer, text_layer_get_layer(g_date_layer));
    text_layer_set_background_color(g_date_layer, GColorBlack);
    text_layer_set_text_color(g_date_layer, GColorWhite);
    text_layer_set_font(g_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
    text_layer_set_text_alignment(g_date_layer, GTextAlignmentRight);

    g_report_layer = text_layer_create(GRect(1, REPORT_DATA_Y,
                                             bounds.size.w/2-2,
                                             REPORT_DATA_HEIGHT));
    layer_add_child(window_layer, text_layer_get_layer(g_report_layer));
    text_layer_set_background_color(g_report_layer, GColorBlack);
    text_layer_set_text_color(g_report_layer, GColorWhite);
    text_layer_set_font(g_report_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
    text_layer_set_text_alignment(g_report_layer, GTextAlignmentLeft);
    text_layer_set_overflow_mode(g_report_layer, GTextOverflowModeWordWrap);

    g_calendar_layer = layer_create(GRect(bounds.size.w/2+1,
                                          CALENDAR_DATA_Y,
                                          bounds.size.w/2-2,
                                          CALENDAR_DATA_HEIGHT));
    layer_set_update_proc(g_calendar_layer, &on_calendar_layer_update);
    layer_add_child(window_layer, g_calendar_layer);
    
    g_battery_layer = layer_create(GRect(1, 1, 10, 17));
    layer_set_update_proc(g_battery_layer, &on_battery_layer_update);
    layer_add_child(window_layer, g_battery_layer);

    g_connection_layer = layer_create(GRect(15, 3, 7, 13));
    layer_set_update_proc(g_connection_layer, &on_connection_layer_update);
    layer_add_child(window_layer, g_connection_layer);

    
    // Weather
    g_weather_icon_layer = layer_create(GRect(1, bounds.size.h-27, 25, 25));
    layer_set_update_proc(g_weather_icon_layer, &on_weather_icon_layer_update);
    layer_add_child(window_layer, g_weather_icon_layer);

    GRect weather_temp_frame = GRect(27, bounds.size.h-30, 54, 30);
    g_weather_temp_layer = layer_create(weather_temp_frame);
    layer_set_update_proc(g_weather_temp_layer, &on_weather_temp_layer_update);
    layer_add_child(window_layer, g_weather_temp_layer);

    GRect weather_day_graph_frame = GRect(weather_temp_frame.origin.x+weather_temp_frame.size.w+1,
                                          bounds.size.h-27,
                                          WEATHER_DAY_GRAPH_SAMPLES+2,
                                          27);
    g_weather_day_graph_layer = layer_create(weather_day_graph_frame);
    layer_set_update_proc(g_weather_day_graph_layer, &on_weather_day_graph_layer_update);
    layer_add_child(window_layer, g_weather_day_graph_layer);

    int weather_precipprob_width = 16;
    GRect weather_precipgraph_frame = GRect(bounds.size.w-50, bounds.size.h-27, 49, 27);
    g_show_short_precipgraph =
        weather_day_graph_frame.origin.x+weather_day_graph_frame.size.w+1+
        weather_precipprob_width+1+weather_precipgraph_frame.size.w <= bounds.size.w;
    int weather_precipprob_x = weather_precipgraph_frame.origin.x-weather_precipprob_width-1;
    g_weather_precipprob_layer = layer_create(GRect(weather_precipprob_x, bounds.size.h-30, weather_precipprob_width, 30));
    layer_set_update_proc(g_weather_precipprob_layer, &on_weather_precipprob_layer_update);
    if (g_show_short_precipgraph) {
        layer_add_child(window_layer, g_weather_precipprob_layer);
    }
    
    g_weather_precipgraph_layer = layer_create(weather_precipgraph_frame);
    layer_set_update_proc(g_weather_precipgraph_layer, &on_weather_precipgraph_layer_update);
    if (g_show_short_precipgraph) {
        layer_add_child(window_layer, g_weather_precipgraph_layer);
    }

    GRect weather_detail_frame = GRect(weather_precipprob_x, bounds.size.h-30,
                                       weather_precipprob_width+1+weather_precipgraph_frame.size.w,
                                       30);
    g_weather_detail_layer = layer_create(weather_detail_frame);
    layer_set_update_proc(g_weather_detail_layer, &on_weather_detail_layer_update);
    layer_add_child(window_layer, g_weather_detail_layer);

    g_weather_humidity_layer = text_layer_create(GRect(1, bounds.size.h-42, 30, 14));
    layer_add_child(window_layer, text_layer_get_layer(g_weather_humidity_layer));
    text_layer_set_background_color(g_weather_humidity_layer, GColorBlack);
    text_layer_set_text_color(g_weather_humidity_layer, GColorWhite);
    text_layer_set_font(g_weather_humidity_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));

    g_weather_wind_layer = text_layer_create(GRect(31, bounds.size.h-42, 40, 14));
    layer_add_child(window_layer, text_layer_get_layer(g_weather_wind_layer));
    text_layer_set_background_color(g_weather_wind_layer, GColorBlack);
    text_layer_set_text_color(g_weather_wind_layer, GColorWhite);
    text_layer_set_font(g_weather_wind_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));

    
    // Health
    g_health_bpm_graph_layer = layer_create(GRect(1, bounds.size.h-43-40-22, 34, 22));
    layer_set_update_proc(g_health_bpm_graph_layer, &on_health_bpm_graph_layer_update);
    layer_add_child(window_layer, g_health_bpm_graph_layer);

    g_health_bpm_heart_layer = layer_create(GRect(1, bounds.size.h-43-40, 9, 14));
    layer_set_update_proc(g_health_bpm_heart_layer, &on_health_bpm_heart_layer_update);
    layer_add_child(window_layer, g_health_bpm_heart_layer);

    g_health_bpm_text_layer = text_layer_create(GRect(10, bounds.size.h-43-40, 23, 14));
    layer_add_child(window_layer, text_layer_get_layer(g_health_bpm_text_layer));
    text_layer_set_background_color(g_health_bpm_text_layer, GColorBlack);
    text_layer_set_text_color(g_health_bpm_text_layer, PBL_IF_COLOR_ELSE(GColorRed, GColorWhite));
    text_layer_set_font(g_health_bpm_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
    
    g_health_meters_text_layer = text_layer_create(GRect(33, bounds.size.h-43-40, 38, 14));
    layer_add_child(window_layer, text_layer_get_layer(g_health_meters_text_layer));
    text_layer_set_background_color(g_health_meters_text_layer, GColorBlack);
    text_layer_set_text_color(g_health_meters_text_layer, GColorWhite);
    text_layer_set_font(g_health_meters_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
    
    g_health_sleep_text_layer = text_layer_create(GRect(1, bounds.size.h-29-40, 70, 14));
    layer_add_child(window_layer, text_layer_get_layer(g_health_sleep_text_layer));
    text_layer_set_background_color(g_health_sleep_text_layer, GColorBlack);
    text_layer_set_text_color(g_health_sleep_text_layer, GColorWhite);
    text_layer_set_overflow_mode(g_health_sleep_text_layer, GTextOverflowModeWordWrap);
    text_layer_set_font(g_health_sleep_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));

    g_health_cals_text_layer = text_layer_create(GRect(1, bounds.size.h-15-40, 80, 14));
    //layer_add_child(window_layer, text_layer_get_layer(g_health_cals_text_layer)); // TODO calories counted incorrectly
    text_layer_set_background_color(g_health_cals_text_layer, GColorBlack);
    text_layer_set_text_color(g_health_cals_text_layer, GColorWhite);
    text_layer_set_font(g_health_cals_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));

    time_t now = time(NULL);
    g_local_time = *localtime(&now);
    on_tick_timer(&g_local_time, MINUTE_UNIT);
    tick_timer_service_subscribe(MINUTE_UNIT, &on_tick_timer);
  
    battery_state_service_subscribe(&on_battery_state);
    on_battery_state(battery_state_service_peek());
  
    health_service_events_subscribe(&on_health, NULL);
    on_health(HealthEventHeartRateUpdate, NULL);

    connection_service_subscribe((ConnectionHandlers) {.pebble_app_connection_handler = on_connection});
    on_connection(connection_service_peek_pebble_app_connection());
  
    accel_tap_service_subscribe(on_tap);  

    for (int i=0; i<WEATHER_DAY_GRAPH_SAMPLES; i++) {
        g_weather_day_atemp_array[i] = WEATHER_DAY_GRAPH_UNKNOWN;
        g_weather_day_precip_array[i] = WEATHER_DAY_GRAPH_UNKNOWN;
    }
  
    Tuplet initial_values[] = {
        TupletInteger(WEATHER_ICON_KEY, (uint8_t)0),
        TupletInteger(WEATHER_ATEMPERATURE_KEY, WEATHER_TEMP_UNKNOWN),
        TupletInteger(WEATHER_ATEMPERATUREMAX_KEY, WEATHER_TEMP_UNKNOWN),
        TupletInteger(WEATHER_ATEMPERATUREMIN_KEY, WEATHER_TEMP_UNKNOWN),
        TupletInteger(WEATHER_TEMPERATURE_KEY, WEATHER_TEMP_UNKNOWN),
        TupletInteger(WEATHER_TEMPERATUREMAX_KEY, WEATHER_TEMP_UNKNOWN),
        TupletInteger(WEATHER_TEMPERATUREMIN_KEY, WEATHER_TEMP_UNKNOWN),
        TupletInteger(WEATHER_PRECIP_PROB_KEY, (uint8_t)0),
        TupletBytes(WEATHER_PRECIP_ARRAY_KEY, g_weather_precip_array, sizeof(g_weather_precip_array)),
        TupletInteger(WEATHER_HUMIDITY_KEY, (uint8_t)101),
        TupletInteger(WEATHER_WIND_SPEED_KEY, (uint16_t)1001),
        TupletCString(REPORT_KEY, ""),
        TupletBytes(WEATHER_DAY_ATEMP_ARRAY_KEY, g_weather_day_atemp_array, sizeof(g_weather_day_atemp_array)),
        TupletBytes(WEATHER_DAY_PRECIP_ARRAY_KEY, g_weather_day_precip_array, sizeof(g_weather_day_precip_array)),
        TupletInteger(WEATHER_UV_INDEX_KEY, (uint8_t)WEATHER_DETAIL_UNKNOWN),
        TupletInteger(WEATHER_CLOUD_COVER_KEY, (uint8_t)WEATHER_PERCENT_UNKNOWN),
        TupletInteger(WEATHER_VISIBILITY_KEY, (uint8_t)WEATHER_DETAIL_UNKNOWN),
        TupletCString(CALENDAR_KEY, ""),
        TupletBytes(CALENDAR_COLORS_KEY, g_calendar_color_array, sizeof(g_calendar_color_array))
    };

    app_sync_init(&g_sync, g_sync_buffer, sizeof(g_sync_buffer),
                  initial_values, ARRAY_LENGTH(initial_values),
                  on_sync_tuple_change, on_sync_error, NULL);
    app_message_open(MESSAGE_BUF, MESSAGE_BUF);
}

static void deinit() {
    tick_timer_service_unsubscribe();
    battery_state_service_unsubscribe();
    health_service_events_unsubscribe();
    connection_service_unsubscribe();
    //accel_tap_service_unsubscribe();
    text_layer_destroy(g_time_layer);
    text_layer_destroy(g_date_layer);
    layer_destroy(g_battery_layer);
    layer_destroy(g_connection_layer);
    layer_destroy(g_health_bpm_graph_layer);
    layer_destroy(g_weather_temp_layer);
    layer_destroy(g_weather_icon_layer);
    layer_destroy(g_weather_precipprob_layer);
    layer_destroy(g_weather_precipgraph_layer);
    layer_destroy(g_weather_detail_layer);
    layer_destroy(g_weather_day_graph_layer);
    text_layer_destroy(g_weather_humidity_layer);
    text_layer_destroy(g_weather_wind_layer);
    text_layer_destroy(g_health_cals_text_layer);
    text_layer_destroy(g_health_meters_text_layer);
    text_layer_destroy(g_health_sleep_text_layer);
    layer_destroy(g_health_bpm_heart_layer);
    text_layer_destroy(g_health_bpm_text_layer);
    text_layer_destroy(g_report_layer);
    layer_destroy(g_calendar_layer);
    window_destroy(g_window);
    app_sync_deinit(&g_sync);
}

// --------------------------------------------------------------------------
// The main event loop.
// --------------------------------------------------------------------------

int main() {
    init();
    app_event_loop();
    deinit();
}
