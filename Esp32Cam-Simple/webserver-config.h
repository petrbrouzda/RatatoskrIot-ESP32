#ifndef __CONFIG___H_
#define __CONFIG___H_

// Konfigurace wifi AP - tj. konfigurační wifi, na kterou se budete případně připojovat mobilkou.
// Za jméno EspCam_ se příkazem .addChipIdToApHostname() doplní ID čipu, tj. EspCam_123456.
#define AP_SSID "EspCam_"
#define AP_PASSWORD "password"

// Pokud váš mobil nedělá automaticky přesměrování, zadejte do prohlížeče http://192.168.59.1
IPAddress local_ip(192,168,59,1);
IPAddress gateway(192,168,59,1);
IPAddress subnet(255,255,255,0);

#define WEB_PORT 80 

// na jakou adresu ma presmerovavat captive portal?
#define CAPTIVE_PORTAL_REDIRECT_REL "/"

#endif