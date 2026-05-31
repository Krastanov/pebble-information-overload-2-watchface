# AGENTS.md

## Project Summary

This repository contains `InfoOverload2`, a Pebble SDK 3 native watchface for
the `diorite` target platform. It is intentionally dense: the screen combines
status indicators, a short fetched report, time/date, health metrics, and local
weather.

The project is split between watch-side C and phone-side PebbleKit JavaScript:

- Watch-side C in `src/c/watchface.c` owns the Pebble window, layers, drawing,
  health/time/battery/connection event subscriptions, and AppMessage/AppSync
  receiving.
- Phone-side JavaScript in `src/pkjs/index.js` runs inside the Pebble mobile
  app on the connected phone. It handles configuration, persistent settings,
  GPS, HTTP requests, and sends normalized data to the watch.
- Phone-side Clay configuration in `src/pkjs/config.js` defines the settings
  webview fields used by the JavaScript.
- `package.json` is the Pebble manifest: app metadata, SDK version, watchface
  flag, target platform, permissions/capabilities, dependencies, resources,
  and declared message keys.
- `wscript` is the Pebble build script. It compiles all `src/c/**/*.c` files
  and bundles `src/pkjs/**/*.js` and `src/pkjs/**/*.json` with
  `src/pkjs/index.js` as the JavaScript entry point.

There are no automated tests in this repository. The practical validation path
is to build and run the watchface in a Pebble SDK environment or emulator.

## Documentation

Primary Pebble documentation:

- C SDK contents: <https://developer.repebble.com/docs/c/>
- PebbleKit JS: <https://developer.repebble.com/docs/pebblekit-js/>
- AppMessage, the phone-watch key/value messaging layer:
  <https://developer.repebble.com/docs/c/Foundation/AppMessage/>
- AppSync, the C-side helper used by this project to keep incoming message
  tuples available for UI updates:
  <https://developer.repebble.com/docs/c/Foundation/AppSync/>

Useful C SDK sections for this codebase:

- Graphics: <https://developer.repebble.com/docs/c/Graphics/>
- Layers: <https://developer.repebble.com/docs/c/User_Interface/Layers/>
- TextLayer:
  <https://developer.repebble.com/docs/c/User_Interface/Layers/TextLayer/>
- Window: <https://developer.repebble.com/docs/c/User_Interface/Window/>
- TickTimerService:
  <https://developer.repebble.com/docs/c/Foundation/Event_Service/TickTimerService/>
- BatteryStateService:
  <https://developer.repebble.com/docs/c/Foundation/Event_Service/BatteryStateService/>
- ConnectionService:
  <https://developer.repebble.com/docs/c/Foundation/Event_Service/ConnectionService/>
- HealthService:
  <https://developer.repebble.com/docs/c/Foundation/Event_Service/HealthService/>
- Resources: <https://developer.repebble.com/docs/c/Foundation/Resources/>

Use the C SDK docs for anything compiled into `src/c/watchface.c`. Use the
PebbleKit JS docs for the `Pebble` namespace, configuration events,
`Pebble.sendAppMessage`, `Pebble.openURL`, `localStorage`, geolocation, and
`XMLHttpRequest` usage in `src/pkjs`.

The PebbleKit JS docs are explicit that this JavaScript runs in the Pebble
mobile application on the phone, not on the watch. Do not put watch rendering,
watch service subscriptions, or direct layer logic in JavaScript. Do not put
network requests or GPS lookup in C for this project; the current architecture
uses the phone for those capabilities.

## Repository Layout

- `src/c/watchface.c`: complete watchface implementation.
- `src/pkjs/index.js`: PebbleKit JS runtime entry point.
- `src/pkjs/config.js`: Clay configuration schema for settings.
- `resources/images/*.png`: 25x25 weather bitmap resources.
- `package.json`: Pebble app manifest and npm dependency list.
- `wscript`: Pebble SDK build rules.
- `README.md`: short human-facing project description and screenshots.
- `screenshot1.png`, `screenshot2.png`: watchface screenshots.

## Build And Run

Typical Pebble SDK commands from the repository root:

```sh
pebble build
pebble install --emulator diorite
```

For a physical watch, use the normal Pebble CLI install flow for the configured
phone/watch environment. This repository targets `diorite`; the UI uses many
hardcoded positions and should be checked carefully before changing target
platforms.

## Runtime Data Flow

1. The C app starts in `main()`, calls `init()`, pushes a watchface window, and
   enters `app_event_loop()`.
2. `init()` creates all watch layers, subscribes to time, battery, health, and
   connection services, initializes `AppSync`, and opens AppMessage with
   `MESSAGE_BUF` bytes for both inbox and outbox.
3. The Pebble mobile app starts `src/pkjs/index.js` on the connected phone.
4. On PebbleKit JS `ready`, and again every 30 minutes, JavaScript fetches
   weather and report data if settings are present.
5. JavaScript sends compact AppMessage dictionaries to the watch using numeric
   keys.
6. Watch-side `on_sync_tuple_change()` receives changed tuples, updates global
   state and/or TextLayer text, and marks custom drawing layers dirty.
7. Watch-side layer update callbacks redraw the visible UI.

## What Runs On The Watch

All watch-side behavior is in `src/c/watchface.c`.

The watch owns:

- Window creation and teardown.
- TextLayer and custom Layer creation.
- Drawing battery, connection, weather, precipitation, and heart-rate graph
  elements.
- Time/date formatting from local watch time.
- Health data reads through `HealthService`.
- Battery and Bluetooth/app connection state reads.
- AppMessage/AppSync receive buffers and tuple change handling.
- Mapping numeric weather icon ids to compiled bitmap resources.

The watch does not fetch weather, read remote report URLs, store settings, or
display the Clay configuration webview. It receives already-normalized values
from the phone.

### Main Watch-Side Sections

`Types and global variables`

- Layer pointers and state fields are declared near the top of
  `src/c/watchface.c`.
- `MESSAGE_BUF` is currently 512 bytes and is used for both AppSync backing
  storage and AppMessage inbox/outbox size.
- Unknown weather temperatures use sentinel value `101`.
- `g_weather_precip_array` stores up to 60 minutes of precipitation intensity
  data received from the phone.
- `g_report_string` stores the phone-fetched report text.

`enum CommKey`

- Defines the watch-side numeric AppMessage contract from key `0x0` through
  `0xB`.
- Keep this enum synchronized with the numeric keys in `src/pkjs/index.js`.
- `package.json` currently declares message key names only through
  `WEATHER_PRECIP_ARRAY_KEY`; the running code still uses numeric JS keys for
  humidity, wind, and report. If moving to named keys, update the manifest,
  JavaScript, and C together.

`Drawing functions`

- `on_battery_layer_update()` draws a vertical battery outline and fill.
- `on_connection_layer_update()` draws the connection indicator.
- `on_health_bpm_graph_layer_update()` reads recent health minute data and
  draws a compact heart-rate history.
- `on_weather_temp_layer_update()` draws current, high, and low temperature
  values for actual and apparent temperature.
- `on_weather_icon_layer_update()` maps the weather icon id to a bitmap
  resource and draws it.
- `on_weather_precipprob_layer_update()` draws precipitation probability.
- `on_weather_precipgraph_layer_update()` draws the short-term precipitation
  graph, shifted forward by minute ticks since the last weather update.

`System event handlers`

- `on_tick_timer()` updates time/date every minute, advances the precipitation
  graph offset, and refreshes the heart-rate and precipitation graph layers.
- `on_battery_state()` stores battery percent and redraws the battery layer.
- `on_connection()` stores current Pebble app connection state and redraws the
  connection layer.
- `on_health()` dispatches heart-rate, movement, sleep, and significant health
  updates.
- `on_tap()` currently only logs taps.
- `on_sync_error()` logs AppSync/AppMessage errors.
- `on_sync_tuple_change()` is the main incoming-data dispatcher from the phone.

`Initialization and teardown`

- `init()` constructs the full layout, subscribes services, initializes initial
  AppSync tuple values, and opens AppMessage.
- `deinit()` unsubscribes services, destroys layers/windows, and deinitializes
  AppSync.
- Check both functions when adding or removing any Pebble resource.

## What Runs On The Phone

All phone-side runtime behavior is in `src/pkjs/index.js`.

The JavaScript runs inside the Pebble mobile app on the connected phone. It has
access to phone-side facilities exposed by PebbleKit JS, including storage,
configuration events, geolocation, HTTP requests, and `Pebble.sendAppMessage`.

The phone owns:

- Opening the Clay settings page.
- Saving configuration values in `localStorage`.
- Reading the phone's location via `navigator.geolocation`.
- Fetching weather JSON over HTTP.
- Fetching a general report URL over HTTP.
- Translating weather icon names into compact numeric ids for the watch.
- Trimming the report text to fit the watch-side buffer.
- Sending AppMessage dictionaries to the watch.

### Main Phone-Side Sections

Configuration storage

- At startup, `DarkskyKey` and `ReportSource` are loaded from `localStorage`.
- `showConfiguration` opens the Clay-generated settings URL.
- `webviewclosed` parses the Clay response and persists both settings.

Weather fetch

- `sendWeather()` exits unless `DarkskyKey` is present.
- It calls `navigator.geolocation.getCurrentPosition()` with low accuracy,
  a 10 minute maximum cached age, and a 10 second timeout.
- It requests the Dark Sky forecast endpoint with SI units.
- It converts the returned JSON into numeric AppMessage fields:
  icon id, apparent temperature, actual temperature, daily highs/lows,
  precipitation probability, minute precipitation array, humidity, and wind.
- It sends those fields with `Pebble.sendAppMessage(json)`.

Report fetch

- `sendReport()` exits unless `ReportSource` is present.
- It performs an HTTP GET to the configured URL.
- It sends the first 99 response characters to key `11`.

Scheduling

- Both weather and report are sent once on PebbleKit JS `ready`.
- Both are refreshed every 30 minutes with `setInterval()`.

Clay settings

- `src/pkjs/config.js` defines two input fields:
  `DarkskyKey` and `ReportSource`.
- These are configuration values for the phone-side JavaScript. They are not
  AppMessage keys used directly by the C watchface.

## Main Semantic Pieces Of The Watchface

Top status strip

- Battery icon at the upper left.
- Connection indicator beside it.
- Static reminder message `"This is not normal!"` aligned at the upper right.

Remote report area

- Three compact text rows below the top strip.
- The phone fetches arbitrary text from `ReportSource`.
- The watch displays the trimmed text in `g_report_layer`.

Time and date

- Large time on the right.
- Date below the time.
- Updated by the watch once per minute from local watch time.

Health area

- Heart-rate text.
- Heart-rate mini graph for recent data.
- Walked distance in kilometers.
- Sleep text showing restful percentage and total hours.
- A calories TextLayer exists but is not added to the window because the code
  comments say calories are counted incorrectly.

Weather area

- Bottom-left weather icon.
- Actual temperature and apparent temperature, each with current, max, and min.
- Humidity and wind text above the bottom row.
- Precipitation probability.
- Short-term precipitation graph at the lower right.

Phone-watch communication layer

- Phone-side JS produces compact data.
- Watch-side C receives via AppSync and updates display state.
- This is the boundary between network/configuration logic and watch rendering.

Assets and resources

- Weather icons live in `resources/images`.
- `package.json` registers each bitmap resource and the Pebble build generates
  `RESOURCE_ID_*` constants used by C.
- When adding a new icon, add the image file, add it to `package.json`, update
  the JS `iconNameToId` map, and update the C switch in
  `on_weather_icon_layer_update()`.

## AppMessage Contract

The code currently uses numeric AppMessage keys directly. Treat this table as
the source of truth when changing either side.

| Key | C enum | Producer | Value | Watch behavior |
| --- | --- | --- | --- | --- |
| `0` / `0x0` | `WEATHER_ICON_KEY` | JS weather fetch | `uint8` icon id from `1` to `10` | Selects a bitmap resource and redraws weather icon |
| `1` / `0x1` | `WEATHER_ATEMPERATURE_KEY` | JS weather fetch | Current apparent temperature, Celsius, rounded | Redraws temperature layer |
| `2` / `0x2` | `WEATHER_ATEMPERATUREMAX_KEY` | JS weather fetch | Daily max apparent temperature, Celsius, rounded | Redraws temperature layer |
| `3` / `0x3` | `WEATHER_ATEMPERATUREMIN_KEY` | JS weather fetch | Daily min apparent temperature, Celsius, rounded | Redraws temperature layer |
| `4` / `0x4` | `WEATHER_TEMPERATURE_KEY` | JS weather fetch | Current temperature, Celsius, rounded | Redraws temperature layer |
| `5` / `0x5` | `WEATHER_TEMPERATUREMAX_KEY` | JS weather fetch | Daily max temperature, Celsius, rounded | Redraws temperature layer |
| `6` / `0x6` | `WEATHER_TEMPERATUREMIN_KEY` | JS weather fetch | Daily min temperature, Celsius, rounded | Redraws temperature layer |
| `7` / `0x7` | `WEATHER_PRECIP_PROB_KEY` | JS weather fetch | Daily precipitation probability as percent | Redraws precipitation probability layer |
| `8` / `0x8` | `WEATHER_PRECIP_ARRAY_KEY` | JS weather fetch | Up to 60 bytes of minute precipitation intensity | Resets weather age counter and redraws precipitation graph |
| `9` / `0x9` | `WEATHER_HUMIDITY_KEY` | JS weather fetch | Current humidity as percent | Updates humidity TextLayer |
| `10` / `0xA` | `WEATHER_WIND_SPEED_KEY` | JS weather fetch | Wind speed multiplied by 10 | Displays integer meters per second after dividing by 10 |
| `11` / `0xB` | `REPORT_KEY` | JS report fetch | C string, first 99 response characters | Updates report TextLayer |

Notes:

- AppSync accepts only keys present in the `initial_values` array passed to
  `app_sync_init()`. Add new keys there before expecting the watch to receive
  them.
- If a message grows, revisit `MESSAGE_BUF` and the AppMessage buffer sizes in
  `app_message_open()`.
- AppMessage is asynchronous. Use Pebble logs when debugging failed sends or
  dropped messages.

## Dependencies

- `pebble-clay`: used by `src/pkjs/index.js` and `src/pkjs/config.js` to build
  the phone-side configuration webview.
- `pebble-fctx`: included in `package.json` and included by `watchface.c`.
  The current visible drawing code mainly uses standard Pebble graphics APIs.

## Maintenance Guidance

- Keep phone-side fetch/normalization in `src/pkjs/index.js` and watch-side
  rendering/state in `src/c/watchface.c`.
- Keep the AppMessage key contract synchronized across C, JavaScript, and the
  manifest if you move away from numeric keys.
- Avoid allocating persistent objects inside layer update callbacks unless the
  teardown path is clear. Pebble layer update callbacks can run often.
- Any layer created in `init()` should have a matching destroy or cleanup path
  in `deinit()`.
- Any service subscription in `init()` should have a matching unsubscribe in
  `deinit()`.
- The UI is densely packed and mostly absolute-positioned. After layout edits,
  verify on the `diorite` screen size and check overlap around the bottom
  weather/health rows.
- The code contains existing TODOs around sentinel values, message key naming,
  memory cleanup, and calorie display. Preserve those concerns when refactoring.
