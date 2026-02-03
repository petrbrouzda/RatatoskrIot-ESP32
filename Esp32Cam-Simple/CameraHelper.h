#ifndef __CAMERA__HELPER_H
#define __CAMERA__HELPER_H

/**
 * Obsluha kamery
 */

#include <Arduino.h>

#include "esp_camera.h"

#include "src/logging/LoggerInterface.h"
#include "src/toolkit/AppState.h"
#include "src/toolkit/BasicConfig.h"

#define CAMERAHELPER_VERSION "3.1 2026-02-02"


  // nastavení pinů se bere z camera_pins.h podle zvolené desky v  board_config.h


#define NAS_FRAMEBUFFER_SIZE 700000

/**
 * Jeden uložený obrázek
 */
class SavedImage
{
    public:
        unsigned char * data;
        int size;
        int w;
        int h;
        long timeTaken;
        int maxSize;
};


class CameraHelper
{
    public:
        CameraHelper( LoggerInterface * logger, BasicConfig * config, AppState * appState );

       
        /** 
         * Vlastní spuštění kamery; při chybě naplní appState.
         * 
         * MUSÍ být voláno ze setup() - protože jinak nelze na některých ESP32 bezpečně naalokovat PSRAM
         * (bug zmiňován např. https://github.com/me-no-dev/ESPAsyncWebServer/issues/1074 )
         */
        void cameraInit( camera_model_t cameraModel );
        
        /** nastaveni parametru, je možné volat i později po změně nastavení*/
        void setCameraParameters();
        
        /** Sejme jeden snímek a uloží ho do img. Nepracujeme přímo s framebufferem kamery, aby ho někdo nezapomněl vrátit. */
        bool capture();

        /** obrázek, se kterym pracujeme v hlavním threadu - pracovní buffer */
        SavedImage img;

        /** je obrázek v pracovním bufferu? */
        bool hasImage();

        /** obrazek, ktery je odlozeny pro webserver operaci saveImage() */
        SavedImage savedImg;

        /** je obrázek v bufferu pro webserver? */
        bool hasSavedImage();

        /**
         * Nakopíruje pracovní buffer do bufferu pro webserver - aby webvserver mohl podávat fotku a ta se mu neměnila pod rukama.
         */
        void saveImage();

        /**
         * Podařilo se nainicializovat kameru?
         */
        bool cameraOK = false;

        /**
         * Počet vyfocených obrázků
         */
        int imagesTaken = 0;

        /**
         * Počet neúspěšných pokusů o fotku
         */
        int errorsCapturingPhoto = 0;

    private:
         LoggerInterface * logger;
         AppState * appState;
         BasicConfig * config;

         camera_config_t * camCfg;

         std::mutex serial_mtx;
        
};

#endif