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
        "defaultValue": "Darksky"
      },
      {
        "type": "input",
        "messageKey": "DarkskyKey",
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
    "type": "submit",
    "defaultValue": "Save Settings"
  }
];
