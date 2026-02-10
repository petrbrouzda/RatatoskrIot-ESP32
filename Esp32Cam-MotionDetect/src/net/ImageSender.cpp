#include "ImageSender.h"
#include <string.h>
#include "../logging/NullLogger.h"

#include "mbedtls/md.h"


ImageSender::ImageSender( LoggerInterface *logger )
{
    if( logger == NULL ) {
        this->logger = (LoggerInterface*) new NullLogger();
    } else {
        this->logger = logger;
    }
    
    this->lastHttpStatus=0;
    this->token[0] = 0;
}

//TODO: parsovat pripadny explicitne urceny port
/**
 * Nastaví konfiguraci - URL serveru, bezpečnostní token, port.
 * URL rozebere na server a cestu.
 */
void ImageSender::setConfig( const char *url, const char *token )
{
    this->token[0] = 0;

    // "https://ratatoskr.lovecka.info/app/api"
    char buff[20];
    strncpy( buff, url, 8 );
    buff[8] = 0;
    if( strcmp( "https://", buff ) == 0 ) {
        this->port = 443;
    } else {
        this->port = 80;
    }

    char *p = (char*)url + (this->port==80 ? 7 : 8);

    char *out = this->server;
    int l = 0;
    while(true) {
        char c = *p;
        if( c==0 ) {
            return;
        }
        if( c==':' ) {
            this->logger->log( "URL obsahuje port, to zatim neumim: %s", url );
            break;
        }
        if( c=='/' ) {
            break;
        }
        if( l < CP_URL_LEN-1 ) {
            *out = c;
            out++;
            l++;
        }
        p++;
    }
    *out = 0;

    out = this->path;
    l = 1;
    while(true) {
        char c = *p;
        if( c==0 ) {
            break;
        }
        if( l < CP_URL_LEN-1 ) {
            *out = c;
            out++;
            l++;
        }
        p++;
    }
    *out = 0;

    // oriznout pripadne lomitko na konci
    int delka = strlen( this->path );
    if( this->path[delka-1] == '/' ) {
        this->path[delka-1] = 0;
    }

    strncpy( this->token, token, CP_URL_LEN );
    this->token[CP_URL_LEN-1] = 0;

    this->logger->log( "Gallery server '%s':%d '%s' %s", 
                            this->server, 
                            this->port, 
                            this->path, 
                            this->port==80 ? "(plain http!)" : "" );

    //TODO: udelat SocketFactory?

    if( this->port == 443 ) {
        WiFiClientSecure * wificlsec = new WiFiClientSecure();
        wificlsec->setInsecure();
            // alternativa: 
            // wificlsec->setCACert(test_root_ca);
            // wificlsec->setCertificate(test_client_cert); // for client verification
            // wificlsec->setPrivateKey(test_client_key);	// for client verification        
        this->secureClient = (WiFiClient*)wificlsec;
    } else {
        this->secureClient = new WiFiClient();
    }

    // nastavit timeouty
    this->secureClient->setTimeout( 45000 );

}


/**
 * Načte headery z http responsu; true = 200 OK, false = cokoli jiného
 * 
 * Z ostatnich headeru hleda:
 *      X-Hash: f19fe4c53ea48c0f1a7f47f281dbe49640e0e7e8770ecd5feb841b23a082e786
 *      Content-Length: 55843
 *      Content-Type: application/octet-stream
 */
bool ImageSender::parseStatusAndHeaders()
{
    this->lastHttpStatus=-1;
    this->contentLength = 0;

    if( !this->secureClient->connected()) {
        this->logger->log( "not connected" );
        return false;
    }

    int i = 0;
    while (!this->secureClient->available()) {
        delay(10);
        i++;
        if(i>MAX_WAIT_FOR_SERVER) {
            this->logger->log( "zadna odpoved ze serveru" );
            return false;
        }
    }

    String line = this->secureClient->readStringUntil('\n');      

    //D/ this->logger->log( "status: %s", line.c_str() );
    // "HTTP/1.1 200 OK"
    if( line.length()>13 ) {
        this->lastHttpStatus = atoi( line.c_str() + 9 );
        strncpy( this->lastHttpStatusText, line.c_str()+13, CP_HTTP_STATUS_LEN );
        this->lastHttpStatusText[CP_HTTP_STATUS_LEN-1] = 0;
        // oriznout \r na konci
        int l = strlen(this->lastHttpStatusText);
        if( this->lastHttpStatusText[l-1] == '\r' ) {
            this->lastHttpStatusText[l-1] = 0;
        }

        if( this->lastHttpStatus!=200 ) {
            this->logger->log( "http status=%d, '%s'", 
                    this->lastHttpStatus, 
                    this->lastHttpStatusText );
            return false;;
        }            
    } else {
        this->logger->log( "nedokazu zpracovat status: '%s'", line.c_str() );
        return false;
    }
            
    char hdrContentLength[] = "Content-Length: ";

    while (this->secureClient->available()) {

        line = this->secureClient->readStringUntil('\n');

        if( line.startsWith(hdrContentLength) ) {
            this->contentLength = atol( line.c_str() + strlen(hdrContentLength) );
        } else {
            // ostatni hlavicky, ignorujeme
            //D/ this->logger->log( "# %s", line.c_str() );
        }

        if (line.c_str()[0]=='\r' || line.c_str()[0]==0 ) {
            //D/ this->logger->log("headers received");
            break;
        }
    }
    
    if( this->lastHttpStatus!=200 ) {
        // neprisly zadne hlavicky a vypadli jsme z while()
        return false;
    }

    return true;
}


/**
 * Zajistí napojení na server
 */
bool ImageSender::connectToServer() {
  
    //D/ this->logger->log("Starting connection to server...");
    if (!this->secureClient->connect( this->server, this->port )) {
        this->logger->log("Connection to server '%s' failed!", this->server );
        return false;
    } else {
        return true;
    }
}


/**
 * Vrátí true=povedlo se, false=nepodařilo se.
 */
bool ImageSender::sendImage( const unsigned char * image, int imageSize, const char * fileName ) {
    bool rc = true;

    //TODO: spocitat a poslat hash z obrazku?
    long start = millis();

    if( this->token[0]==0 ) {
        this->logger->log("ImageSender: not configured!");
        return false;
    }

    this->logger->log( "img send: %d byte, %s", imageSize, fileName );

    // spocist hash obrazku
    unsigned char hash[32];
	mbedtls_md_context_t ctx;
	mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;
	mbedtls_md_init(&ctx);
	mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 0);
	mbedtls_md_starts(&ctx);
    mbedtls_md_update(&ctx, (const unsigned char*)image, imageSize );  
    mbedtls_md_finish(&ctx, hash );
	mbedtls_md_free(&ctx);
    char hashBuf[70];
  	for(int i = 0; i < 32; i++)
	{
		sprintf( hashBuf+(2*i), "%02x", hash[i]);
	}      

    if( ! this->connectToServer() ) {
        return false;
    }

    // Make a HTTP request:
    this->secureClient->printf("POST %s/image/?name=%s HTTP/1.1\r\n", this->path, fileName );
    
    this->secureClient->printf("Host: %s\r\n", this->server );
    this->secureClient->printf("X-Auth: %s\r\n", this->token );
    this->secureClient->printf("Content-type: image/jpeg\r\n" );
    this->secureClient->printf("Content-length: %d\r\n", imageSize );
    this->secureClient->printf("X-Hash: %s\r\n", hashBuf );
    this->secureClient->println("Connection: close");
    this->secureClient->println();

    // poslat binarni data
/*
#define SEND_MAX_BLOCK 2048    
    unsigned char * ptr = (unsigned char *)image;
    int remain = imageSize;
    while(1) {
        int packSize = remain > SEND_MAX_BLOCK ? SEND_MAX_BLOCK : remain;
        int sent = this->secureClient->write( ptr, packSize );
        if( sent!=packSize ) {
            this->logger->log( "chyba pri odesilani" );
            rc = false;
            break;
        }
        remain -= packSize;
        ptr += packSize;
        this->logger->log( "req: %d, sent %d, remain %d", packSize, sent, remain );
        if( remain==0 ) {
            break;
        }
    }
*/    
    int sent = this->secureClient->write( image, imageSize );
    if( sent!=imageSize ) {
        this->logger->log( "chyba pri odesilani" );
        this->secureClient->stop();
        return false;
    }

    if( !this->parseStatusAndHeaders() ) {
        this->secureClient->stop();
        return false;
    }

    this->secureClient->stop();

    long t = millis() - start;
    this->logger->log( "img sent OK, %d ms", t );

    return rc;
}


