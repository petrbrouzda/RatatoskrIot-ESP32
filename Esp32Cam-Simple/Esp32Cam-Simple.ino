/**
 * Základní obsluha kamery a periodické posílání fotky do galerie
 * na serveru https://ratatoskr.lovecka.info/
 */


// konfigurace:

// 1) (POZOR!) typ desky zvolte v board_config.h

// 2) zde zvolte jakou kameru máte - nejčastější je CAMERA_OV2640 (1600x1200 px)
#define TYP_KAMERY CAMERA_OV2640
// #define TYP_KAMERY CAMERA_OV3660
// (číselník typů je zde: https://github.com/espressif/esp32-camera/blob/master/driver/include/sensor.h )

// 3) defaultní připojení wifi, token pro server atd. jsou zde:
#include "defaultcfg.h"

// 4) konfiguraci Wifi AP pro lokální připojení k ESP32 najdete ve "webserver-config.h"




/* AsyncLogger se pouziva pro logovani udalosti v asynchronnich aktivitach (webserver...).
Uklada zaznamy do pole v pameti a ty se pak vypisou v loop() pomoci volani dumpTo(); */
#include "src/logging/AsyncLogger.h"
AsyncLogger asyncLogger;

/* SerialLogger vypisuje přímo na sériový port*/
#include "src/logging/SerialLogger.h"
SerialLogger serialLogger( &Serial );

/*
Pokud nechcete nelogovat vubec nic, pak všude, kde se má předávat LoggerInterface, 
předejte buď NULL nebo lépe pointer na instanci NullLogger.
*/


/** Sdileny stav aplikace - objekt drzici napr. chybu inicializace kamery, aby se dala vypsat uživateli ve webu */
#include "src/toolkit/AppState.h"
AppState appState( &serialLogger );


/*
Jednoducha obsluha wifi.
*/
#include "src/net/WifiRunner.h"
WifiRunner wifirunner( &serialLogger );


/*
Posílání fotek do galerie.
*/
#include "src/net/ImageSender.h"
ImageSender imageSender( &serialLogger );


/*
WebServer pro možnost administrace zařízení.
Přes objekt EasyWebServer se používají tyto dvě knihovny:
- https://github.com/ESP32Async/ESPAsyncWebServer
- https://github.com/ESP32Async/AsyncTCP 

V kódu volaném z webserveru je NUTNÉ používat asyncLogger, aby se akce v http callbacích nezdržovaly a nedocházelo ke kolizím na výstupu.
*/
#include "src/net/EasyWebServer.h"
#include "webserver-config.h"
EasyWebServer webserver( WEB_PORT, &asyncLogger );



/*
Konfigurace.
Je nutny alespon kousek filesystemu SPIFFS.
*/
#include "src/toolkit/BasicConfig.h"
#include "src/toolkit/ConfigProviderSpiffs.h"
// nástroj pro držení konfigurace
BasicConfig config;
// načítání a ukládání konfigurace na SPIFFS; může dostat serialLogger, protože poběží synchronně v loopu
ConfigProviderSpiffs configProvider( &config, &appState, &serialLogger );

/*
Periodicke ulohy
https://github.com/joysfera/arduino-tasker
*/
#define TASKER_MAX_TASKS 32
#include <Tasker.h>
Tasker tasker;


// funkce formatDeviceInfo() a výpis stavu paměti
#include "src/toolkit/DeviceInfo.h"
MemoryHelper memoryHelper( &serialLogger );


/* 
Kamera je obsluhovana pres cameraHelper 
*/
#include "CameraHelper.h"
CameraHelper cameraHelper( &serialLogger, &config, &appState );






/** každou sekundu si vezmeme fotku, aby bylo co ukazovat ve webserveru */
long cameraSnapshotIntervalMs = 1000;

/** každých 120 sekund se pošle fotka na server */
long cameraSendPictureEveryMs = 120000;

/** prvni snimek se posle 30 sekund po startu (aby auto-expozice už nabrala dobré nastavení) */
long firstShotMs = 30000;





void setup() {
  Serial.begin(115200);
  delay(3000);

  // načtení konfigurace z SPIFFS
  configProvider.openFsAndLoadConfig();

  // spuštění kamery - je nutné říct, jaká to je
  cameraHelper.cameraInit( TYP_KAMERY );
  cameraHelper.setCameraParameters();

  // kam se ma pripojit - defaulty jsou v defaultcfg.h
  imageSender.setConfig( 
        config.getString( "ra_url", SERVER_URL_DEFAULT ), 
        config.getString( "ra_gallery_token", GALLERY_TOKEN_DEFAULT ) );

  // jméno Wifi klienta, které je vidět ve správě  sítě (setClientHostname) bude rozšířeno o ID čipu, třeba "ESP_154545464" 
  wifirunner.addChipIdToApHostname();

  const char * ssid = config.getString( "client_ssid", WIFI_STA_SSID );
  const char * pwd = config.getString( "client_pass", WIFI_STA_PASS );
  if( ssid[0]!=0 && pwd[0]!=0 ) {
    // pokud máme konfiguraci klienta, spustíme AP i klienta
    wifirunner.setClientConfig( ssid, pwd, (int)config.getLong("client_ch", 0) );
    wifirunner.setClientHostname( "esp_cam_", true );
    wifirunner.startApAndClient( AP_SSID, AP_PASSWORD, local_ip, gateway, subnet );
  } else {
    // jinak pustíme jen AP
    serialLogger.log( "Nemam konfiguraci Wifi klienta, spoustim jen AP" );
    wifirunner.startAp( AP_SSID, AP_PASSWORD, local_ip, gateway, subnet );
  }

  webserver.startWebserverOnApAndClient( CAPTIVE_PORTAL_REDIRECT_REL );

  // nastavime jako jednorazove spusteni - a v prubehu jejich behu si nastavi dalsi
  tasker.setTimeout( doCamera, cameraSnapshotIntervalMs );
  // prvni fotka po 30 sec, aby se kamera chytla
  tasker.setTimeout( sendImage, firstShotMs );

  // bude spousteno periodicky
  tasker.setInterval( doMemoryInfo, 60000 );

  doMemoryInfo();
}


/**
 * Callback z webserveru.
 * Zde nastavte routy pro svou aplikaci.
 * Je zavolano z webserveru v dobe volani webserver.startWebserverOnApAndClient()
 * 
 * Pokud chcete nektere sluzby obsluhovat jen na AP rozhrani, nebo jen na client rozhrani, pouzijte filtry
 *    ON_AP_FILTER
 *    ON_STA_FILTER
 * viz filterApOnly()
 */
void userRoutes( AsyncWebServer * server )
{
  server->on("/", HTTP_GET, onRequestRoot );
  server->on("/setwifi", HTTP_GET, onRequestSetWifi );
  server->on("/setwifiA", HTTP_GET, onRequestSetWifiAction );
  server->on("/setserver", HTTP_GET, onRequestSetServer );
  server->on("/setserverA", HTTP_GET, onRequestSetServerAction );
  server->on("/restartA", HTTP_GET, onRequestRestart );
  server->on("/image", HTTP_GET, onRequestImage );
  server->on("/setcamera", HTTP_GET, onRequestSetCamera );
  server->on("/setcamA", HTTP_GET, onRequestSetCameraA );
}

/**
 * Callback pro reporting WiFi.
 * Je zavoláno pokaždé, když Wifi přejde z nepřipojeno do připojeno.
 * Pokud se nepoužívá AP+STA nebo STA režim (je použito startAp), tak se nikdy nepoužijí; ale musí být definované.
 */
void WifiStatus_Connected( const char * ssid, int rssi, int channel ) {
  serialLogger.log( "Wifi connected" );
}


/**
 * Callback pro reporting WiFi.
 * Je zavoláno pokaždé, když Wifi přejde z připojeno do nepřipojeno.
 * Pokud se nepoužívá AP+STA nebo STA režim (je použito startAp), tak se nikdy nepoužijí; ale musí být definované.
 */
void WifiStatus_NotConnected( int status ) {
  serialLogger.log( "Wifi not connected (%d)", status );
}



// ----------- vlastni vykonna cast aplikace (loop)

/** 
 * Voláno z taskeru každých 60 sec.
 * Vypíše stav heapu a změnu odminule - moc příjemné pro hledání memory leaků.
 */
void doMemoryInfo() {
  memoryHelper.printFreeHeap();
}


int imagesSent = 0;
int imagesNotSent = 0;



/** 
 * Voláno z taskeru. Pořídí fotku.
 * Bere se 1 fotka za sekundu, 
 * aby kameře fungovalo automatické nastavení expozice (nebo když se uživatel připojí přes webserver),
 * prakticky se pak odešle jedna za dvě minuty, zbytek se nepoužije.
 */
void doCamera() {
    if( !cameraHelper.cameraOK ) {
      serialLogger.log( "camera not OK" );
      return;
    }
    cameraHelper.capture();
    tasker.setTimeout( doCamera, cameraSnapshotIntervalMs );
}




/** volano z taskeru kazde 2 min */
void sendImage()
{
  if( !wifirunner.isClientConnected() ) {
    imagesNotSent++;
    serialLogger.log("(nemam wifi, neposilam)" );
    return;
  }
  
  if( !cameraHelper.hasImage() ) {
    serialLogger.log("(nemam fotku!)" );
    return;
  }

  serialLogger.log( "odesilam: %dx%d px, %d b, #%d", 
      cameraHelper.img.w, cameraHelper.img.h, 
      cameraHelper.img.size, cameraHelper.imagesTaken );

  if( imageSender.sendImage( cameraHelper.img.data, cameraHelper.img.size, (const char*)"camera" ) ) {
    imagesSent++;
  } else {
    imagesNotSent++;
  }

  tasker.setTimeout( sendImage, cameraSendPictureEveryMs );
}





void loop() {

  // vypiseme asynchronni log, pokud v nem neco je
  asyncLogger.dumpTo( &Serial );

  // aby Tasker spoustel ulohy
  tasker.loop();

  // odbavit wifi a DNS pozadavky
  wifirunner.process();
  webserver.process();

  // pokud se zmenila konfigurace, ulozit ji do souboru
  if( config.isDirty() ) {
    configProvider.saveConfig();
  }
}


/*
Následující dvě funkce se pouští z webserveru Taskerem - protože webserver nemůže z asynchronních požadavků
bezpečně sahat na wifi nebo kameru (rychlost, kolize na proměnných). Volání přes Tasker zajistí, že se to spustí
z hlavního threadu aplikace.
*/ 


/** 
 * voláno Taskerem, spuštění se nastavuje z OnRequestSetWifiAction - aby proces ve webserveru nesahal na wifi přímo
 */
void reparamWifi() {
  serialLogger.log( "prepinam nastaveni wifi clienta" );
  const char * ssid = config.getString( "client_ssid", WIFI_STA_SSID );
  const char * pwd = config.getString( "client_pass", WIFI_STA_PASS );
  if( ssid[0]!=0 && pwd[0]!=0 ) {
    // pokud máme konfiguraci klienta, zmenime ji
    wifirunner.setClientConfig( ssid, pwd, (int)config.getLong("client_ch", 0) );
    wifirunner.reconnectClient();
  }
}

/**
 * voláno taskerem poté, co uživatel změní nastavení přes webserver
 * (aby se na kameru sahalo jen z hlavního threadu)
 */
void reparamCamera()  {
  cameraHelper.setCameraParameters();
}




// -------------- odsud dal je webserver ------------------------------------

/*
 * Je lepe misto Serial.print pouzivat AsyncLogger.
 * Je volano z webserveru asynchronne.
 * Nevolat odsud dlouhotrvajici akce!
 * I logovani by melo byt pres asyncLogger!
 * 
 * Kazda funkce onRequest* musi byt zaregistrovana v userRoutes()
 */


const char htmlHlavicka[] PROGMEM = R"rawliteral(
  <!DOCTYPE HTML><html>
  <head>
    <title>Esp32Cam-Simple</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
      html {font-family: Arial; display: inline-block; text-align: left;}
      h2 {font-size: 1.8rem;}
      h3 {font-size: 1.45rem; font-weight: 600}
      p {font-size: 1.2rem;}
      input {font-size: 1.2rem;}
      input#text {width: 100%;}
      select {font-size: 1.2rem;}
      form {font-size: 1.2rem;}
      body {max-width: 600px; margin:10px ; padding-bottom: 25px;}
    </style>
  </head>
  <body>
)rawliteral";

const char htmlPaticka[] PROGMEM = R"rawliteral(
  </body>
  </html>
)rawliteral";


void vlozInformace( AsyncResponseStream *response )  {

  if( appState.isProblem() ) {
    response->printf( "<p><b>%s:</b> [%s] před %d sec.</p>",
      appState.globalState==ERROR ? "Chyba" : "Varování",
      appState.problemDesc,
      (millis()-appState.problemTime) / 1000L
    );  
  }

  response->print( "<p><small>" );
  if( wifirunner.isClientConnected() ) {
    response->printf( "Wifi '%s' připojeno %d min: %s, ch %d, %d dB.", 
      wifirunner.getClientSsid(),
      (millis() - wifirunner.lastConnectedTime) / 60000L,
      wifirunner.getClientIp(),
      WiFi.channel(),
      WiFi.RSSI()
    );
  } else {
    response->printf( "Wifi '%s' nepřipojeno již %d min.", 
      wifirunner.getClientSsid(),
      (millis() - wifirunner.lastDisconnectedTime) / 60000L
    ); 
  }

  response->printf( "<br>Čas od spuštění zařízení: %d min.",
    millis()/60000L
  );

  response->printf( "<br>Fotek nafoceno: %d, posláno: %d, neposláno: %d.",
    cameraHelper.imagesTaken,
    imagesSent,
    imagesNotSent
  );

  response->print( "</small></p>" );

}




/**
 * Pro požadavky přijaté z client wifi (tj. z veřejné sítě)
 * vrátí volajícímu chybu, zaloguje a vrátí false;
 * pro požadavky přes AP vrátí true.
 */
bool filterApOnly( AsyncWebServerRequest *request ) {
  if (ON_STA_FILTER(request)) {
      asyncLogger.log( "pozadavek z STA zahozen" );
      request->send(200, "text/plain", "Konfigurace se smi provadet jen pres Wifi AP.");
      return false;
  } else {
    return true;
  }
}







void onRequestRoot(AsyncWebServerRequest *request){
  asyncLogger.log( "@ req root" );

  AsyncResponseStream *response = request->beginResponseStream(webserver.HTML_UTF8);
  response->print( htmlHlavicka );
  
  response->print("<h1>Stav zařízení</h1>");
  response->print("<p><a href=\"/?\">Obnov stav</a></p>" );

  vlozInformace( response );

  cameraHelper.saveImage();
  if( cameraHelper.hasSavedImage() ) {
      response->printf( "<p>Poslední fotka (stará %d sec)<br>", (millis()-cameraHelper.savedImg.timeTaken)/1000 );
      response->printf( "<img src=\"/image?%d\" style=\"max-width: 100%%; height: auto;\"></p>", millis() );
  }

  response->print( "<p><a href=\"/setwifi\">Nastavit wifi klienta</a></p>" );
  response->print( "<p><a href=\"/setserver\">Nastavit připojení na server</a></p>" );
  response->print( "<p><a href=\"/setcamera\">Nastavení kamery</a></p>" );
  response->print( "<p><a href=\"/restartA\">Restart</a></p>" );
  response->print( htmlPaticka );

  request->send(response);
}



int wifiScan = 0;

/**
 * Výpis wifi sítí
 */
void onRequestSetWifi(AsyncWebServerRequest *request){
  asyncLogger.log( "@ req setwifi" );

  // tohle je možné spustit jen přes AP, ne z internetu
  if( !filterApOnly(request) ) return;

  AsyncResponseStream *response = request->beginResponseStream(webserver.HTML_UTF8);
  response->print( htmlHlavicka );

  response->print( "<h1>Konfigurace Wifi</h1>" );

  response->print( "<h2>Seznam sítí</h2>" );
  if( wifiScan==0 ) {
    response->print( "<p>Spuštěn sken wifi sítí, obnovte stránku pro načtení výsledků (cca 10 sekund).</p>");
    // asynchronni scan!
    WiFi.scanNetworks( true );
    wifiScan = 1;
    asyncLogger.log( "spusten scan wifi" );
  } else if( wifiScan==1 ) {
    int n = WiFi.scanComplete();
    if( n<0 ) {
      asyncLogger.log( "scan stale probiha" );
      response->print( "<p>Sken wifi sítí stále probíhá, obnovte stránku pro načtení výsledků. <small>(Pokud máte nastaveno Wifi na jiný kanál než 0, nemusí to nikdy doběhnout.)</small></p>");
    } else {
      asyncLogger.log( "mame vysledek %d siti", n );
      response->print("<p>");
      for (int i = 0; i < n; ++i) {
        // Print SSID and RSSI for each network found
        response->printf( "<small>%d: <a href=\"#s\" onclick=\"document.getElementById('v1').value = '%s';\" >%s</a>, ch %d, %d dBm (%s)</small><br>",
            i + 1,
            WiFi.SSID(i).c_str(),
            WiFi.SSID(i).c_str(),
            WiFi.channel(i),
            WiFi.RSSI(i),
            WiFi.BSSIDstr(i).c_str()
        );
      }
      response->print("</p>");
      wifiScan = 0;
    }
  }
  response->print( "<p><a href=\"/setwifi?\">Aktualizovat seznam sítí</a></p>" );

  response->print("<p><hr><h2>Nastavení Wifi</h2>");
  response->print( "<form action=\"/setwifiA\" method=\"GET\">");
  response->print( "Jméno sítě (SSID):<br>");
  response->printf( "<input type=\"text\" id=\"v1\" name=\"ssid\" value=\"%s\">",
                        config.getString( "client_ssid", "" ) );
  response->print( "<br>Heslo:<br>");
  response->print( "<input type=\"text\" name=\"pass\" value=\"\">");
  response->print( "<br>Kanál (0=automatické hledání; jiné číslo=fixní kanál):<br>");
  response->printf( "<input type=\"number\" name=\"ch\" value=\"%d\">",
                         config.getLong( "client_ch", 0 ) );  
  response->print( "<br><input type=\"submit\" name=\"send\" value=\"Zapiš\">");
  response->print( "</form>" );
    
  response->print( "<p><a href=\"/\">Zpět</a></p>" );

  response->print( htmlPaticka );
  request->send(response);
}


void onRequestSetWifiAction(AsyncWebServerRequest *request) {
  asyncLogger.log( "@ req setwifiA" );

  // tohle je možné spustit jen přes AP, ne z internetu
  if( !filterApOnly(request) ) return;

  config.setValue( "client_ssid", webserver.getQueryParamAsString( request, "ssid", "" ));
  config.setValue( "client_pass", webserver.getQueryParamAsString( request, "pass", "" ));
  config.setValue( "client_ch", webserver.getQueryParamAsString( request, "ch", "" ));

  tasker.setTimeout( reparamWifi, 1 );

  request->redirect("/");
}

void onRequestSetServer(AsyncWebServerRequest *request){
  asyncLogger.log( "@ req setserver" );

  // tohle je možné spustit jen přes AP, ne z internetu
  if( !filterApOnly(request) ) return;

  AsyncResponseStream *response = request->beginResponseStream(webserver.HTML_UTF8);
  response->print( htmlHlavicka );

  response->print( "<h1>Konfigurace připojení k serveru</h1>" );

  response->print( "<form action=\"/setserverA\" method=\"GET\">");
  response->print( "URL serveru:<br>");
  response->printf( "<input type=\"text\" name=\"url\" value=\"%s\">",
                        config.getString( "ra_url", SERVER_URL_DEFAULT ) );
  response->print( "<br>Zabezpečovací token pro galerii:<br>");
  response->print( "<input type=\"text\" name=\"token_g\" value=\"\">" );
  response->print( "<br><input type=\"submit\" name=\"send\" value=\"Zapiš\">");
  response->print( "</form>" );
    
  response->print( "<p><a href=\"/\">Zpět</a></p>" );

  response->print( htmlPaticka );
  request->send(response);
}


void onRequestSetServerAction(AsyncWebServerRequest *request) {
  asyncLogger.log( "@ req setserverA" );

  // tohle je možné spustit jen přes AP, ne z internetu
  if( !filterApOnly(request) ) return;

  const char * url = webserver.getQueryParamAsString( request, "url", "" );
  config.setValue( "ra_url", url );
  const char * token_g = webserver.getQueryParamAsString( request, "token_g", "" ); 
  if( token_g[0]!=0 ) {
    config.setValue( "ra_gallery_token", token_g );
    imageSender.setConfig( url, token_g );
  }
  request->redirect("/");
}



void onRequestRestart(AsyncWebServerRequest *request) {
  asyncLogger.log( "@ req restart" );

  // tohle je možné spustit jen přes AP, ne z internetu
  if( !filterApOnly(request) ) return;

  ESP.restart();
}



void onRequestImage(AsyncWebServerRequest *request) {
  asyncLogger.log( "@ req image" );

  if( ! cameraHelper.hasSavedImage() ) {
    asyncLogger.log( "nemam fotku" );
    AsyncResponseStream *response = request->beginResponseStream(webserver.HTML_UTF8);
    response->print( "ERR" );
    request->send(response);
  }

  AsyncWebServerResponse *response = request->beginResponse(
          200, 
          "image/jpeg", 
          cameraHelper.savedImg.data, 
          cameraHelper.savedImg.size
        );
  request->send(response);
}



void onRequestSetCamera(AsyncWebServerRequest *request){
  asyncLogger.log( "@ req setcamera" );

  // tohle je možné spustit jen přes AP, ne z internetu
  if( !filterApOnly(request) ) return;

  AsyncResponseStream *response = request->beginResponseStream(webserver.HTML_UTF8);
  response->print( htmlHlavicka );

  response->print( "<p><a href=\"/\">Zpět</a></p>" );

  response->print( "<h1>Konfigurace kamery</h1>" );

  response->print( "<h2>Otočení a zobrazení</h2>");
  response->print( "<form action=\"/setcamA\" method=\"GET\">");
  response->print( "VFLIP - vertikální zrcadlo (0/1):<br>");
  response->printf( "<input type=\"text\" name=\"vflip\" value=\"%d\">",
                        config.getBool( "camera_vflip", false ) ? 1 : 0 );
  response->print( "<br>HMIRROR - horizontální zrcadlo (0/1):<br>");
  response->printf( "<input type=\"text\" name=\"hmirror\" value=\"%d\">",
                        config.getBool( "camera_hmirror", false ) ? 1 : 0 );
  response->print( "<br>Lens correction (0/1):<br>");
  response->printf( "<input type=\"text\" name=\"lenscorr\" value=\"%d\">",
                        config.getBool( "camera_lenscorr", false ) ? 1 : 0 );
  response->print( "<br><input type=\"submit\" name=\"send\" value=\"Zapiš\">");
  response->print( "</form>" );
    
  response->print( "<h2>Základní parametry</h2>");
  response->print( "<form action=\"/setcamA\" method=\"GET\">");
  response->print( "raw gamma (0/1), 1 je lepší:<br>");
  response->printf( "<input type=\"text\" name=\"rawgma\" value=\"%d\">",
                        config.getBool( "camera_rawgma", true ) ? 1 : 0 );
  response->print( "<br>kontrast (OV2640: -2 až 2, OV3660: -3 až 3):<br>");
  response->printf( "<input type=\"text\" name=\"contrast\" value=\"%d\">",
                        config.getLong( "camera_contrast", 2 ) );
  response->print( "<br>jas (OV2640: -2 až 2, OV3660: -3 až 3):<br>");
  response->printf( "<input type=\"text\" name=\"brightness\" value=\"%d\">",
                        config.getLong( "camera_brightness", 0 ) );
  response->print( "<br>saturace (OV2640: -2 až 2, OV3660: -4 až 4):<br>");
  response->printf( "<input type=\"text\" name=\"saturation\" value=\"%d\">",
                        config.getLong( "camera_saturation", -2 ) );
  response->print( "<br>AE level (OV2640: -2 až 2, OV3660: -5 až 5):<br>");
  response->printf( "<input type=\"text\" name=\"ae_level\" value=\"%d\">",
                        config.getLong( "camera_ae_level", 0 ) );
  response->print( "<br><input type=\"submit\" name=\"send\" value=\"Zapiš\">");
  response->print( "</form>" );

  response->print( "<h2>Expozice</h2>");
  response->print( "<form action=\"/setcamA\" method=\"GET\">");
  response->print( "auto exposure (0/1):<br>");
  response->printf( "<input type=\"text\" name=\"aec\" value=\"%d\">",
                        config.getBool( "camera_aec", true ) ? 1 : 0 );
  response->print( "<br>manual exposure level (0-1200):<br>");
  response->printf( "<input type=\"text\" name=\"manual_exposure\" value=\"%d\">",
                        config.getLong( "camera_manual_exposure", 600 ) );
  response->print( "<br>AGC automatic gain control (0/1):<br>");
  response->printf( "<input type=\"text\" name=\"agc\" value=\"%d\">",
                        config.getBool( "camera_agc", true ) ? 1 : 0 );
  response->print( "<br>AE gain ceiling (OV2640: 0-6, OV3660: 0-56):<br>");
  response->printf( "<input type=\"text\" name=\"gainceiling\" value=\"%d\">",
                        config.getLong( "camera_gainceiling", 6 ) );
  response->print( "<br>exposure gain (OV2640: 0-30, OV3660: 1-64):<br>");
  response->printf( "<input type=\"text\" name=\"manualgain\" value=\"%d\">",
                        config.getLong( "camera_manualgain", 20 ) );
  response->print( "<br>auto white balance (0/1):<br>");
  response->printf( "<input type=\"text\" name=\"awb\" value=\"%d\">",
                        config.getBool( "camera_awb", true ) ? 1 : 0 );
  response->print( "<br>manual white balance (0=auto, 1=slunce, 2=mraky, 3=zářivky, 4=žárovky):<br>");
  response->printf( "<input type=\"text\" name=\"wb_mode\" value=\"%d\">",
                        config.getLong( "camera_wb_mode", 1 ) );
  response->print( "<br><input type=\"submit\" name=\"send\" value=\"Zapiš\">");
  response->print( "</form>" );

  response->print( "<p><a href=\"/\">Zpět</a></p>" );

  response->print( htmlPaticka );
  request->send(response);
}

void onRequestSetCameraA(AsyncWebServerRequest *request) {
  asyncLogger.log( "@ req setwifiA" );

  // tohle je možné spustit jen přes AP, ne z internetu
  if( !filterApOnly(request) ) return;

  int v = webserver.getQueryParamAsLong( request, "vflip", -999 );
  if( v!=-999 ) {
    config.setValue( "camera_vflip", v );
  }
  v = webserver.getQueryParamAsLong( request, "hmirror", -999 );
  if( v!=-999 ) {
    config.setValue( "camera_hmirror", v );
  }
  v = webserver.getQueryParamAsLong( request, "lenscorr", -999 );
  if( v!=-999 ) {
    config.setValue( "camera_lenscorr", v );
  }
  v = webserver.getQueryParamAsLong( request, "rawgma", -999 );
  if( v!=-999 ) {
    config.setValue( "camera_rawgma", v );
  }
  v = webserver.getQueryParamAsLong( request, "contrast", -999 );
  if( v!=-999 ) {
    config.setValue( "camera_contrast", v );
  }
  v = webserver.getQueryParamAsLong( request, "brightness", -999 );
  if( v!=-999 ) {
    config.setValue( "camera_brightness", v );
  }
  v = webserver.getQueryParamAsLong( request, "saturation", -999 );
  if( v!=-999 ) {
    config.setValue( "camera_saturation", v );
  }
  v = webserver.getQueryParamAsLong( request, "ae_level", -999 );
  if( v!=-999 ) {
    config.setValue( "camera_ae_level", v );
  }
  v = webserver.getQueryParamAsLong( request, "awb", -999 );
  if( v!=-999 ) {
    config.setValue( "camera_awb", v );
  }  
  v = webserver.getQueryParamAsLong( request, "aec", -999 );
  if( v!=-999 ) {
    config.setValue( "camera_aec", v );
  }  
  v = webserver.getQueryParamAsLong( request, "wb_mode", -999 );
  if( v!=-999 ) {
    config.setValue( "camera_wb_mode", v );
  }
  v = webserver.getQueryParamAsLong( request, "gainceiling", -999 );
  if( v!=-999 ) {
    config.setValue( "camera_gainceiling", v );
  }
  v = webserver.getQueryParamAsLong( request, "manualgain", -999 );
  if( v!=-999 ) {
    config.setValue( "camera_manualgain", v );
  }
  v = webserver.getQueryParamAsLong( request, "agc", -999 );
  if( v!=-999 ) {
    config.setValue( "camera_agc", v );
  }
  v = webserver.getQueryParamAsLong( request, "manual_exposure", -999 );
  if( v!=-999 ) {
    config.setValue( "camera_manual_exposure", v );
  }
  
  tasker.setTimeout( reparamCamera, 1 );

  request->redirect("/setcamera");
}

/**

// ESP32-CAM (AI Thinker)
//    FQBN: esp32:esp32:esp32cam:PartitionScheme=min_spiffs
// ESP32-S3-CAM
//    FQBN: esp32:esp32:esp32s3:PartitionScheme=minimal,PSRAM=opi

ESP32 arduino core 3.3.5

Using library DNSServer at version 3.3.5 in folder: C:\Users\brouzda\AppData\Local\Arduino15\packages\esp32\hardware\esp32\3.3.5\libraries\DNSServer 
Using library ESP32 Async UDP at version 3.3.5 in folder: C:\Users\brouzda\AppData\Local\Arduino15\packages\esp32\hardware\esp32\3.3.5\libraries\AsyncUDP 
Using library WiFi at version 3.3.5 in folder: C:\Users\brouzda\AppData\Local\Arduino15\packages\esp32\hardware\esp32\3.3.5\libraries\WiFi 
Using library Networking at version 3.3.5 in folder: C:\Users\brouzda\AppData\Local\Arduino15\packages\esp32\hardware\esp32\3.3.5\libraries\Network 
Using library NetworkClientSecure at version 3.3.5 in folder: C:\Users\brouzda\AppData\Local\Arduino15\packages\esp32\hardware\esp32\3.3.5\libraries\NetworkClientSecure 
Using library Async TCP at version 3.4.0 in folder: E:\dev.moje\arduino\libraries\Async_TCP 
Using library ESP Async WebServer at version 3.7.7 in folder: E:\dev.moje\arduino\libraries\ESP_Async_WebServer 
Using library FS at version 3.3.5 in folder: C:\Users\brouzda\AppData\Local\Arduino15\packages\esp32\hardware\esp32\3.3.5\libraries\FS 
Using library Tasker at version 2.0.3 in folder: E:\dev.moje\arduino\libraries\Tasker 
Using library SPIFFS at version 3.3.5 in folder: C:\Users\brouzda\AppData\Local\Arduino15\packages\esp32\hardware\esp32\3.3.5\libraries\SPIFFS 
Using library Hash at version 3.3.5 in folder: C:\Users\brouzda\AppData\Local\Arduino15\packages\esp32\hardware\esp32\3.3.5\libraries\Hash 

 */