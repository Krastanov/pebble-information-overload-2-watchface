#include <pebble.h>
#include <pebble-fctx/fctx.h>
#include <pebble-fctx/fpath.h>
#include <pebble-fctx/ffont.h>

// message buffer size:
#define MESSAGE_BUF 512

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
static TextLayer* g_health_bpm_text_layer;    // Layer updated on heart beat events.
static Layer* g_health_bpm_graph_layer;       // Layer updated on heart beat events or on minute ticks.
static Layer* g_weather_temp_layer;           // Layer updated on weather events from PebbleKit messages.
static Layer* g_weather_icon_layer;           // Layer updated on weather events from PebbleKit messages.
static Layer* g_weather_precipprob_layer;     // Layer updated on weather events from PebbleKit messages.
static Layer* g_weather_precipgraph_layer;    // Layer updated on weather events from PebbleKit messages or on minute ticks.
static TextLayer* g_weather_humidity_layer;   // Layer updated on weather events from PebbleKit messages.
static TextLayer* g_weather_wind_layer;       // Layer updated on weather events from PebbleKit messages.
static TextLayer* g_my_message_layer;         // A reminder about 2016.
static TextLayer* g_report_layer;             // General purpose report layer - text generated in javascript.
static struct tm g_local_time;
static uint8_t g_battery_level;
static int8_t g_connected; // TODO Should be bool!
static int8_t g_atemp = 101; // TODO Use some more meaningful way to specify "unknown", not just setting it to 101!
static int8_t g_atempmax = 101;
static int8_t g_atempmin = 101;
static int8_t g_temp = 101;
static int8_t g_tempmax = 101;
static int8_t g_tempmin = 101;
static uint8_t g_precipprob;
static uint8_t g_weather_icon; // TODO Use less obfuscated data type!
static uint8_t g_weather_precip_array[60];
static uint8_t g_ticks_since_weather_array_update;
static char g_report_string[100];
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
  REPORT_KEY = 0xB
};

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
    time_t t2 = time(NULL);
    time_t t1 = t2 - SECONDS_PER_HOUR;
    // TODO Why not health_service_get_minute_history(minute_data, sizeof(minute_data), &t1, &t2));
    health_service_get_minute_history(&minute_data[0], 60, &t1, &t2);
    graphics_context_set_fill_color(ctx, GColorDarkGray);
    graphics_fill_rect(ctx, GRect(1, 11, 33, 1), 0, GCornerNone);
    graphics_context_set_stroke_color(ctx, GColorWhite);
    int last_y = 20;
    for (int i=0; i<60; i++) {
        int y;
        if (minute_data[i].is_invalid || minute_data[i].heart_rate_bpm == 0) {
            y = last_y;
        } else {
            y = min(20, max(1, 20-(minute_data[i].heart_rate_bpm-50)*20/100));
            last_y = y;
        }
        if (i>=30) { // We are plotting the last 30 minutes, but reading the last 60 minutes is a cheap way ensure we are not starting with a bad datapoint.
            graphics_draw_line(ctx, GPoint(i+2-30, y), GPoint(i+2-30, 20));
        }
    }
    graphics_draw_rect(ctx, GRect(0,0,34,22));
    graphics_fill_rect(ctx, GRect(16, 1, 1, 20), 0, GCornerNone);
}

static void on_weather_temp_layer_update(Layer* layer, GContext* ctx) {
    char temp_string[4];
    if (g_temp < 101) {
        snprintf(temp_string, sizeof temp_string, "%d", g_temp);
        graphics_draw_text(ctx, temp_string,
                           fonts_get_system_font(FONT_KEY_GOTHIC_14),
                           GRect(0,7,18,15), GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
        snprintf(temp_string, sizeof temp_string, "%d", g_tempmax);
        graphics_draw_text(ctx, temp_string,
                           fonts_get_system_font(FONT_KEY_GOTHIC_14),
                           GRect(16-((g_tempmax<0)?5:0),0,18,15), GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
        snprintf(temp_string, sizeof temp_string, "%d", g_tempmin);
        graphics_draw_text(ctx, temp_string,
                           fonts_get_system_font(FONT_KEY_GOTHIC_14),
                           GRect(16-((g_tempmin<0)?5:0),14,18,15), GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
        snprintf(temp_string, sizeof temp_string, "%d", g_atemp);
        graphics_draw_text(ctx, temp_string,
                           fonts_get_system_font(FONT_KEY_GOTHIC_14),
                           GRect(20,7,18,15), GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
        snprintf(temp_string, sizeof temp_string, "%d", g_atempmax);
        graphics_draw_text(ctx, temp_string,
                           fonts_get_system_font(FONT_KEY_GOTHIC_14),
                           GRect(38-((g_tempmax<0)?5:0),0,18,15), GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
        snprintf(temp_string, sizeof temp_string, "%d", g_atempmin);
        graphics_draw_text(ctx, temp_string,
                           fonts_get_system_font(FONT_KEY_GOTHIC_14),
                           GRect(38-((g_tempmin<0)?5:0),14,18,15), GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
    }
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
        graphics_context_set_compositing_mode(ctx, GCompOpSet);
        graphics_draw_bitmap_in_rect(ctx, s_bitmap, GRect(0,0,25,25));
    }
}

static void on_weather_precipprob_layer_update(Layer* layer, GContext* ctx) { // TODO make text layer with wrapping.
    char percent_string[4];
    if (g_precipprob > 0) {
        snprintf(percent_string, sizeof percent_string, "%d", g_precipprob);
        graphics_draw_text(ctx, percent_string,
                           fonts_get_system_font(FONT_KEY_GOTHIC_14),
                           GRect(0,2,20,15), GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
        graphics_draw_text(ctx, "%",
                           fonts_get_system_font(FONT_KEY_GOTHIC_14),
                           GRect(0,13,20,15), GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
    }
}

static void on_weather_precipgraph_layer_update(Layer* layer, GContext* ctx) {
    graphics_context_set_stroke_color(ctx, GColorWhite);
    int count = 0;
    int i;
    for (i=0; i<45; i++) {
        int i_off = g_ticks_since_weather_array_update+i;      
        if (i_off < 60) {
            if (g_weather_precip_array[i_off] > 0) {
                count += 1;
                graphics_draw_line(ctx, GPoint(i+2, 25-g_weather_precip_array[i_off]/10), GPoint(i+2, 25));
            }
        } else {
            break;
        }
    }
    if (count > 0) {
        graphics_draw_rect(ctx, GRect(0,0,49,27));
        graphics_context_set_fill_color(ctx, GColorDarkGray);
        graphics_fill_rect(ctx, GRect(17, 1, 1, 25), 0, GCornerNone);
        graphics_fill_rect(ctx, GRect(32, 1, 1, 25), 0, GCornerNone);
        if (g_ticks_since_weather_array_update>15) {
            graphics_fill_rect(ctx, GRect(i+1, 23, 46-i, 2), 0, GCornerNone);
        }
    }
}

// --------------------------------------------------------------------------
// System event handlers.
// --------------------------------------------------------------------------

static void on_tick_timer(struct tm* tick_time, TimeUnits units_changed) {
    g_local_time = *tick_time;
    g_ticks_since_weather_array_update += 1;
    static char time_string[6];
    static char date_string[7];
    strftime(time_string, sizeof time_string, "%H:%M", &g_local_time);
    text_layer_set_text(g_time_layer, time_string);
    strftime(date_string, sizeof date_string, "%b %d", &g_local_time);
    text_layer_set_text(g_date_layer, date_string);
    layer_mark_dirty(g_health_bpm_graph_layer);
    layer_mark_dirty(g_weather_precipgraph_layer);
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
    snprintf(bpm_string, sizeof bpm_string, "\U00002764%d", (int)health_service_peek_current_value(HealthMetricHeartRateBPM));
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
            break;
        case WEATHER_PRECIP_ARRAY_KEY:
            if (new_tuple->length) for (int i=0; i<new_tuple->length; i++) {g_weather_precip_array[i] = new_tuple->value->data[i];}
            else for (int i=0; i<new_tuple->length; i++) {g_weather_precip_array[i] = 0;} // TODO something more elegant than implicit "zero meaning no data"
            g_ticks_since_weather_array_update = 0;
            layer_mark_dirty(g_weather_precipgraph_layer);
            break;
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
            strncpy(g_report_string, new_tuple->value->cstring, min(sizeof(g_report_string), new_tuple->length));
            text_layer_set_text(g_report_layer, g_report_string);
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

    g_time_layer = text_layer_create(GRect(bounds.size.w-104, bounds.size.h-43-40-32, 102, 32));
    layer_add_child(window_layer, text_layer_get_layer(g_time_layer));
    text_layer_set_background_color(g_time_layer, GColorBlack);
    text_layer_set_text_color(g_time_layer, GColorWhite);
    text_layer_set_font(g_time_layer, fonts_get_system_font(FONT_KEY_LECO_32_BOLD_NUMBERS));
    text_layer_set_text_alignment(g_time_layer, GTextAlignmentRight);

    g_date_layer = text_layer_create(GRect(bounds.size.w-70, bounds.size.h-43-40, 66, 30));
    layer_add_child(window_layer, text_layer_get_layer(g_date_layer));
    text_layer_set_background_color(g_date_layer, GColorBlack);
    text_layer_set_text_color(g_date_layer, GColorWhite);
    text_layer_set_font(g_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
    text_layer_set_text_alignment(g_date_layer, GTextAlignmentRight);

    g_report_layer = text_layer_create(GRect(1, 17, bounds.size.w-2, 3*14));
    layer_add_child(window_layer, text_layer_get_layer(g_report_layer));
    text_layer_set_background_color(g_report_layer, GColorBlack);
    text_layer_set_text_color(g_report_layer, GColorWhite);
    text_layer_set_font(g_report_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
    text_layer_set_text_alignment(g_report_layer, GTextAlignmentLeft);
    
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

    g_weather_temp_layer = layer_create(GRect(27, bounds.size.h-30, 54, 30));
    layer_set_update_proc(g_weather_temp_layer, &on_weather_temp_layer_update);
    layer_add_child(window_layer, g_weather_temp_layer);

    g_weather_precipprob_layer = layer_create(GRect(71, bounds.size.h-30, 20, 30));
    layer_set_update_proc(g_weather_precipprob_layer, &on_weather_precipprob_layer_update);
    layer_add_child(window_layer, g_weather_precipprob_layer);
    
    g_weather_precipgraph_layer = layer_create(GRect(bounds.size.w-51, bounds.size.h-27, 49, 27));
    layer_set_update_proc(g_weather_precipgraph_layer, &on_weather_precipgraph_layer_update);
    layer_add_child(window_layer, g_weather_precipgraph_layer);

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

    g_health_bpm_text_layer = text_layer_create(GRect(1, bounds.size.h-43-40, 28, 14));
    layer_add_child(window_layer, text_layer_get_layer(g_health_bpm_text_layer));
    text_layer_set_background_color(g_health_bpm_text_layer, GColorBlack);
    text_layer_set_text_color(g_health_bpm_text_layer, GColorWhite);
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

    
    // Message    
    g_my_message_layer = text_layer_create(GRect(37, 0, bounds.size.w-37, 14));
    layer_add_child(window_layer, text_layer_get_layer(g_my_message_layer));
    text_layer_set_background_color(g_my_message_layer, GColorBlack);
    text_layer_set_text_color(g_my_message_layer, GColorWhite);
    text_layer_set_font(g_my_message_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
    text_layer_set_text_alignment(g_my_message_layer, GTextAlignmentRight);
    text_layer_set_text(g_my_message_layer, "This is not normal!");

    
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
  
    Tuplet initial_values[] = {
        TupletInteger(WEATHER_ICON_KEY, (uint8_t)0),
        TupletInteger(WEATHER_ATEMPERATURE_KEY, (int8_t)101),
        TupletInteger(WEATHER_ATEMPERATUREMAX_KEY, (int8_t)101),
        TupletInteger(WEATHER_ATEMPERATUREMIN_KEY, (int8_t)101),
        TupletInteger(WEATHER_TEMPERATURE_KEY, (int8_t)101),
        TupletInteger(WEATHER_TEMPERATUREMAX_KEY, (int8_t)101),
        TupletInteger(WEATHER_TEMPERATUREMIN_KEY, (int8_t)101),
        TupletInteger(WEATHER_PRECIP_PROB_KEY, (uint8_t)0),
        TupletBytes(WEATHER_PRECIP_ARRAY_KEY, g_weather_precip_array, sizeof(g_weather_precip_array)),
        TupletInteger(WEATHER_HUMIDITY_KEY, (uint8_t)101),
        TupletInteger(WEATHER_WIND_SPEED_KEY, (uint16_t)1001),
        TupletCString(REPORT_KEY, "")
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
    text_layer_destroy(g_weather_humidity_layer);
    text_layer_destroy(g_weather_wind_layer);
    text_layer_destroy(g_health_cals_text_layer);
    text_layer_destroy(g_health_meters_text_layer);
    text_layer_destroy(g_health_sleep_text_layer);
    text_layer_destroy(g_health_bpm_text_layer);
    text_layer_destroy(g_my_message_layer);
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