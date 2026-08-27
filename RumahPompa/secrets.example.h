// Credential template.
//
//   cp secrets.example.h secrets.h
//
// then fill in the values below. secrets.h is listed in .gitignore and must
// never be committed.

#ifndef SECRETS_H
#define SECRETS_H

// Blynk device credentials, from the Blynk console (Developer Zone ->
// My Templates, and Devices -> your device -> Device Info).
#define BLYNK_TEMPLATE_ID "TMPLxxxxxxxx"
#define BLYNK_TEMPLATE_NAME "Rumah pompa"
#define BLYNK_AUTH_TOKEN "your-blynk-auth-token"

// Blynk cloud region. sgp1 is Singapore; use the host shown in your console.
#define BLYNK_SERVER "sgp1.blynk.cloud"
#define BLYNK_PORT 80

// Wi-Fi network the controller joins.
#define WIFI_SSID "your-wifi-name"
#define WIFI_PASSWORD "your-wifi-password"

#endif  // SECRETS_H
