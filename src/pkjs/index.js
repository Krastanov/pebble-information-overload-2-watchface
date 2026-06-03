var OpenWeatherKey = localStorage.getItem("OpenWeatherKey");
var ReportSource = localStorage.getItem("ReportSource");
var CalendarUrls = localStorage.getItem("CalendarUrls");
var CalendarColors = localStorage.getItem("CalendarColors");

var Clay = require('pebble-clay');
var ICAL = require('ical.js');
var clayConfig = require('./config');
var clay = new Clay(clayConfig, null, { autoHandleEvents: false });
var WEATHER_TEMP_UNKNOWN = -128;
var REPORT_KEY = 11;
var CALENDAR_KEY = 17;
var CALENDAR_COLORS_KEY = 18;
var REPORT_TEXT_MAX_LENGTH = 219;
var CALENDAR_TEXT_MAX_LENGTH = 255;
var CALENDAR_MAX_EVENTS = 8;
var CALENDAR_ROW_MAX_LENGTH = 28;
var CALENDAR_LOOKAHEAD_DAYS = 90;
var CALENDAR_RECURRENCE_SCAN_LIMIT = 5000;

Pebble.addEventListener('showConfiguration', function(e) {
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (e && !e.response) {return;}
  var json_resp = JSON.parse(e.response);
  OpenWeatherKey = json_resp.OpenWeatherKey.value;
  localStorage.setItem("OpenWeatherKey", OpenWeatherKey);
  ReportSource = json_resp.ReportSource.value;
  localStorage.setItem("ReportSource", ReportSource);
  CalendarUrls = json_resp.CalendarUrls.value;
  localStorage.setItem("CalendarUrls", CalendarUrls);
  CalendarColors = json_resp.CalendarColors.value;
  localStorage.setItem("CalendarColors", CalendarColors);
  sendCalendar();
});

var iconNameToId = {
  '01d': 1,
  '01n': 2,
  '09d': 3,
  '09n': 3,
  '10d': 3,
  '10n': 3,
  '11d': 3,
  '11n': 3,
  '13d': 4,
  '13n': 4,
  '50d': 7,
  '50n': 7,
  '03d': 8,
  '03n': 8,
  '04d': 8,
  '04n': 8,
  '02d': 9,
  '02n': 10
};

function isFiniteNumber(value) {
  return typeof value === 'number' && isFinite(value);
}

function roundValue(value, fallback) {
  return isFiniteNumber(value) ? Math.round(value) : fallback;
}

function roundBoundedValue(value, fallback, minValue, maxValue) {
  if (!isFiniteNumber(value)) {return fallback;}
  return Math.max(minValue, Math.min(maxValue, Math.round(value)));
}

function roundTemperature(value) {
  return roundValue(value, WEATHER_TEMP_UNKNOWN);
}

function collectFiniteValues(source, keys) {
  var values = [];
  if (!source) {return values;}
  keys.forEach(function (key){
    if (isFiniteNumber(source[key])) {
      values.push(source[key]);
    }
  });
  return values;
}

function buildDailyTemperatureBounds(day) {
  var apparentTemps = collectFiniteValues(day && day.feels_like, ["day", "night", "eve", "morn"]);
  return {
    atempMax: apparentTemps.length ? Math.max.apply(null, apparentTemps) : null,
    atempMin: apparentTemps.length ? Math.min.apply(null, apparentTemps) : null,
    tempMax: day && day.temp && isFiniteNumber(day.temp.max) ? day.temp.max : null,
    tempMin: day && day.temp && isFiniteNumber(day.temp.min) ? day.temp.min : null
  };
}

function buildHourlyTemperatureBounds(hourlyData) {
  var actualTemps = [];
  var apparentTemps = [];
  hourlyData = hourlyData || [];
  for (var i=0; i<hourlyData.length; i++) {
    if (isFiniteNumber(hourlyData[i] && hourlyData[i].temp)) {
      actualTemps.push(hourlyData[i].temp);
    }
    if (isFiniteNumber(hourlyData[i] && hourlyData[i].feels_like)) {
      apparentTemps.push(hourlyData[i].feels_like);
    }
  }
  return {
    atempMax: apparentTemps.length ? Math.max.apply(null, apparentTemps) : null,
    atempMin: apparentTemps.length ? Math.min.apply(null, apparentTemps) : null,
    tempMax: actualTemps.length ? Math.max.apply(null, actualTemps) : null,
    tempMin: actualTemps.length ? Math.min.apply(null, actualTemps) : null
  };
}

function temperatureOrFallback(primary, fallback) {
  if (isFiniteNumber(primary)) {return roundTemperature(primary);}
  if (isFiniteNumber(fallback)) {return roundTemperature(fallback);}
  return WEATHER_TEMP_UNKNOWN;
}

function openWeatherIconToId(weather) {
  if (!weather) {return 0;}
  if (weather.id === 611 || weather.id === 612 || weather.id === 613) {return 5;}
  if (weather.id >= 200 && weather.id < 600) {return 3;}
  if (weather.id >= 600 && weather.id < 700) {return 4;}
  if (weather.id >= 700 && weather.id < 800) {return 7;}
  return iconNameToId[weather.icon] || 0;
}

function firstData(response) {
  return response && response.data && response.data.length ? response.data[0] : null;
}

function maxPopPercent(response) {
  var data = response && response.data ? response.data : [];
  var maxPop = null;
  for (var i=0; i<data.length; i++) {
    var pop = data[i] && data[i].pop;
    if (isFiniteNumber(pop)) {
      maxPop = maxPop === null ? pop : Math.max(maxPop, pop);
    }
  }
  return maxPop === null ? null : Math.round(maxPop*100);
}

function interpolateNumber(a, b, fraction) {
  var hasA = isFiniteNumber(a);
  var hasB = isFiniteNumber(b);
  if (hasA && hasB) {return a+(b-a)*fraction;}
  if (hasA) {return a;}
  if (hasB) {return b;}
  return null;
}

function encodeGraphTemp(value) {
  if (!isFiniteNumber(value)) {return 255;}
  return Math.max(0, Math.min(254, Math.round(value)+100));
}

function encodeGraphPrecipProbability(value) {
  if (!isFiniteNumber(value)) {return 255;}
  return Math.max(0, Math.min(100, Math.round(value*100)));
}

function buildDayGraphData(hourlyData) {
  var temps = [];
  var precip = [];
  hourlyData = hourlyData || [];
  for (var i=0; i<48; i++) {
    var hourIndex = Math.floor(i/2);
    var fraction = (i%2) ? 0.5 : 0;
    var current = hourlyData[hourIndex] || null;
    var next = hourlyData[hourIndex+1] || current;
    temps.push(encodeGraphTemp(interpolateNumber(current && current.feels_like,
                                                 next && next.feels_like,
                                                 fraction)));
    precip.push(encodeGraphPrecipProbability(interpolateNumber(current && current.pop,
                                                               next && next.pop,
                                                               fraction)));
  }
  return {temps: temps, precip: precip};
}

function truncateText(value, maxLength) {
  value = value || "";
  if (value.length <= maxLength) {return value;}
  return value.substring(0, maxLength);
}

function cleanSingleLineText(value) {
  value = value || "";
  value = value.replace(/\s+/g, " ");
  return value.replace(/^\s+|\s+$/g, "");
}

function splitCalendarUrls(value) {
  var urls = [];
  var parts = (value || "").split(",");
  for (var i=0; i<parts.length; i++) {
    var url = parts[i].replace(/^\s+|\s+$/g, "");
    if (url) {
      urls.push(url);
    }
  }
  return urls;
}

function calendarColorId(value) {
  value = cleanSingleLineText(value).toLowerCase();
  if (value === "green") {return 1;}
  if (value === "blue") {return 2;}
  if (value === "red") {return 3;}
  if (value === "yellow") {return 4;}
  if (value === "cyan") {return 5;}
  if (value === "magenta") {return 6;}
  if (value === "orange") {return 7;}
  if (value === "purple") {return 8;}
  return 0;
}

function parseCalendarColors(value) {
  var colorIds = [];
  var parts = (value || "").split(",");
  for (var i=0; i<parts.length; i++) {
    colorIds.push(calendarColorId(parts[i]));
  }
  return colorIds;
}

function padTwo(value) {
  return value < 10 ? "0" + value : "" + value;
}

function formatCalendarTime(time) {
  if (!time) {return "--:--";}
  if (time.isDate) {return "All day";}

  var date = time.toJSDate();
  return padTwo(date.getHours()) + ":" + padTwo(date.getMinutes());
}

function calendarTimeToUnix(time) {
  return Math.floor(time.toJSDate().getTime()/1000);
}

function addCalendarOccurrence(events, item, startDate, endDate, nowUnix, limitUnix, colorId) {
  if (!startDate) {return false;}

  var startUnix = calendarTimeToUnix(startDate);
  var endUnix = endDate ? calendarTimeToUnix(endDate) : startUnix;
  if (endUnix < nowUnix || startUnix > limitUnix) {return false;}

  events.push({
    startUnix: startUnix,
    startDate: startDate,
    summary: cleanSingleLineText(item && item.summary) || "(untitled)",
    colorId: colorId || 0
  });
  return true;
}

function collectRecurringCalendarEvents(events, event, nowUnix, limitUnix, colorId) {
  var iterator = event.iterator();
  var occurrence = null;
  var scanCount = 0;
  var eventCount = 0;

  while ((occurrence = iterator.next()) && scanCount < CALENDAR_RECURRENCE_SCAN_LIMIT) {
    scanCount += 1;

    var details = event.getOccurrenceDetails(occurrence);
    if (!details || !details.startDate) {continue;}

    var startUnix = calendarTimeToUnix(details.startDate);
    if (startUnix > limitUnix) {break;}

    if (addCalendarOccurrence(events, details.item || event, details.startDate,
                              details.endDate,
                              nowUnix, limitUnix, colorId)) {
      eventCount += 1;
    }
    if (eventCount >= CALENDAR_MAX_EVENTS) {break;}
  }
}

function buildUpcomingCalendarEvents(icsText, now, colorId) {
  var calendar = new ICAL.Component(ICAL.parse(icsText));
  var components = calendar.getAllSubcomponents("vevent");
  var nowUnix = Math.floor(now.getTime()/1000);
  var limitUnix = nowUnix + CALENDAR_LOOKAHEAD_DAYS*24*60*60;
  var events = [];

  for (var i=0; i<components.length; i++) {
    try {
      var event = new ICAL.Event(components[i]);
      if (event.isRecurrenceException()) {continue;}
      if (!event.startDate) {continue;}

      if (event.isRecurring()) {
        collectRecurringCalendarEvents(events, event, nowUnix, limitUnix, colorId);
      } else {
        addCalendarOccurrence(events, event, event.startDate, event.endDate,
                              nowUnix, limitUnix, colorId);
      }
    } catch (e) {
      console.log("Calendar event parse failed: " + e.message);
    }
  }

  events.sort(function(a, b) {
    return a.startUnix - b.startUnix;
  });

  return events.slice(0, CALENDAR_MAX_EVENTS);
}

function buildCalendarMessage(events) {
  var rows = [];
  for (var i=0; i<events.length; i++) {
    rows.push(truncateText(formatCalendarTime(events[i].startDate) + " " +
                           events[i].summary, CALENDAR_ROW_MAX_LENGTH));
  }
  return truncateText(rows.join("\n"), CALENDAR_TEXT_MAX_LENGTH);
}

function buildCalendarColorData(events) {
  var colors = [];
  for (var i=0; i<events.length; i++) {
    colors.push(events[i].colorId || 0);
  }
  return colors;
}

function requestJson(url, done) {
  var req = new XMLHttpRequest();
  req.addEventListener("load", function (){
    if (req.status && (req.status < 200 || req.status >= 300)) {
      console.log("OpenWeather request failed: " + req.status);
      done(null);
      return;
    }
    var response = req.response;
    if (!response && req.responseText) {
      try {
        response = JSON.parse(req.responseText);
      } catch (e) {
        console.log("OpenWeather response was not valid JSON.");
      }
    }
    done(response);
  });
  req.addEventListener("error", function (){
    console.log("OpenWeather request failed.");
    done(null);
  });
  req.responseType = 'json';
  req.open("GET", url);
  req.send();
}

function sendWeather() {
    if (OpenWeatherKey) {
        console.log("Setting up getCurrentPosition.");
        navigator.geolocation.getCurrentPosition(
        function (pos){
            console.log("Got position, setting up OpenWeather requests.");
            var pending = 4;
            var hasWeatherData = false;
            var json = {};
            var dailyTemperatureBounds = null;
            var hourlyTemperatureBounds = null;
            var query = "?lat="+pos.coords.latitude+"&lon="+pos.coords.longitude+"&units=metric&appid="+encodeURIComponent(OpenWeatherKey);
            var baseUrl = "https://api.openweathermap.org/data/4.0/onecall/";

            function put(key, value) {
              json[key] = value;
              hasWeatherData = true;
            }

            function finishRequest() {
              pending -= 1;
              if (pending === 0 && hasWeatherData) {
                dailyTemperatureBounds = dailyTemperatureBounds || {};
                hourlyTemperatureBounds = hourlyTemperatureBounds || {};
                put(2, temperatureOrFallback(dailyTemperatureBounds.atempMax, hourlyTemperatureBounds.atempMax)); // Celsius
                put(3, temperatureOrFallback(dailyTemperatureBounds.atempMin, hourlyTemperatureBounds.atempMin)); // Celsius
                put(5, temperatureOrFallback(dailyTemperatureBounds.tempMax, hourlyTemperatureBounds.tempMax));   // Celsius
                put(6, temperatureOrFallback(dailyTemperatureBounds.tempMin, hourlyTemperatureBounds.tempMin));   // Celsius
                Pebble.sendAppMessage(json);
              }
            }

            requestJson(baseUrl+"current"+query, function (response){
              var current = firstData(response);
              var weather = current && current.weather && current.weather.length ? current.weather[0] : null;
              if (current) {
                put(0, openWeatherIconToId(weather));                 // id - check the c source
                put(1, roundTemperature(current.feels_like));          // Celsius
                put(4, roundTemperature(current.temp));                // Celsius
                put(9, roundValue(current.humidity, 101));            // Percents
                put(10, roundValue(current.wind_speed*10, 1001));     // dm/s
                put(14, roundBoundedValue(current.uvi, 255, 0, 254)); // UV index
                put(15, roundBoundedValue(current.clouds, 101, 0, 100)); // Percents
                put(16, roundBoundedValue(current.visibility/1000, 255, 0, 254)); // Kilometers
              }
              finishRequest();
            });

            requestJson(baseUrl+"timeline/1day"+query, function (response){
              var day = firstData(response);
              if (day) {
                dailyTemperatureBounds = buildDailyTemperatureBounds(day);
                hasWeatherData = true;
              }
              finishRequest();
            });

            requestJson(baseUrl+"timeline/1h"+query, function (response){
              var hourlyData = response && response.data ? response.data.slice(0) : [];

              function finishHourlyTimeline(data) {
                if (data.length) {
                  var precipProb = maxPopPercent({data: data.slice(0, 25)});
                  var graphData = buildDayGraphData(data);
                  hourlyTemperatureBounds = buildHourlyTemperatureBounds(data.slice(0, 25));
                  if (precipProb !== null) {
                    put(7, precipProb);                                                              // Percents
                  }
                  put(12, graphData.temps);                                                          // Apparent temp, Celsius + 100
                  put(13, graphData.precip);                                                         // Precipitation probability
                }
                finishRequest();
              }

              var lastHour = hourlyData.length ? hourlyData[hourlyData.length-1] : null;
              if (hourlyData.length > 0 && hourlyData.length < 25 && lastHour && lastHour.dt) {
                requestJson(baseUrl+"timeline/1h"+query+"&start="+(lastHour.dt+3600), function (nextResponse){
                  if (nextResponse && nextResponse.data) {
                    hourlyData = hourlyData.concat(nextResponse.data);
                  }
                  finishHourlyTimeline(hourlyData);
                });
              } else {
                finishHourlyTimeline(hourlyData);
              }
            });

            requestJson(baseUrl+"timeline/1min"+query, function (response){
              if (response && response.data) {
                var minuteData = [];
                for (var i=0; i<60; i++) {
                  var minute = response.data[i];
                  var precipitation = minute && isFiniteNumber(minute.precipitation) ? minute.precipitation : 0;
                  minuteData.push(Math.min(Math.round(precipitation/10*255), 255)); // mm/h scaled to a byte
                }
                put(8, minuteData);
              }
              finishRequest();
            });
        },
        function (err) {
            console.log("Error with getCurrentPosition.");
            if(err.code == err.PERMISSION_DENIED) console.log('Location access was denied by the user.');  
            else console.log('location error (' + err.code + '): ' + err.message);
        },
        {
            enableHighAccuracy: false,
            maximumAge: 1000*60*10,
            timeout: 10000
        }
        );
        console.log("getCurrentPosition setup complete.");
    }
}

function sendReport() {
    if (ReportSource!==null) {
        var req = new XMLHttpRequest();
        req.addEventListener("load", function (){
            if (req.status && (req.status < 200 || req.status >= 300)) {
                console.log("Report request failed: " + req.status);
                return;
            }
            // TODO Fix the message key issue and use descriptive keys!
            var json = {};
            json[REPORT_KEY] = truncateText(req.responseText || req.response || "",
                                            REPORT_TEXT_MAX_LENGTH);
            Pebble.sendAppMessage(json);
        });
        req.addEventListener("error", function (){
            console.log("Report request failed.");
        });
        req.open("GET", ReportSource);
        req.send();
    }
}

function sendCalendar() {
    var urls = splitCalendarUrls(CalendarUrls);
    if (!urls.length) {return;}

    var calendarColorIds = parseCalendarColors(CalendarColors);
    var pending = urls.length;
    var allEvents = [];
    var sawCalendarData = false;
    var now = new Date();

    function finishCalendarRequest() {
        pending -= 1;
        if (pending !== 0) {return;}

        if (sawCalendarData) {
            allEvents.sort(function(a, b) {
                return a.startUnix - b.startUnix;
            });
            var visibleEvents = allEvents.slice(0, CALENDAR_MAX_EVENTS);
            var json = {};
            json[CALENDAR_KEY] = buildCalendarMessage(visibleEvents);
            json[CALENDAR_COLORS_KEY] = buildCalendarColorData(visibleEvents);
            Pebble.sendAppMessage(json);
        }
    }

    for (var i=0; i<urls.length; i++) {
        (function (url, colorId){
            var req = new XMLHttpRequest();
            req.addEventListener("load", function (){
                if (req.status && (req.status < 200 || req.status >= 300)) {
                    console.log("Calendar request failed: " + req.status + " " + url);
                    finishCalendarRequest();
                    return;
                }

                try {
                    allEvents = allEvents.concat(
                        buildUpcomingCalendarEvents(req.responseText || req.response || "",
                                                    now, colorId)
                    );
                    sawCalendarData = true;
                } catch (e) {
                    console.log("Calendar parse failed: " + e.message + " " + url);
                }
                finishCalendarRequest();
            });
            req.addEventListener("error", function (){
                console.log("Calendar request failed: " + url);
                finishCalendarRequest();
            });
            req.open("GET", url);
            req.send();
        })(urls[i], calendarColorIds[i] || 0);
    }
}

Pebble.addEventListener("ready", function() {sendWeather(); sendReport(); sendCalendar();});

setInterval(function(){sendWeather(); sendReport(); sendCalendar();}, 30*60*1000);
