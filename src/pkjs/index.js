var DarkskyKey = localStorage.getItem("DarkskyKey");
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
  DarkskyKey = json_resp.DarkskyKey.value;
  localStorage.setItem("DarkskyKey", DarkskyKey);
  ReportSource = json_resp.ReportSource.value;
  localStorage.setItem("ReportSource", ReportSource);
});

var iconNameToId = {
  'clear-day': 1,
  'clear-night': 2,
  'rain': 3,
  'snow': 4,
  'sleet': 5,
  'wind': 6,
  'fog': 7,
  'cloudy': 8,
  'partly-cloudy-day': 9,
  'partly-cloudy-night': 10
};

function sendWeather() {
    if (DarkskyKey!==null) {
        console.log("Setting up getCurrentPosition.");
        navigator.geolocation.getCurrentPosition(
        function (pos){
            console.log("Got position, setting up weather request.");
            var req = new XMLHttpRequest();
            req.addEventListener("load", function (){
              // TODO Fix the message key issue and use descriptive keys!
              var minuteData = req.response.minutely ? req.response.minutely.data.map(function (el){return Math.min(Math.round(el.precipIntensity/10*255), 255);}).slice(0,60) : minuteData = [];
              var json = {0:iconNameToId[req.response.currently.icon],                         // id - check the c source
                          1:Math.round(req.response.currently.apparentTemperature),            // Celsius
                          2:Math.round(req.response.daily.data[0].apparentTemperatureMax),     // Celsius
                          3:Math.round(req.response.daily.data[0].apparentTemperatureMin),     // Celsius
                          4:Math.round(req.response.currently.temperature),            // Celsius
                          5:Math.round(req.response.daily.data[0].temperatureMax),     // Celsius
                          6:Math.round(req.response.daily.data[0].temperatureMin),     // Celsius
                          7:Math.round(req.response.daily.data[0].precipProbability*100),      // Percents
                          8:minuteData, // cm/h scaled to a byte, >7.6 mm/h is the definition of heavy rain
                          9:Math.round(req.response.currently.humidity*100), // Percents
                         10:Math.round(req.response.currently.windSpeed*10)  // dm/s
                         };
              Pebble.sendAppMessage(json);
            });
            req.responseType = 'json';
            console.log("https://api.darksky.net/forecast/"+DarkskyKey+"/"+pos.coords.latitude+","+pos.coords.longitude+"?units=si");
            req.open("GET", "https://api.darksky.net/forecast/"+DarkskyKey+"/"+pos.coords.latitude+","+pos.coords.longitude+"?units=si");
            req.send();
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