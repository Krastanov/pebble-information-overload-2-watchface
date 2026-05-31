var OpenWeatherKey = localStorage.getItem("OpenWeatherKey");
var ReportSource = localStorage.getItem("ReportSource");

var Clay = require('pebble-clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

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

function roundValue(value, fallback) {
  return typeof value === 'number' && isFinite(value) ? Math.round(value) : fallback;
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
            var pending = 3;
            var hasWeatherData = false;
            var json = {};
            var query = "?lat="+pos.coords.latitude+"&lon="+pos.coords.longitude+"&units=metric&appid="+encodeURIComponent(OpenWeatherKey);
            var baseUrl = "https://api.openweathermap.org/data/4.0/onecall/";

            function put(key, value) {
              json[key] = value;
              hasWeatherData = true;
            }

            function finishRequest() {
              pending -= 1;
              if (pending === 0 && hasWeatherData) {
                Pebble.sendAppMessage(json);
              }
            }

            requestJson(baseUrl+"current"+query, function (response){
              var current = firstData(response);
              var weather = current && current.weather && current.weather.length ? current.weather[0] : null;
              if (current) {
                put(0, openWeatherIconToId(weather));                 // id - check the c source
                put(1, roundValue(current.feels_like, 101));          // Celsius
                put(4, roundValue(current.temp, 101));                // Celsius
                put(9, roundValue(current.humidity, 101));            // Percents
                put(10, roundValue(current.wind_speed*10, 1001));     // dm/s
              }
              finishRequest();
            });

            requestJson(baseUrl+"timeline/1day"+query, function (response){
              var day = firstData(response);
              var apparentTemps = [];
              if (day && day.feels_like) {
                ["day", "night", "eve", "morn"].forEach(function (key){
                  if (typeof day.feels_like[key] === 'number' && isFinite(day.feels_like[key])) {
                    apparentTemps.push(day.feels_like[key]);
                  }
                });
              }
              if (day) {
                put(2, apparentTemps.length ? Math.round(Math.max.apply(null, apparentTemps)) : 101); // Celsius
                put(3, apparentTemps.length ? Math.round(Math.min.apply(null, apparentTemps)) : 101); // Celsius
                put(5, day.temp ? roundValue(day.temp.max, 101) : 101);                              // Celsius
                put(6, day.temp ? roundValue(day.temp.min, 101) : 101);                              // Celsius
                put(7, roundValue(day.pop*100, 0));                                                   // Percents
              }
              finishRequest();
            });

            requestJson(baseUrl+"timeline/1min"+query, function (response){
              if (response && response.data) {
                var minuteData = [];
                for (var i=0; i<60; i++) {
                  var minute = response.data[i];
                  var precipitation = minute && typeof minute.precipitation === 'number' ? minute.precipitation : 0;
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
