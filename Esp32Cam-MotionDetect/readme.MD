# Esp32Cam-MotionDetect

## Obsah
* [Jak aplikace funguje?](#jak-to-funguje)
* [Co kde v aplikaci najdete](#co-kde-najdete)
* [Sestavení aplikace](#sestaven%C3%AD-aplikace)
* [Konfigurace aplikace](#konfigurace-aplikace)
* [První spuštění](#prvn%C3%AD-spu%C5%A1t%C4%9Bn%C3%AD)
* [Přístup přes lokální AP](#p%C5%99%C3%ADstup-p%C5%99es-lok%C3%A1ln%C3%AD-ap)
* [Nahrávání obrázků do galerie](#nahr%C3%A1v%C3%A1n%C3%AD-obr%C3%A1zk%C5%AF-do-galerie)
* [Použité knihovny třetích stran](#knihovny-t%C5%99et%C3%ADch-stran)


## Úvod

Aplikace pro obsluhu kamery v ESP32, která pořizuje fotky při pohybu (změnách) v obraze.

Má následující funkce:
* neustále snímá fotky, pokud se na nich něco změní, pošle fotku do galerie https://ratatoskr.lovecka.info/
* pokud nedojde k žádnému pohybu 20 minut, pošle fotku
* lokálně nabízí Wifi AP, na které se můžete připojit a přes webový prohlížeč
  * koukat na aktuální fotku z kamery
  * nastavovat parametry kamery
  * nastavit Wifi připojení (SSID, heslo)
  * nastavit parametry připojení na galerii (token)

Pokud hledáte jen jednoduchou obsluhu kamery bez detekce změn v obrazu, koukněte [sem](../Esp32Cam-Simple/).

## Jak to funguje?

Aplikace neustále snímá fotky v nízkém rozlišení 640x480. Každý snímek dekóduje pomocí knihovny TJpg_Decoder a pro čtverce 16x8 bodů
(které jsou výsledkem JPG dekódéru) spočte průměrnou hodnotu jasu v každém barevném kanále.
Mezi po sobě následujícími dvěma fotkami pak porovná hodnoty jednotlivých čtverců a pokud je dost čtverců s větším rozdílem průměrného 
jasu, než je limit, oznámí detekci pohybu.

Po detekci pohybu se do galerie odešlou **tři** fotky:
- "motion-leader" - fotka v nízkém rozlišení 640x480, ve které byl detekován pohyb
- "motion" - hned poté vyfocená fotka v maximálním rozlišení kamery
- "after-motion" - cca o sekundu později vyfocená fotka v maximálním rozlišení kamery

Hodnota limitu i počet čtverců, které se musí změnit, se dynamicky mění - podle výsledku předešlého snímku se nastavují limity 
pro snímek následující.

Dále aplikace rozpoznává situaci, kdy je "celý snímek černý" (jas většiny čtverců je menší než nastavený limit). Pokud je snímek černý,
**nedetekuje** se pohyb. A vzhledem k tomu, že (zejména) OV2640 si při nízkých úrovních jasu často vymýšlí šum (vodorovné světlé pruhy), přepnutí 
z režimu "černý snímek" do běžného stavu se provede až po **pěti po sobě následujících** snímcích s vyšším jasem. Tedy i když je uprostřed tmy jeden snímek, který má vyšší jas, nebude detekován.

Detekce pohybu je prováděna v objektu MotionHelper.
Pokud chcete použít detekci pohybu, ale chcete obrázky nahrávat někam jinam, změňte v Esp32Cam-MotionDetect.ino funkci sendImage().


### Upozornění k OV2640

**POZOR!** Detekce pohybu funguje skvěle jen na chytřejších kamerách - třeba OV3660. 
U nejběžnější a nejlevnější **OV2640** bohužel platí, že
* Při změně rozlišení se smaže nastavení automatické expozice - takže je při použití automatické expozice potřeba počkat třeba 10 snímků, než se obraz po změně rozlišení ustálí. Takže mezi detekcí pohybu a vyfocením fotky v plné kvalitě uplyne docela dlouhá doba, kdy se "nic neděje". Počet ignorovaných snímků se dá nastavit na konci fce CameraHelper.setResolution.
* Kamera má vysoký šum, takže za nižších hladin osvětlení při povoleném vyšším gainu (camera_gainceiling > 2 ) nepřetržitě hlásí pohyb. Nedávejte vyšší hodnotu gainceling než 2!

Takže tam, kde OV3660 při pohybu "okamžitě" (250 ms) vyfotí fotku ve vysokém rozlišení, OV2640 bude mít vždy trochu zpoždění.
Pokud si nastavíte pro nějaké specifické použití manuální expozici, můžete snížit množství zahazovaných fotek na 2.

## Co kde najdete?

* **Esp32Cam-MotionDetect.ino** - Vlastní základní kód aplikace.
  * setup() - Inicializace kamery, detektoru pohybu, wifi klienta, AP.
  * userRoutes() - Nastavuje konfiguraci webserveru - jaká URL budou mapována na jaké funkce.
  * WifiStatus_Connected(), WifiStatus_NotConnected() - Callbacky pro změnu stavu wifi klienta.
  * captureAndAnalyzeMotion() - Funkce volaná každých 100 msec. Sejme snímek, zavolá na něj detekci pohybu a pokud je pohyb detekován, vyfotí obrázek ve vysokém rozlišení, odešle snímky do galerie a naplánuje nafocení after-motion snímku.
  * captureFullSize() - Sejme jeden obrázek ve vysokém rozlišení.
  * captureAndSendPeriodically() - Priodicky volaná fukce pro vyfocení a odeslání snímku jednou za 20 min, když se nic neděje (není detekován pohyb).
  * sendImage() - Odeslání fotky do galerie.
  * loop() - Hlavní smyčka
  * a za ní následuje obsluha webserveru
  * (Pro periodické a časové spouštění operací v aplikaci se používá knihovna Tasker.)

* **CameraHelper**  - Obsluha kamery. Inicializace, ověření správného typu kamery, nastavení provozních parametrů, přepínání rozlišení.

* **MotionDetector**  - Detekce pohybu.

* **board_config.h** - Konfigurace, jakou máte ESP32 desku. (Převzato ze samplu CameraWebServer)

* **camera_pins.h** - Detailní konfigurace desky na základě volby v board_config.h. (Převzato ze samplu CameraWebServer)

* **src/logging/** - Knihovny pro logování na sériový port (SerialLogger) a asynchronní logování (AsyncLogger). 
  * Proč se nepoužívá Serial.println()? Protože použitím těchto knihoven zároveň vzniká možnost odesílání logů na server. (Což se v tomhle projektu nepoužívá.)

* **src/net/WifiRunner** - Obsluha Wifi. Připojení k wifi v režimu klienta s automatickým reconnectem, pokud se spojení nedaří. Vytvoření AP pro připojení klientů k administraci aplikace.

* **src/net/EasyWebServer** - Webový server pro kontrolu stavu a nastavení konfigurace aplikace.

* **src/net/ImageSender** - Odesílání obrázků do galerie https://ratatoskr.lovecka.info/

* **src/toolkit/AppState** - Sdílený stav aplikace. Sem se zapisují chyby, které jsou pak vypisovány ve webové administraci zařízení.

* **src/toolkit/BasicConfig** a **src/toolkit/ConfigProviderSpiffs** - Konfigurace aplikace a její ukládání do SPIFFS.

* **src/toolkit/DeviceInfo** - Výpis stavu paměti.



## Sestavení aplikace

Aplikace je určená pro Arduino IDE. Je potřeba mít nainstalovanou desku ESP32 ve verzi **3.3.x** (nyní 3.3.5).

Pro sestavení potřebujete následující knihovny:
* Async TCP (by ESP32Async) 3.4.10
* ESP Async WebServer (by ESP32Async) 3.9.6
* Tasker (by Petr Stehlík) 2.0.3

Před sestavením musíte aplikaci nakonfigurovat podle své desky a kamery.

## Konfigurace aplikace

### 1) Konfigurace desky

1.A) Pokud máte desku ESP32-CAM, tj. tuhle:

![ESP32-CAM](doc/aithinker.jpg)

tak v souboru [board_config.h](board_config.h) odkomentujte řádek

```
#define CAMERA_MODEL_AI_THINKER // Has PSRAM
```

a zakomentujte ostatní varianty. V Arduino IDE zvolte typ desky "AI Thinker ESP32-CAM" a udělejte tato nastavení:

![ESP32-CAM konfigurace](doc/aithinker-config.png)


1.B Pokud máte desku ESP32S3-CAM, tj. tuhle:

![ESP32S3-CAM](doc/s3cam.jpg)

tak v souboru [board_config.h](board_config.h) odkomentujte řádek

```
#define CAMERA_MODEL_ESP32S3_EYE // Has PSRAM
```

a zakomentujte ostatní varianty. Pozor, nesplést s ..._ESP_EYE!

Pro připojení k počítači použijte pravý z USB konektorů (tak, jak je na fotce). 

V Arduino IDE zvolte desku "ESP32S3 Dev Module" a udělejte tato nastavení:

![ESP32S3-CAM konfigurace](doc/s3cam-config.png)

### 2) Nastavení kamery

Na začátku souboru [Esp32Cam-Simple.ino](Esp32Cam-Simple.ino) zvolte kameru, jakou máte.
Nejčastější verze je OV2640. Typ kamery je většinou napsán na plochém kabelu ke kameře (viz fotky výše).

```
#define TYP_KAMERY CAMERA_OV2640
// #define TYP_KAMERY CAMERA_OV3660
```

### 3) Nastavení Wifi připojení

V souboru [defaultcfg.h](defaultcfg.h) je konfigurace Wifi klienta (jméno sítě, heslo) a konfigurace galerie, do které se mají fotky nahrávat.
Token, který je tam vložen, je platný.

Všechny tyto konfigurační hodnoty můžete následně změnit připojením k wifi vašeho ESP.

### 4) Nastavení Wifi AP

V souboru [webserver-config.h](webserver-config.h) je konfigurace Wifi AP a webového serveru.


## První spuštění

Po prvním spuštěním vám aplikace na sériový port vypíše

```
E (3011) SPIFFS: mount failed, -10025
```

a asi na 20 sekund se zasekne. Nepropadejte panice - probíhá formát flash filesystému. Další starty už budou rychlé.


## Přístup přes lokální AP

Na svém telefonu si **vypněte** mobilní data (alespoň to doporučuju - leckteré telefony ochotně přehazují provoz na mobilní data, pokud se jim wifi nelíbí).

Připojte se na AP pojmenované "EspCam_123456" (kde číslo se liší podle ID vašeho čipu). Heslo je "password" (obojí můžete změnit v [webserver-config.h](webserver-config.h) ).

Pokud se telefon zeptá ve stylu "Tato síť není připojená do internetu, co mám dělat?", zvolte "Zachovat připojení".

A pak už by se telefon měl ozvat, že "Tato síť vyžaduje vaše přihlášení" a automaticky vás přehodit na webovou stránku ESP kamery. Pokud to neudělá, spusťte webový prohlížeč a v něm otevřte adresu **http://192.168.59.1**

Objeví se stránka s informací o stavu zařízení a s aktuální fotkou:

![aplikace 1](doc/app1.png)

Dole máte odkazy na jednotlivé nastavovací stránky. Můžete nastavit připojení k Wifi, připojení k serveru a pak taky vlastnosti kamery.

![aplikace 2](doc/app2.png)


## Nahrávání obrázků do galerie

Po 30 sekundách od startu (SEND_PICTURE_FIRST_PHOTO_MS) a pak při pohybu nebo jednou za čas (SEND_PICTURE_PERIODICALLY_INTERVAL_MS) se odešle aktuální fotka do galerie s použitím tokenu z [defaultcfg.h](defaultcfg.h) nebo zadaného přes wifi.
Pokud necháte API token, co je v souboru nyní, fotka bude vidět v této galerii:
https://ratatoskr.lovecka.info/app/gallery/main/6/Ap8LzcoyAb8s19bFGVVc72zcgqYgvqMy

Ale správná cesta je zaregistrovat se, založit si vlastní galerii a zadat sem její token.

## Knihovny třetích stran

Kromě knihoven z Arduino IDE (viz kapitola "Sestavení aplikace") aplikace obsahuje následující knihovny přímo v kódu.

### TJpg_Decoder by Bodmer
* v adresáři [src/TJpg_Decoder/](src/TJpg_Decoder/)
* zdroj: https://github.com/Bodmer/TJpg_Decoder
* [licence](src/TJpg_Decoder/license.txt)



