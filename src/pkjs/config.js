module.exports = [
  {
    "type": "heading",
    "defaultValue": "Watch Configuration"
  },
  {
    "type": "text",
    "defaultValue": "You need to provide API keys and HTTP addresses."
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "OpenWeather"
      },
      {
        "type": "input",
        "messageKey": "OpenWeatherKey",
        "defaultValue": "",
        "label": "API Key"
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Report Source"
      },
      {
        "type": "input",
        "messageKey": "ReportSource",
        "defaultValue": "",
        "label": "HTTP address"
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Calendars"
      },
      {
        "type": "input",
        "messageKey": "CalendarUrls",
        "defaultValue": "",
        "label": "ICS URLs"
      },
      {
        "type": "input",
        "messageKey": "CalendarColors",
        "defaultValue": "",
        "label": "Colors"
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save Settings"
  }
];
