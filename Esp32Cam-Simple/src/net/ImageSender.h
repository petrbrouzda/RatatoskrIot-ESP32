#ifndef __IMG__SENDER_H
#define __IMG__SENDER_H

/**
  * Zajišťuje odeslání obrázků do galerie.
  * 
  * Pokud chcete ověřovat identitu serveru (certifikát) nebo posílat certifikát klienta, 
  * hledejte v CommandProcessor::connectToServer().
  */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "../logging/LoggerInterface.h"


#define IMAGESENDER_VERSION "1.2.1 2026-02-05"

/** max délka status textu v http response */
#define CP_HTTP_STATUS_LEN 50
/** max délka cesty / jména serveru */
#define CP_URL_LEN 100

/** na reakci serveru po odeslání požadavku se čeká max N bloků po 10 msec (tedy 500 = 5000 msec = 5 sec) */
#define MAX_WAIT_FOR_SERVER 500



class ImageSender
{
    public:
        ImageSender( LoggerInterface * logger = NULL );

        /**
         * Nastaví konfiguraci - URL serveru, bezpečnostní token, port.
         * URL rozebere na server a cestu.
         */
        void setConfig( const char * url, const char * token );

        /**
         * Pošle obrázek na server
         * Vrátí true=povedlo se, false=nepodařilo se.
         * 
         * fileName musí (!!!) být URLencoded, pokud obsahuje znaky jako mezery, 
         */
        bool sendImage( const unsigned char * image, int imageSize, const char * fileName );
        

    private:
        LoggerInterface * logger; 
        WiFiClient * secureClient;

        char path[CP_URL_LEN];
        char token[CP_URL_LEN];
        char server[CP_URL_LEN];
        int port;

        // provozni data
        int lastHttpStatus;
        char lastHttpStatusText[CP_HTTP_STATUS_LEN];

        /**
         * Zajistí napojení na server
         */
        bool connectToServer();

        /**
         * Načte headery z http responsu; true = 200 OK, false = cokoli jiného
         */
        bool parseStatusAndHeaders();

        /** data z posledniho zpracovani parseStatusAndHeaders() */
        int contentLength;

};

#endif