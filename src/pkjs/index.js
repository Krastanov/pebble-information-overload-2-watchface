var OpenWeatherKey = localStorage.getItem("OpenWeatherKey");
var ReportSource = localStorage.getItem("ReportSource");

var Clay = require('pebble-clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig, null, { autoHandleEvents: false });
var WEATHER_TEMP_UNKNOWN = -128;

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
            // TODO Fix the message key issue and use descriptive keys!
            var json = {11:req.response.substring(0,99)};
            Pebble.sendAppMessage(json);
        });
        req.open("GET", ReportSource);
        req.send();
    }
}

Pebble.addEventListener("ready", function() {sendWeather(); sendReport();});

setInterval(function(){sendWeather(); sendReport();}, 30*60*1000);
