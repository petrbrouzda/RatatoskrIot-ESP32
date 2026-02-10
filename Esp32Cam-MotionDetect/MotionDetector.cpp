#include "MotionDetector.h"





MotionDetector::MotionDetector(LoggerInterface *logger,  AppState *appState)
{
    this->logger = logger;
    this->appState = appState;
}

void MotionDetector::init( int w, int h )
{
  this->maxSizeW = w;
  this->maxSizeH = h;
  int size = this->maxSizeW/16 * this->maxSizeH/8 * 3 + 10;
  this->block1 = (unsigned char *)ps_malloc( size );
  this->block2 = (unsigned char *)ps_malloc( size );    
  if( this->block1==NULL || this->block2==NULL ) {
    this->appState->setProblem( ERROR, "Nepodarila se alokace %d byte PSRAM [MotionDetector:%d]", 2*size, __LINE__ );
    return;
  }  
}

void MotionDetector::setAfterMotionNumbnessMs(long time)
{
  this->afterMotionNumbness = time;
}

void MotionDetector::setMinimalBlocksChanged(int blocks)
{
  this->minimalChangeBlocks = blocks;
}

void MotionDetector::setMinimalBlockValueDifference(int value)
{
  this->minimalBlockChange = value;
}

/* staticka promenna pro callback */
MotionDetector * cbMotionDetector;

/**
 * Callback z TJpg_Detectoru, jen předá zpět do objektu.
 */
bool callback_jpg_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) 
{
  return cbMotionDetector->jpg_output(x, y, w, h, bitmap);
}


/*
dostává bloky 16x8 px, RGB565
*/
bool MotionDetector::jpg_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap)
{
  if( x>this->maxSizeW || y>this->maxSizeH ) {    
    return 1;
  }

  /*
  pro rozliseni 640x480 asi neni potreba!
  if( x==0 && y%20==0 ) {
    // na chvilku uvolnime procesor - 75 msec navic na velky obrazek
    delay(1);
  }
  */

  long sum_r = 0;
  long sum_g = 0;
  long sum_b = 0;

  int limit = w*h;
  uint16_t * p = bitmap;
  for( int i = 0; i<limit; i++ ) {
    uint16_t color565 = *p;
    p++;

    uint8_t r = (color565 >> 11) & 0x1F;
    uint8_t g = (color565 >> 5) & 0x3F;
    uint8_t b = (color565 & 0x1F);

    // Scale to 8-bit (0-255)
    uint8_t r8 = (r << 3) | (r >> 2); // Shift left and fill lower bits
    uint8_t g8 = (g << 2) | (g >> 4);
    uint8_t b8 = (b << 3) | (b >> 2);

    sum_r += r8;
    sum_b += b8;
    sum_g += g8;
  }

  int block_x = x / 16;
  int block_y = y / 8;

  /*
  this->logger->log("jpg_output @%d,%d %dx%d ... [%d,%d] = %d,%d,%d\n", x,y,w,h,
        block_x, block_y,
        sum_r/limit, sum_g/limit, sum_b/limit
       );
       */

  int pos = (block_x * this->blocksW + block_y) * 3;
  this->currentBlock[pos] = sum_r/limit;
  this->currentBlock[pos+1] = sum_g/limit;
  this->currentBlock[pos+2] = sum_b/limit;

  // Return 1 to decode next block
  return 1;
}

/**
 * Výstup: true = motion, false = non-motion
 * Jako sideefect nastavuje 
 * this->blackPhoto - snímání je v režimu noci, nedělá se analýza (do režimu noci se překlopí první černou fotkou, ale pro návrat do denního
 *                    režimu je potřeba 5 fotek)
 * 
 * interní výstup: 
 *  detectorBlack - aktuální fotka je příliš tmavá než aby se dělala analýza (týká se current fotky, interní proměnná pro MotionDetector) 
 *
 */
bool MotionDetector::porovnejBloky() {

  int avg_dev_sum = 0;
  int ct = 0;
  int changed = 0;
  int notNightBlocks = 0;
  bool blackPhoto;

  for( int x=0; x<this->blocksW; x++ )  {
    for( int y=0; y<this->blocksH; y++ ) {
        int pos = (x*this->blocksW + y) * 3;
        int dev_r = abs( this->block2[pos] - this->block1[pos] ); 
        int dev_g = abs( this->block2[pos+1] - this->block1[pos+1] );
        int dev_b = abs( this->block2[pos+2] - this->block1[pos+2] );
        int dev_sum = dev_r + dev_g + dev_b;

        int block_val = (this->block2[pos]+this->block2[pos+1]+this->block2[pos+2]) / 3;
        if( block_val > NIGHT_MAX_AVG_LEVEL_PER_BLOCK ) {
          notNightBlocks++;
        }

        // tady bylo 3*sum !
        if( dev_sum>(2*this->minDiff) )  {
          changed++;
        }

        avg_dev_sum += dev_sum;
        ct++;
    }
  }

  this->numBlocksChanged = changed;
  this->blockChangeLimit = this->minDiff;

  bool detection = changed > (this->avgChangedBlocks + this->avgChangedBlocks/8);

  this->minDiff = (this->minDiff*7/8) + (avg_dev_sum / ct / 8);
  if( this->minDiff < this->minimalBlockChange ) this->minDiff = this->minimalBlockChange;

  this->avgChangedBlocks = (this->avgChangedBlocks*3/4) + (changed/4);
  if( this->avgChangedBlocks < this->minimalChangeBlocks ) this->avgChangedBlocks = this->minimalChangeBlocks;

  bool detectorBlack = notNightBlocks < NIGHT_MAX_BLOCKS_OVER_LIMIT;
  

  /* Do nočního režimu bez detekce (this->blackPhoto) se to přepne první černou fotkou;
     do denního režimu s detekcí je potřeba 5 (NUM_PHOTOS_FOR_NIGHT_TO_DAY_TRANSITION) fotek světlých nad limit.
     To eliminuje chybové framy z kamery.
  */
  if( detectorBlack ) {
    this->blackPhoto = true;
    this->neededForDay = NUM_PHOTOS_FOR_NIGHT_TO_DAY_TRANSITION;
  } else {
    if( this->blackPhoto ) {
      this->neededForDay--;
      if( this->neededForDay==0 ) {
        // pokud se ze tmy preplo na svetlo, posleme detekci
        this->blackPhoto = false;
        detection = true;
      }
    }
  }

  // pořadové číslo fotky
  // o kolik se změnila hodnota pixelů v bloku - suma přes obraz / průměr na blok
  // L:jaký je nový limit pro velikost změny
  // nr: kolik bloků se změnilo a o kolik je to proti průměru
  // výstup detekce (YES / -)
  // je noční režim? (BLACK / -) : kolik bloků je nad limitem pro noc
  this->logger->log("#%d %d/%d L:%d nr:%d(%d) %s %s:%d", 
          this->imagesCount,
          avg_dev_sum,
          avg_dev_sum / ct,
          this->minDiff,
          changed,
          changed - this->avgChangedBlocks,
          detection ? "YES" : "-",
          this->blackPhoto ? "BLACK" : "-",
          notNightBlocks
       );  

  return detection;
}

/**
 * 0 = startup, nic se nedeje
 * 1 = bez pohybu / či necitlivost po předešlé detekci pohybu / černá fotka
 * 2 = pohyb
 * -1 = chyba
 */
int MotionDetector::analyze(unsigned char *image, int size, int w, int h )
{
    if( size==0 ) {
      return -1;
    }

    if( this->imagesCount == 0 ) {
        // inicializace
        this->blocksW = w/16;
        this->blocksH = h/8;
        cbMotionDetector = this;    
        this->currentBlock = this->block1;
    } else if( this->imagesCount == 1 ) {
        this->currentBlock = this->block2;
    } else {
        unsigned char * tmp = this->block1;
        this->block1 = this->block2;
        this->block2 = tmp;
        this->currentBlock = this->block2;        
    }

    if( w > this->maxSizeW || h > this->maxSizeH ) {
      this->appState->setProblem( ERROR, "Obrazek %dx%d je vetsi nez pracovni buffer!", w,h );
      return -1;
    }

    TJpgDec.setJpgScale(1);
    TJpgDec.setSwapBytes(false);
    TJpgDec.setCallback(callback_jpg_output);
    // trva pro 2048x1536 asi 1900 ms
    //   pro 640x480 asi 210 ms, to je daleko lepsi
    TJpgDec.drawJpg(0, 0, image, size );

    bool movement = false;
    if( this->imagesCount !=0 ) {
        // trva asi 6 ms pro 2048x1536, 1 ms pro 640x480
        movement = this->porovnejBloky();
    }

    this->imagesCount++;

    if( this->imagesCount < STARTUP_IMAGES ) {
        return 0;
    }

    // aktualni fotka je prilis cerna, detekce pohybu neni provadena
    if( this->blackPhoto ) {
      return 1;
    }

    if( movement && (millis()-this->lastMotionDetected < this->afterMotionNumbness) )  {
      // detekujeme pohyb maximalne jednou za <afterMotionNumbness> ms
      return 1;
    }

    if( movement ) {
      this->lastMotionDetected = millis();
      return 2;
    } else {
      return 1;
    }
}

void MotionDetector::imgSent()
{
  this->lastMotionDetected = millis();
}
