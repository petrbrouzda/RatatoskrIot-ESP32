#ifndef __MOTION__DETECT__H
#define __MOTION__DETECT__H

/**
 * Obsluha kamery
 */

#include <Arduino.h>
#include "src/TJpg_Decoder/TJpg_Decoder.h"

#include "src/logging/LoggerInterface.h"
#include "src/toolkit/AppState.h"

#define MOTION_DETECTOR_VERSION "2.2 2026-02-06"


// kolik snímků se na začátku projde, než se začne detekovat pohyb
#define STARTUP_IMAGES 20

// kolik bloků 16x8 px se musí minimálně změnit, aby se detekoval pohyb
#define MINIMUM_CHANGED_BLOCKS 50

// o kolik se minimálně musí změnit blok, aby se počítal jako změněný
#define MINIMAL_BLOCK_CHANGE 20

// nebude brát pohyb častěji než jednou za 15 sekund
#define AFTER_MOTION_NUMBNESS 15000

// detekce tmy - jaký je limit jasu bloku pro "den"
#define NIGHT_MAX_AVG_LEVEL_PER_BLOCK 30
// detekce tmy - kolik z 2400 bloků maximálně smí být přes limit
#define NIGHT_MAX_BLOCKS_OVER_LIMIT 30

// kolik fotek musí být ne-černých, aby se zapla detekce pohybu (je jich víc než 1, aby to ignorovalo náhodný šum kamery)
#define NUM_PHOTOS_FOR_NIGHT_TO_DAY_TRANSITION 5

class MotionDetector
{
    public:
        MotionDetector( LoggerInterface * logger, AppState * appState );

        /**
         * MUSÍ být voláno ze setup() - protože jinak nelze na některých ESP32 bezpečně naalokovat PSRAM
         * (bug zmiňován např. https://github.com/me-no-dev/ESPAsyncWebServer/issues/1074 )
         * 
         * Zadejte rozlišení, nad kterým se bude motion detect provozovat.
         */
        void init(  int w, int h  );

        /** Jak dlouho po pohybu se další pohyb nedetekuje */
        void setAfterMotionNumbnessMs( long time );

        /** kolik bloků 16x8 px se musí minimálně změnit, aby byl detekován pohyb (default: 50) */
        void setMinimalBlocksChanged( int blocks );

        /** o kolik se musí nejméně změnit průměrná barva bloku, aby se vzal jako změněný (default: 10)*/
        void setMinimalBlockValueDifference( int value );

        /** po analýze obrazu se nastaví na true, pokud je na fotce tma */
        bool blackPhoto = false;

        /** po detekci změny: kolik bloků se změnilo */
        int numBlocksChanged;

        /** po detekci změny: jaký byl limit pro změnu */
        int blockChangeLimit;

        /**
         * Analyzuje jeden snímek.
         * 
         * 0 = startup, nic se nedeje
         * 1 = bez pohybu
         * 2 = pohyb
         * -1 = došlo k nějaké chybě
         */
        int analyze( unsigned char * image, int size, int w, int h  );

        /** Z kamery byl ručně poslán obrázek, tedy dalších N sekund by to nemělo poslat další.  */
        void imgSent();

        /** kdy byl naposledy zaznamenán pohyb? */
        long lastMotionDetected = 0;


        /**
         * Interní funkce, která je ovšem volána zvenku (callbackem), proto je public. Nepoužívat!
         */
        bool jpg_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap);    
        
       
    private:
        LoggerInterface * logger;
        AppState * appState;

        int imagesCount = 0;
        
        int maxSizeW = 0;
        int maxSizeH = 0;
        int blocksW;
        int blocksH;

        unsigned char * block1;
        unsigned char * block2;
        unsigned char * currentBlock;

        /** minimální odchylka bloků mezi sebou, aby byl detekován pohyb */
        int minDiff = 100;
        /** průměrně se změnilo kolik bloků? */
        int avgChangedBlocks = 0;

        bool porovnejBloky();

        long afterMotionNumbness = AFTER_MOTION_NUMBNESS;
        int minimalChangeBlocks = MINIMUM_CHANGED_BLOCKS;
        int minimalBlockChange = MINIMAL_BLOCK_CHANGE;

        int neededForDay = 5;
        
};

#endif