#include "CameraHelper.h"



// ===========================
// Select camera model in board_config.h
// ===========================
#include "board_config.h"

// dokumentace, zdrojáky:
// https://github.com/espressif/esp32-camera





CameraHelper::CameraHelper(LoggerInterface *logger, BasicConfig * config, AppState *appState)
{
    this->logger = logger;
    this->appState = appState;
    this->config = config;
    
    this->camCfg = (camera_config_t*)malloc( sizeof(camera_config_t) );
    memset( this->camCfg, 0, sizeof(camera_config_t) ); 

    this->img.size = 0;
    this->img.maxSize = 0;
    this->img.data = NULL;

    this->savedImg.size = 0;
    this->savedImg.maxSize = 0;
    this->savedImg.data = NULL;

}

/** 
 * Inicializace kamery; musí být voláno ze setup().
 * Vybráno z examplu CameraWebServer.ino
 */ 
void CameraHelper::cameraInit( camera_model_t cameraModel ) {
    this->cameraOK = false;
    this->cameraModel = cameraModel;

    this->img.maxSize = NAS_FRAMEBUFFER_SIZE;
    this->img.data = (unsigned char *)ps_malloc(NAS_FRAMEBUFFER_SIZE);

    this->savedImg.maxSize = NAS_FRAMEBUFFER_SIZE;
    this->savedImg.data = (unsigned char *)ps_malloc(NAS_FRAMEBUFFER_SIZE);

    if( this->savedImg.data==NULL || this->img.data==NULL ) {
      appState->setProblem( ERROR, "Nepodarila se alokace %d byte PSRAM", 2*NAS_FRAMEBUFFER_SIZE );
      return;
    }
  
    // konfigurační položky:
    // https://github.com/espressif/esp32-camera/blob/master/driver/include/esp_camera.h
    // https://github.com/espressif/esp32-camera/blob/master/driver/include/sensor.h

    // nastavení pinů se bere z camera_pins.h podle zvolené desky v  board_config.h
    // oba soubory jsou z examplu CameraWebServer.ino

    this->camCfg->pin_pwdn  = PWDN_GPIO_NUM;
    this->camCfg->pin_reset = RESET_GPIO_NUM;
    this->camCfg->pin_xclk = XCLK_GPIO_NUM;
    this->camCfg->pin_sccb_sda = SIOD_GPIO_NUM;
    this->camCfg->pin_sccb_scl = SIOC_GPIO_NUM;

    this->camCfg->pin_d7 = Y9_GPIO_NUM;
    this->camCfg->pin_d6 = Y8_GPIO_NUM;
    this->camCfg->pin_d5 = Y7_GPIO_NUM;
    this->camCfg->pin_d4 = Y6_GPIO_NUM;
    this->camCfg->pin_d3 = Y5_GPIO_NUM;
    this->camCfg->pin_d2 = Y4_GPIO_NUM;
    this->camCfg->pin_d1 = Y3_GPIO_NUM;
    this->camCfg->pin_d0 = Y2_GPIO_NUM;
    this->camCfg->pin_vsync = VSYNC_GPIO_NUM;
    this->camCfg->pin_href = HREF_GPIO_NUM;
    this->camCfg->pin_pclk = PCLK_GPIO_NUM;

    this->camCfg->xclk_freq_hz = 20000000;
    this->camCfg->ledc_timer = LEDC_TIMER_0;
    this->camCfg->ledc_channel = LEDC_CHANNEL_0;

    this->camCfg->pixel_format = PIXFORMAT_JPEG; // YUV422,GRAYSCALE,RGB565,JPEG

    // quality - podle ruznych zdroju:
    // 0-63, for OV series camera sensors, lower number means higher quality
    // 10-63 lower number means higher quality
    int jpegQuality;
    //   For ESP32, do not use sizes above QVGA when not JPEG. The performance of the ESP32-S series has improved a lot, but JPEG mode always gives better frame rates.
    switch( cameraModel ) {
      case CAMERA_OV2640:
        this->logger->log( "camera: OV2640, 1600x1200"); 
        jpegQuality = 10;
        this->maxResolution = FRAMESIZE_UXGA;
        this->expectedResolutionW = 1600;
        this->expectedResolutionH = 1200; 
        break;

      case CAMERA_OV3660:
        this->logger->log( "camera: OV3660, 2048x1536"); 
        // hodnota 0-3 = pad kamery !
        jpegQuality = 4;
        this->maxResolution = FRAMESIZE_QXGA;
        this->expectedResolutionW = 2048;
        this->expectedResolutionH = 1536; 
        // pokud neni nastaven default, nastavime ho - pro tuhle kameru je potreba vic nez 6!
        if( this->config->getLong( "camera_gainceiling", -99999 ) == -99999 ) {
          this->config->setValue( "camera_gainceiling", 40 );
        }
        break;

      case CAMERA_OV5640:
        this->logger->log( "camera: OV5640, 2560x1920" );
        jpegQuality = 10;
        this->maxResolution = FRAMESIZE_QSXGA;
        this->expectedResolutionW = 2560;
        this->expectedResolutionH = 1920; 
        break;

      default:
        this->logger->log( "camera: neznamy typ, doplnte si konfiguraci, 1280x720" );
        jpegQuality = 10;
        this->maxResolution = FRAMESIZE_HD;
        this->expectedResolutionW = 1280;
        this->expectedResolutionH = 720; 
        break;
    }
    this->logger->log( "rozliseni %d, jpeg quality %d", this->camCfg->frame_size, this->camCfg->jpeg_quality );   
    this->camCfg->jpeg_quality = this->config->getLong( "camera_jpeq_quality", jpegQuality ); 
    this->camCfg->frame_size = this->maxResolution;
    
    this->camCfg->fb_count = 2; //When jpeg mode is used, if fb_count more than one, the driver will work in continuous mode.
    this->camCfg->grab_mode =  CAMERA_GRAB_LATEST; //CAMERA_GRAB_WHEN_EMPTY CAMERA_GRAB_LATEST. Sets when buffers should be filled
    
  #if defined(CAMERA_MODEL_ESP_EYE)
    pinMode( Y3_GPIO_NUM, INPUT_PULLUP);    // 13
    pinMode( Y4_GPIO_NUM, INPUT_PULLUP);    // 14
  #endif  

    if(PWDN_GPIO_NUM != -1){
        pinMode(PWDN_GPIO_NUM, OUTPUT);
        digitalWrite(PWDN_GPIO_NUM, LOW);
    }

    // camera init
    esp_err_t err = esp_camera_init(this->camCfg);
    if (err != ESP_OK) {
      this->appState->setProblem( ERROR, "Camera init failed with error 0x%x", err ); 
      return;
    }

    sensor_t *s = esp_camera_sensor_get();

    this->cameraOK = true;

    // ověřit typ kamery
    if( cameraModel==CAMERA_OV2640 && s->id.PID!=OV2640_PID) {
      
      this->appState->setProblem( ERROR, "Camera init: chyba konfigurace, ma to byt OV2640 a je to pid:%d", s->id.PID );   
      this->cameraOK = false;
    }
    if( cameraModel==CAMERA_OV3660 && s->id.PID!=OV3660_PID) {
      this->appState->setProblem( ERROR, "Camera init: chyba konfigurace, ma to byt OV3660 a je to pid:%d", s->id.PID );   
      this->cameraOK = false;
    }
    if( cameraModel==CAMERA_OV5640 && s->id.PID!=OV5640_PID) {
      this->appState->setProblem( ERROR, "Camera init: chyba konfigurace, ma to byt OV5640 a je to pid:%d", s->id.PID );   
      this->cameraOK = false;
    }

    if( this->cameraOK ) {
      this->logger->log("Camera init OK");
    }
}



/*
  https://randomnerdtutorials.com/esp32-cam-ov2640-camera-settings/

  frekvence kamery v MHz, default 20
  snížení na 5 zvyšuje u OV2640 kvalitu (!)
  int res = s->set_xclk(s, LEDC_TIMER_0, xclk);

  */  
void CameraHelper::setCameraParameters() {

  sensor_t *s = esp_camera_sensor_get();

  this->logger->log( "camera parameters:" );
  

  // https://github.com/eloquentarduino/EloquentEsp32cam/blob/main/src/eloquent_esp32cam/camera/sensor.h
  bool vflip = this->config->getBool( "camera_vflip", false );
  s->set_vflip( s, vflip ? 1 : 0 );
  bool hmirror = this->config->getBool( "camera_hmirror", false );
  s->set_hmirror( s, hmirror ? 1 : 0 );
  bool lensCorr = this->config->getBool( "camera_lenscorr", false );
  s->set_lenc( s, lensCorr ? 1 : 0 );
  // int clk = (int)this->config->getLong( "camera_clk", 20);  // default 20, dobré pro UXGA
  // s->set_xclk(s, LEDC_TIMER_0, clk);

  this->logger->log( "* vflip=%s hmirror=%s lens_corr=%s",    
                      vflip ? "Y" : "N", hmirror ? "Y" : "N", lensCorr  ? "Y" : "N" );
                // clk=%d MHz, clk

/*
gain control auto
	agc=1			set_gain_ctrl
manual
	agc=0			set_gain_ctrl
	agc_gain 1-64		set_agc_gain

gainceiling nezávisle na agc
				set_gainceiling
*/                
  bool agc = this->config->getBool( "camera_agc", true );
  // OV2640: 0-6, OV3660: 0-56 
  int gainCeiling = (int)this->config->getLong( "camera_gainceiling", 6 );  
  if( agc ) {
    this->logger->log( "* gain auto, ceiling=%d", gainCeiling );
    s->set_gain_ctrl(s, 1);  
    s->set_gainceiling(s, (gainceiling_t)gainCeiling); 
  } else {
    // OV2640: 0-30   OV3660: 1-64
    int manualGain = (int)this->config->getLong( "camera_manualgain", 20);  // 0-30
    this->logger->log( "* gain manual, gain=%d", manualGain );
    s->set_gain_ctrl(s, 0);  
    s->set_agc_gain(s, manualGain);  
  }
  // Nesmi se poslat set_agc_gain a set_gainceiling za sebou, to vede k chybě "cam_hal: FB-OVF" ??

  // raw gamma
  bool rawgma = this->config->getBool( "camera_rawgma", true );
  // OV2640: -2 .. 2, OV3660: -3 .. 3
  int contrast = (int)this->config->getLong( "camera_contrast", 0);  
  // OV2640: -2 .. 2, OV3660: -3 .. 3
  int brightness = (int)this->config->getLong( "camera_brightness", 0);  
  // OV2640: -2 .. 2, OV3660: -4 .. 4
  int saturation = (int)this->config->getLong( "camera_saturation", -2); 
  this->logger->log( "* contrast=%d, brightness=%d, saturation=%d, rawgma=%s", 
        contrast, brightness, saturation,
        rawgma ? "Y" : "N" );
  s->set_brightness(s, brightness);
  s->set_saturation(s, saturation); 
  s->set_contrast(s, contrast); 
  s->set_raw_gma(s, rawgma ? 1 : 0);  

/*
manual exposition
	aec=0			set_exposure_ctrl
	aec_value (0-1536	set_aec_value
auto exp
	aec=1			set_exposure_ctrl

night mode
	aec2=1			set_aec2
  */

  bool aec = this->config->getBool( "camera_aec", true );
  // OV2640: -2 .. 2, OV3660: -5 .. 5
  int ae_level = (int)this->config->getLong( "camera_ae_level", 0 ); 
  if( aec ) {
    this->logger->log( "* automatic exposure+night mode, ae_level=%d", ae_level );
    s->set_exposure_ctrl(s, 1); 
    s->set_aec2(s, 1);     
  } else {
    int manualExposure = (int)this->config->getLong( "camera_manual_exposure", 600 );  // 0-1200
    this->logger->log( "* manual exposure %d, ae_level=%d", manualExposure, ae_level );
    s->set_aec2(s, 0);     
    s->set_exposure_ctrl(s, 0); 
    s->set_aec_value(s, manualExposure);    // 0 to 1200
  }
  s->set_ae_level(s, ae_level);   // -2 to 2 

/*

auto awb:
	awb=1			set_whitebal
	dcw=1			set_dcw

manual awb:
	awb_gain=1		set_awb_gain
	wb_mode=<číslo>		set_wb_mode

        wb_mode
            0 – Auto
            1 – Sunny
            2 – Cloudy
            3 – Office
            4 – Home
*/
  bool awb = this->config->getBool( "camera_awb", true );
  int wb_mode = (int)this->config->getLong( "camera_wb_mode", 1 ); 
  if( awb ) {
    this->logger->log( "* white balance auto" );
    s->set_whitebal(s, 1); 
    s->set_dcw(s, 1); 
  } else {
    this->logger->log( "* white balance %d", wb_mode );
    s->set_whitebal(s, 0); 
    // https://randomnerdtutorials.com/esp32-cam-ov2640-camera-settings/
    s->set_awb_gain(s, 1); 
    s->set_wb_mode( s, wb_mode );
  }
}


bool CameraHelper::capture()
{
  if( !this->cameraOK ) {
    return false;
  }

  camera_fb_t *fb = NULL;

  // po zmene rozliseni zahazujeme nekolik naslednych fotek
  while( this->skipImages>0 ) {
    fb = esp_camera_fb_get();
    if( fb ) { esp_camera_fb_return(fb); }
    this->skipImages--;
  }

  fb = esp_camera_fb_get();

  if (!fb) {
    this->appState->setProblem( ERROR, "Chyba při focení: camera capture failed." );        
    this->errorsCapturingPhoto++;
    return false;
  }

  if (fb->format != PIXFORMAT_JPEG) {
    this->appState->setProblem( ERROR, "Chyba při focení: obrázek neni JPEG." );        
    this->errorsCapturingPhoto++;
    esp_camera_fb_return(fb);
    return false;
  }

  if( fb->width!=this->expectedResolutionW || fb->height!=this->expectedResolutionH ) {
    this->logger->log("Obrazek ma rozliseni %dx%d, ale chci %dx%d", 
      fb->width, fb->height, 
      this->expectedResolutionW, this->expectedResolutionH
    ); 
    this->errorsCapturingPhoto++;
    esp_camera_fb_return(fb);
    return false;
  }

  if( fb->len >= this->img.maxSize ) {
    this->logger->log("Obrazek je vetsi (%d) nez framebuffer (%d), preskakuji ", fb->len, this->img.maxSize );  
    this->appState->setProblem( ERROR, "Chyba při focení: obrázek se nevejde do paměti." );        
    this->errorsCapturingPhoto++;
    esp_camera_fb_return(fb);
    return false;
  }

  // nakopirujeme do pracovniho bufferu, po tu dobu zamkneme zamek, aby nedoslo ke kolizi s pripadnym volanim saveImage()

  std::lock_guard<std::mutex> lck(this->serial_mtx);

  this->img.size = fb->len;
  this->img.w = fb->width;
  this->img.h = fb->height;
  this->img.timeTaken = millis();

  memcpy( this->img.data, (const unsigned char *)fb->buf, fb->len );

  esp_camera_fb_return(fb);

  this->imagesTaken++;
  return true;
}

/**
 * Nakopíruje pracovní buffer do bufferu pro webserver - aby webvserver mohl podávat fotku a ta se mu neměnila pod rukama.
 */
void CameraHelper::saveImage() {
    if( this->img.size==0 ) return;
    
    // zamkneme zámek, aby se nám pracovní buffer neměnil pod rukama
    std::lock_guard<std::mutex> lck(this->serial_mtx);

    memcpy( this->savedImg.data, this->img.data, this->img.size );
    this->savedImg.size = this->img.size;
    this->savedImg.w = this->img.w;
    this->savedImg.h = this->img.h;
    this->savedImg.timeTaken = this->img.timeTaken;
}

void CameraHelper::setMaximalResolution()
{
  this->setResolution( this->maxResolution );
}

void CameraHelper::setResolution(framesize_t resolution)
{
  switch( resolution ) {
    case  FRAMESIZE_QSXGA:    // 2560x1920
      this->expectedResolutionW = 2560;
      this->expectedResolutionH = 1920; 
      break;
    case FRAMESIZE_QXGA:     // 2048x1536
      this->expectedResolutionW = 2048;
      this->expectedResolutionH = 1536; 
      break;
    case FRAMESIZE_UXGA:      // 1600x1200
      this->expectedResolutionW = 1600;
      this->expectedResolutionH = 1200;      
      break;
    case FRAMESIZE_HD:       // 1280x720
      this->expectedResolutionW = 1280;
      this->expectedResolutionH = 720;      
      break;
    case FRAMESIZE_VGA:      // 640x480
      this->expectedResolutionW = 640;
      this->expectedResolutionH = 480;      
      break;
    default:
      this->logger->log( "Pro tohle rozliseni chybi konfigurace, doplnte do setResolution()!");
      return;
  }
  sensor_t *s = esp_camera_sensor_get();
  int res = 0;
  res = s->set_framesize(s, (framesize_t)resolution);
  if( res!=0 ) {
    this->logger->log( "set_framesize: chyba %d", res );
  }
  this->skipImages = 2;
}

bool CameraHelper::hasImage()
{
  return this->img.size!=0;
}

bool CameraHelper::hasSavedImage()
{
  return this->savedImg.size!=0;
}

