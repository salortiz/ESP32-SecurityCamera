/*******************************************************************************************************************
 *
 *        CameraWifiMotion
 *
 *        ESP32-Cam based security camera with motion detection, email, ftp and web server -  using Arduino IDE or PlatformIO
 *
 *                                   https://github.com/alanesq/CameraWifiMotion
 *                   Tested with ESP32 board manager version 1.0.6, WifiManager v1.7.4 (espressif32 v3.4.0 in platformio)
 *                   Use standard development board with PSRAM enabled and "minimal spiffs with ota" in partition table, otherwise OTA will not work
 *                   If using Esp33cam with it's motherboard you may need to lower the upload speed
 *
 *                   Included files: email.h, standard.h, ota.h, wifi.h
 *
 *             GPIO13 is used as an input pin for external sensors etc. (just reports status change at the moment)
 *             GPIO12 can be used for io but must be low at boot
 *             GPIO1 / 03 Used for serial port
 *
 *             IMPORTANT! - If you are getting weird problems (motion detection retriggering all the time, slow wifi
 *                          response times, random restarting - especially when using the LED) chances are there is a problem
 *                          the power to the board.  It needs a good 500ma supply and ideally a good sized smoothing
 *                          capacitor near the esp board in the order 3000uF.
 *
 *      Motion detection based on: https://eloquentarduino.github.io/2020/01/motion-detection-with-esp32-cam-only-arduino-version/
 *
 *      camera troubleshooting: https://randomnerdtutorials.com/esp32-cam-troubleshooting-guide/
 *
 *      ESP32 support for Arduino IDE: https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
 *
 *
 *      First time the ESP starts it will create an access point "ESPcam" which you need to connect to in order to
 *      enter your wifi details.  default password = "password"   (change this in net.h)
 *      see: https://randomnerdtutorials.com/wifimanager-with-esp8266-autoconnect-custom-parameter-and-manage-your-ssid-and-password
 *
 *
 *            Distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
 *                    implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 ********************************************************************************************************************/


#if (!defined ESP32)
    #error This sketch is for esp32cam only
#endif

#include <Arduino.h>        // required by PlatformIO
#include "esp_camera.h"     // https://github.com/espressif/esp32-camera

// ---------------------------------------------------------------
//                       - S E T T I N G S -
// ---------------------------------------------------------------
// You can use the following defines to activate de related feature
//#define EMAIL_ENABLED                                // Enable E-mail support
//#define FTP_ENABLED                                  // if ftp uploads are enabled

const char* stitle = "UrbIOTSecurityCamera";           // title of this sketch
const char* sversion = "230216";                   // version of this sketch
const short serialDebug = 1;                           // provide debug info on serial port
bool flashIndicatorLED = 1;                            // flash the onboard led when detection is enabled

#define ENABLE_OTA 1                                   // Enable Over The Air updates (OTA)

#if (defined POST_SERVER)
#define POST_ENABLED 1                                 // if POST uploads are enabled
#define _STR(x)  #x
#define _XSTR(x) _STR(x)
String PostServerName = _XSTR(POST_SERVER);            // The domain to upload to
int PostServerPort = 80;
#undef _XSTR(x)
#undef _STR(x)
#else
#error "No POST_SERVER DEFINED"
#endif

const char JPGX[] = ".jpg";
const bool ledON = HIGH;                               // Status LED control
const bool ledOFF = LOW;
const byte onboardLED = 33;                            // indicator LED
const int serialSpeed = 115200;                        // Serial data speed to use
const String HomeLink = "/";                           // Where home button on web pages links to (usually "/")
const uint16_t ServerPort = 80;                        // ip port to serve web pages on
const bool ledBlinkEnabled = 1;                        // enable blinking onboard status LED
const uint16_t ledBlinkRate = 1500;                    // Speed to blink the status LED (milliseconds) - also perform some system tasks
const uint16_t MaintCheckRate = 5;                     // how often to carry out routine system checks (seconds)
const byte gioPin = 13;                                // I/O pin (for external sensor input)


// camera related
// Note: to adjust other camera sensor settings see 'cameraImageSettings()' in 'motion.h'
#define IMAGE_SETTINGS 1                               // Implement adjustment of camera sensor settings

const int8_t MaxSpiffsImages = 6;                      // number of images to store in camera (Spiffs)
const uint32_t maxCamStreamTime = 20;                  // max camera stream can run for (seconds)
const uint16_t Illumination_led = 4;                   // illumination LED pin
const byte flashMode = 2;                              // 1=take picture using flash when dark, 2=use flash every time, 3=flash after capturing the image as display only
bool ioRequiredHighToTrigger = 0;                      // If motion detection only triggers if IO input is also high
int8_t cameraImageBrightness = 0;                      // image brighness (-2 to 2) - Note: has no effect?
int8_t cameraImageContrast = 0;                        // image contrast (-2 to 2) - Note: has no effect?
float thresholdGainCompensation = 0.65;                // motion detection level compensation for increased noise in image when gain increased (i.e. in darker conditions)

// ---------------------------------------------------------------
// forward declarations - many of these are only required by platformio
void handleRoot();
void handleData();
void handlePing();
void handleDefault();
void handleLive();
void handleCapture();
void handleImages();
void handleDisable();
void handleImagedata();
void handleBootLog();
void handleImg();
bool capturePhotoSaveSpiffs(bool dostream);
void RestartCamera(pixformat_t format);
void RebootCamera(pixformat_t format);
void saveJpgFrame(bool dostream);
void saveGreyscaleFrame(String filesName);
void ioDetected(bool iostat);
void MotionDetected(uint16_t changes);
void handleStream();
void handleStrPst();
void handleJPG();
void handleTest();
bool checkCameraIsFree();
// ---------------------------------------------------------------

// global variables / constants
bool wifiok = 0;                           // Flag if wifi is connected ok
bool SD_Present;                           // flag if an sd card was found (0 = no)
uint16_t dataRefresh = 5;                  // How often the updating info. on root page refreshes (seconds)
bool cameraImageInvert = 0;                // flip image  (i.e. upside down)
float cameraImageExposure = 0;             // Camera exposure (loaded from spiffs)
float cameraImageGain = 0;                 // Image gain (loaded from spiffs)
uint32_t TRIGGERtimer = 0;                 // used for limiting camera motion trigger rate
uint32_t EMAILtimer = 0;                   // used for limiting rate emails can be sent
byte DetectionEnabled = 0;                 // flag if motion detection is enabled (0=stopped, 1=enabled, 2=paused)
String TriggerTime = "Not yet triggered";  // Time of last motion detection as text
uint32_t MaintTiming = millis();           // used for timing maintenance tasks
bool emailWhenTriggered = 0;               // If emails will be sent when motion detected
bool ftpImages = 0;                        // if to FTP images up to server (ftp.h)
bool PostImages = 0;                       // if to send images via POST upload (post.h)
bool ReqLEDStatus = 0;                     // desired status of the illuminator led (i.e. should it be on or off when not being used as a flash)
uint16_t TriggerLimitTime = 2;             // min time between motion detection trigger events (seconds)
uint16_t EmailLimitTime = 60;              // min time between email sending (seconds)
bool UseFlash = 1;                         // use flash when taking a picture (1=yes)
bool SensorStatus = 1;                     // Status of the sensor i/o pin (gioPin)
bool OTAEnabled = 0;                       // flag if OTA has been enabled (via supply of password)
bool disableAllFunctions = 0;              // if set all functions other than web server are disabled
int16_t SpiffsFileCounter = 0;             // counter of last image stored

#include <SPIFFS.h>                        // spiffs used to store images and settings
#include <FS.h>                            // gives file access on spiffs
#include <SD_MMC.h>                        // sd card - see https://randomnerdtutorials.com/esp32-cam-take-photo-save-microsd-card/
#include <soc/soc.h>                       // Used to disable brownout detection
#include <soc/rtc_cntl_reg.h>
#include "net.h"                           // Load the Wifi / NTP stuff
void log_requested(String msg, WiFiClient client);
#include "standard.h"                      // Some standard procedures
#include "motion.h"                        // Include motion.h file for camera/motion detection code

Led statusLed1(onboardLED, LOW);             // set up onboard LED (LOW = on) - standard.h

#if ENABLE_OTA
    const String OTAPassword = "password";   // Password to enable OTA service (supplied as - http://<ip address>?pwd=xxxx )
    #include "ota.h"                         // Over The Air updates (OTA)
#endif

#ifdef EMAIL_ENABLED
    #define _SenderName "ESP"                // name of email sender (no spaces)
    #include "email.h"
#endif

#ifdef FTP_ENABLED
    #include "ftp.h"                         // Include ftp.h file for the ftp of captured images
#endif

#if POST_ENABLED
    #include "post.h"                        // Include php.h file for sending images via POST (can use a PHP script)
#endif


// ---------------------------------------------------------------
//                -check if camera is already in use
// ---------------------------------------------------------------
// try to avoid two processes capturing an image at the same time by using rudimentary flag
bool checkCameraIsFree() {
    int waitTimeout = 10;         // how long to wait if camera is in use
    if (DetectionEnabled != 2) return 1;
    log_system_message("Waiting to capture image as camera is already in use!");
    for (int i = 0; i < waitTimeout; i++) {
        delay(100);   if (DetectionEnabled != 2) return 1;
    }
    log_system_message("Waiting for camera to become free timed out");
    return 0;
}

// blink the led
void BlinkLed(byte Bcount) {
    for (int i = 0; i < Bcount; i++) {
        digitalWrite(Illumination_led, ledON);
        delay(50);
        digitalWrite(Illumination_led, ledOFF);
        delay(300);
    }
}

// ----------------------------------------------------------------
//              Load settings from text file in Spiffs
// ----------------------------------------------------------------
// read a line of text from Spiffs file and parse an integer from it
//     I realise this is complicating things for no real benefit but I wanted to learn to use pointers ;-)
static void ReadLineSpiffs(File* file, String* line, uint16_t* tnum) {
    File tfile = *file;
    String tline = *line;
    tline = tfile.readStringUntil('\n');
    *tnum = tline.toInt();
}

static void LoadSettingsSpiffs() {
    String TFileName = "/settings.txt";
    if (!SPIFFS.exists(TFileName)) {
        log_system_message("Settings file not found on Spiffs");
        return;
    }
    File file = SPIFFS.open(TFileName, "r");
    if (!file || file.isDirectory()) {
        log_system_message("Unable to open settings file from Spiffs");
        return;
    }

    log_system_message("Loading settings from Spiffs");

    // read contents of file
    String line;
    uint16_t tnum;

    ReadLineSpiffs(&file, &line, &tnum);      // ignore first line as it is just a title

    // line 1 - Block_threshold
    ReadLineSpiffs(&file, &line, &tnum);
    if (tnum < 1 || tnum > 255) log_system_message("invalid Block_threshold in settings");
    else Block_threshold = tnum;

    // line 2 - min Image_thresholdL
    ReadLineSpiffs(&file, &line, &tnum);
    if (tnum < 0 || tnum > 255) log_system_message("invalid min_day_image_threshold in settings");
    else Image_thresholdL = tnum;

    // line 3 - min Image_thresholdH
    ReadLineSpiffs(&file, &line, &tnum);
    if (tnum < 0 || tnum > 255) log_system_message("invalid max_day_image_threshold in settings");
    else Image_thresholdH = tnum;

    // line 4 - target brightness level
    ReadLineSpiffs(&file, &line, &tnum);
    if (tnum < 0 || tnum > 255) log_system_message("invalid night/night brightness cuttoff in settings");
    else targetBrightness = tnum;

    // line 5 - emailWhenTriggered
    ReadLineSpiffs(&file, &line, &tnum);
    if (tnum == 0) emailWhenTriggered = 0;
    else if (tnum == 1) emailWhenTriggered = 1;
    else log_system_message("Invalid emailWhenTriggered in settings: " + line);

    // line 6 - TriggerLimitTime
    ReadLineSpiffs(&file, &line, &tnum);
    if (tnum < 1 || tnum > 3600) log_system_message("invalid TriggerLimitTime in settings");
    else TriggerLimitTime = tnum;

    // line 7 - DetectionEnabled
    ReadLineSpiffs(&file, &line, &tnum);
    if (tnum == 2) tnum = 1;     // if it was paused restart it
    if (tnum == 0) DetectionEnabled = 0;
    else if (tnum == 1) DetectionEnabled = 1;
    else log_system_message("Invalid DetectionEnabled in settings: " + line);

    // line 8 - EmailLimitTime
    ReadLineSpiffs(&file, &line, &tnum);
    if (tnum < 60 || tnum > 10000) log_system_message("invalid EmailLimitTime in settings");
    else EmailLimitTime = tnum;

    // line 9 - UseFlash
    ReadLineSpiffs(&file, &line, &tnum);
    if (tnum == 0 || tnum == 1) UseFlash = (bool)tnum;
    else log_system_message("Invalid UseFlash in settings: " + line);

    // line 10 - SpiffsFileCounter
    ReadLineSpiffs(&file, &line, &tnum);
    if (tnum > MaxSpiffsImages) log_system_message("invalid SpiffsFileCounter in settings");
    else SpiffsFileCounter = tnum;

    // line 11 - cameraImageExposure
    ReadLineSpiffs(&file, &line, &tnum);
    if (tnum < 0 || tnum > 1200) log_system_message("invalid exposure in settings");
    else cameraImageExposure = tnum;

    // line 12 - cameraImageGain
    ReadLineSpiffs(&file, &line, &tnum);
    if (tnum > 31) log_system_message("invalid gain in settings");
    else cameraImageGain = tnum;

    // line 13 - tCounterTrigger
    ReadLineSpiffs(&file, &line, &tnum);
    if (tnum > 31) log_system_message("invalid consecutive detections in settings");
    else tCounterTrigger = tnum;

    // line 14 - ftpImages
    ReadLineSpiffs(&file, &line, &tnum);
    if (tnum == 0) ftpImages = 0;
    else if (tnum == 1) ftpImages = 1;
    else log_system_message("Invalid FTP in settings: " + line);

    // line 15 - PostImages
    ReadLineSpiffs(&file, &line, &tnum);
    if (tnum == 0) PostImages = 0;
    else if (tnum == 1) PostImages = 1;
    else log_system_message("Invalid Post in settings: " + line);

    // line 16 - cameraImageInvert
    ReadLineSpiffs(&file, &line, &tnum);
    if (tnum == 0) cameraImageInvert = 0;
    else if (tnum == 1) cameraImageInvert = 1;
    else log_system_message("Invalid cameraImageInvert in settings: " + line);

    // line 17 - dataRefresh
    ReadLineSpiffs(&file, &line, &tnum);
    if (tnum < 1 || tnum > 600) log_system_message("invalid dataRefresh in settings");
    else dataRefresh = tnum;

    // Detection mask grid
    bool gerr = 0;
    mask_active = 0;
    for (int y = 0; y < mask_rows; y++) {
        for (int x = 0; x < mask_columns; x++) {
            ReadLineSpiffs(&file, &line, &tnum);
            if (tnum == 1) {
                mask_frame[x][y] = 1;
                mask_active ++;
            }
            else if (tnum == 0) mask_frame[x][y] = 0;
            else gerr = 1;    // flag invalid entry
        }
    }
    if (gerr) log_system_message("invalid mask entry in settings");
    file.close();
}

// ----------------------------------------------------------------
//              Save settings to a text file in Spiffs
// ----------------------------------------------------------------
static void SaveSettingsSpiffs() {
    String TFileName = "/settings.txt";
    SPIFFS.remove(TFileName);   // delete old file if present
    File file = SPIFFS.open(TFileName, "w");
    if (!file) {
        log_system_message("Unable to open settings file in Spiffs");
        return;
    }

    // save settings in to file
    file.println("CameraWifiMotion settings file " + currentTime(1));   // title
    file.println(String(Block_threshold));
    file.println(String(Image_thresholdL));
    file.println(String(Image_thresholdH));
    file.println(String(targetBrightness));
    file.println(String(emailWhenTriggered));
    file.println(String(TriggerLimitTime));
    file.println(String(DetectionEnabled));
    file.println(String(EmailLimitTime));
    file.println(String(UseFlash));
    file.println(String(SpiffsFileCounter));
    file.println(String(cameraImageExposure));
    file.println(String(cameraImageGain));
    file.println(String(tCounterTrigger));
    file.println(String(ftpImages));
    file.println(String(PostImages));
    file.println(String(cameraImageInvert));
    file.println(String(dataRefresh));

    // Detection mask grid
    for (int y = 0; y < mask_rows; y++) {
        for (int x = 0; x < mask_columns; x++) {
            file.println(String(mask_frame[x][y]));
        }
    }
    file.close();
}

// ----------------------------------------------------------------
//                     Update boot log - Spiffs
// ----------------------------------------------------------------
// keeps a log of esp32 startups along with reasons why it was restarted
static void UpdateBootlogSpiffs(String Info) {
    if (serialDebug) Serial.println("Updating bootlog: " + Info);
    String TFileName = "/bootlog.txt";
    File file = SPIFFS.open(TFileName, FILE_APPEND);
    if (!file || file.isDirectory()) {
        log_system_message("Error: Unable to open boot log in Spiffs");
    } else {
        file.println(currentTime(1) + " - " + Info);       // add entry to log file
        file.close();
    }
}

// ----------------------------------------------------------------
//                       -Wipe/format Spiffs
// ----------------------------------------------------------------
static bool WipeSpiffs() {
    log_system_message("Formatting/Wiping Spiffs memory");

    if (!SPIFFS.format()) {
        log_system_message("Error: Unable to format Spiffs");
        return 0;
    }

    // Spiffs formatted ok
    SpiffsFileCounter = 0;
    TriggerTime = "Not since Spiffs wiped";
    UpdateBootlogSpiffs("Spiffs Wiped");                           // store event in bootlog file
    SaveSettingsSpiffs();                                          // save settings in Spiffs
    return 1;
}


// ---------------------------------------------------------------
//    -SETUP     SETUP     SETUP     SETUP     SETUP     SETUP
// ---------------------------------------------------------------
// setup section (runs once at startup)

void setup() {
    // status info. on serial port config
    if (serialDebug) {
        Serial.begin(115200);                              // serial port speed
        Serial.println("\n\n\n");                          // line feeds
        Serial.println("---------------------------------------");
        Serial.printf("Starting - %s - %s \n", stitle, sversion);
        Serial.println("---------------------------------------");
        // Serial.setDebugOutput(true);                       // enable extra diagnostic info on serial port
    }

    // Spiffs - see: https://circuits4you.com/2018/01/31/example-of-esp8266-flash-file-system-spiffs/
    if (!SPIFFS.begin(true)) {
        if (serialDebug) Serial.println("An Error has occurred while mounting SPIFFS - restarting");
        delay(5000);
        ESP.restart();                                // restart and try again
        delay(5000);
    } else {
        //delay(500);
        if (serialDebug) {
            Serial.print(("SPIFFS mounted successfully: "));
            Serial.printf("total bytes: %d , used: %d \n", SPIFFS.totalBytes(), SPIFFS.usedBytes());
        }
        LoadSettingsSpiffs();     // Load settings from text file in Spiffs
    }

    // sd card
    SD_Present = 0;
    if (!SD_MMC.begin("/sdcard", true)) {         // if loading sd card fails
        // note: ("/sdcard", true) = 1 wire - see: https://www.reddit.com/r/esp32/comments/d71es9/a_breakdown_of_my_experience_trying_to_talk_to_an/
        pinMode(2, INPUT_PULLUP);
        log_system_message("No SD Card detected");
    } else {
        uint8_t cardType = SD_MMC.cardType();
        if (cardType == CARD_NONE) {                // if no sd card found
            log_system_message("SD Card type detection failed");
        } else {
          uint32_t SDfreeSpace = (uint64_t)(SD_MMC.totalBytes() - SD_MMC.usedBytes()) / (1024 * 1024);
          log_system_message("SD Card found, free space = " + String(SDfreeSpace) + "MB");
          SD_Present = 1;                           // flag working sd card found
        }
    }
#if POST_ENABLED
    int pp = PostServerName.indexOf(":");
    if(pp != -1) {
        PostServerPort = PostServerName.substring(pp+1).toInt();
        PostServerName.remove(pp);
    }
    log_system_message("Will post images to " + PostServerName + ":" +String(PostServerPort));
#endif

    // configure the flash/illumination LED
    pinMode(Illumination_led, OUTPUT);
    digitalWrite(Illumination_led, ledOFF);

    BlinkLed(1);           // flash the led once

    // configure the gpio pin (with pullup resistor)
    pinMode(gioPin,  INPUT);

    // configure ondoard indicator led
    pinMode(onboardLED, OUTPUT);
    digitalWrite(onboardLED, HIGH);   // off

    startWifiManager();                      // Connect to wifi (procedure is in wifi.h)
    WiFi.mode(WIFI_STA);     // turn off access point - options are WIFI_AP, WIFI_STA, WIFI_AP_STA or WIFI_OFF

    // set up web page request handling
    server.on(HomeLink, handleRoot);         // root page
    server.on("/data", handleData);          // This displays information which updates every few seconds (used by root web page)
    server.on("/ping", handlePing);          // ping requested
    server.on("/disable", handleDisable);    // disable all functions other than web server
    server.on("/test", handleTest);          // testing page
    server.on("/default", handleDefault);    // All settings to defaults
    server.on("/live", handleLive);          // capture and display live image
    server.on("/capture", handleCapture);    // capture an image
    server.on("/images", handleImages);      // display images
    server.on("/img", handleImg);            // latest captured image
    server.on("/bootlog", handleBootLog);    // display boot log (stored in Spiffs)
    server.on("/imagedata", handleImagedata);// show raw image data
    server.on("/stream", handleStream);      // stream live image
    server.on("/strpst", handleStrPst);      // Post stream live image
    server.on("/jpg", handleJPG);            // display the greyscale motion detection image as a jpg

    server.on("/log", handleLogpage);        // system log
    server.onNotFound(handleNotFound);       // invalid page requested
    server.on("/reboot", handleReboot);      // reboot the esp
#if ENABLE_OTA
    server.on("/ota", handleOTA);          // OTA update page
#endif

    // Check psram
    if (!psramFound()) log_system_message("Warning: No PSRam detected - will limit size of images");

    // start web server
    if (serialDebug) Serial.println(("Starting web server"));
    server.begin();

    // set up camera
    bool tRes = setupCameraHardware(PIXFORMAT_GRAYSCALE);
    if (!tRes) {      // reboot camera
        delay(500);
        if (serialDebug) Serial.println("Problem starting camera - rebooting it");
        RestartCamera(PIXFORMAT_GRAYSCALE);                    // restart camera back to greyscale mode for motion detection
    } else {
        if (serialDebug) Serial.println(("Camera initialised ok"));
    }

    // Finished connecting to network
    BlinkLed(2);                             // flash the led twice
    log_system_message(String(stitle) + " Started");

    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);       // Turn-off the 'brownout detector'

    UpdateBootlogSpiffs("Booted");                   // store time of boot in bootlog

    TRIGGERtimer = millis();                         // reset the retrigger timer to stop instant triggering of motion detection

    log_system_message("Setup complete");
}

void log_requested(String msg, WiFiClient client) {
    // log page request including clients IP address
    IPAddress cip = client.remoteIP();
    String clientIP = decodeIP(cip.toString());   // get ip address and check if it is known
    log_system_message(msg + " requested from: " + clientIP);
}

// ----------------------------------------------------------------
//                reset back to default settings
// ----------------------------------------------------------------
// sets all settings to a standard default
void handleDefault() {
    WiFiClient client = server.client();          // open link with client
    log_requested("Test page", client);

    // default settings
    emailWhenTriggered = 0;
    targetBrightness = 130;
    Block_threshold = 7;
    Image_thresholdL= 15;
    Image_thresholdH= 192;
    TriggerLimitTime = 20;
    EmailLimitTime = 600;
    DetectionEnabled = 1;
    UseFlash = 0;
    cameraImageExposure = 30;
    cameraImageGain = 0;
    tCounterTrigger = 1;
    ftpImages = 0;
    PostImages = 0;

    // Detection mask grid
    for (int y = 0; y < mask_rows; y++)
        for (int x = 0; x < mask_columns; x++)
            mask_frame[x][y] = 1;
    mask_active = 12;

    SaveSettingsSpiffs();                      // save settings in Spiffs
    TRIGGERtimer = millis();                   // reset last image captured timer (to prevent instant trigger)

    String message = "reset to default";
    server.send(404, "text/plain", message);   // send reply as plain text
    message = "";      // clear string
}   // handle default

bool log_state(String msg, bool state) {
    log_system_message(msg + (state ? "en" : "dis") + "abled");
    return state;
}

// Action any user input on root web page
void rootButtons() {
    // email was clicked -  if an email is sent when triggered
    if (server.hasArg("email")) {
        if(log_state("Email when motion detected ", emailWhenTriggered = !emailWhenTriggered))
            EMAILtimer = 0;
        SaveSettingsSpiffs();     // save settings in Spiffs
    }

    // FTP was clicked -  FTP images when triggered
    if (server.hasArg("ftp")) {
        log_state("FTP when motion detected ", ftpImages = !ftpImages);
        SaveSettingsSpiffs();     // save settings in Spiffs
    }

    // Post was clicked -  send images via POST upload
    if (server.hasArg("post")) {
        log_state("POST when motion detected ", PostImages = !PostImages);
        SaveSettingsSpiffs();     // save settings in Spiffs
    }

   // if wipeS was entered  - clear Spiffs
    if (server.hasArg("wipeS")) {
        WipeSpiffs();        // format Spiffs
    }

//    // if wipeSD was entered  - clear all stored images on SD Card    (I do not know how to do this)
//      if (server.hasArg("wipeSD")) {
//        log_system_message("Clearing all stored images (SD Card)");
//        fs::FS &fs = SD_MMC;
//        fs.format();     // not a valid command
//      }

    // if target brightness was entered - targetBrightness
    if (server.hasArg("daynight")) {
        String Tvalue = server.arg("daynight");   // read value
        int val = Tvalue.toInt();
        if (val >= 0 && val < 256 && val != targetBrightness) {
          log_system_message("Target brightness changed to " + Tvalue );
          targetBrightness = val;
          SaveSettingsSpiffs();     // save settings in Spiffs
        }
    }

    // if dblockt was entered - Block_threshold
    if (server.hasArg("dblockt")) {
        String Tvalue = server.arg("dblockt");   // read value
        int val = Tvalue.toInt();
        if (val > 0 && val < 256 && val != Block_threshold) {
            log_system_message("Block_threshold changed to " + Tvalue );
            Block_threshold = val;
            SaveSettingsSpiffs();     // save settings in Spiffs
        }
    }

    // if dimagetl was entered - min-image_threshold
    if (server.hasArg("dimagetl")) {
        String Tvalue = server.arg("dimagetl");   // read value
        int val = Tvalue.toInt();
        if (val >= 0 && val < 192 && val != Image_thresholdL) {
            log_system_message("Min_day_image_threshold changed to " + Tvalue );
            Image_thresholdL = val;
            SaveSettingsSpiffs();     // save settings in Spiffs
        }
    }

    // if dimageth was entered - min-image_threshold
    if (server.hasArg("dimageth")) {
        String Tvalue = server.arg("dimageth");   // read value
        int val = Tvalue.toInt();
        if (val > 0 && val <= 192 && val != Image_thresholdH) {
            log_system_message("Max_day_image_threshold changed to " + Tvalue );
            Image_thresholdH = val;
            SaveSettingsSpiffs();     // save settings in Spiffs
        }
    }

#if IMAGE_SETTINGS
    // if exposure was adjusted - cameraImageExposure
    if (server.hasArg("exp")) {
        String Tvalue = server.arg("exp");   // read value
        if (Tvalue != NULL) {
            int val = Tvalue.toInt();
            if (val >= 0 && val <= 1200 && val != cameraImageExposure) {
              log_system_message("Camera exposure changed to " + Tvalue );
              cameraImageExposure = val;
              SaveSettingsSpiffs();         // save settings in Spiffs
              TRIGGERtimer = millis();      // reset last image captured timer (to prevent instant trigger)
            }
        }
    }

    // if image gain was adjusted - cameraImageGain
    if (server.hasArg("gain")) {
        String Tvalue = server.arg("gain");   // read value
        if (Tvalue != NULL) {
            int val = Tvalue.toInt();
            if (val >= 0 && val <= 31 && val != cameraImageGain) {
                log_system_message("Camera gain changed to " + Tvalue );
                cameraImageGain = val;
                SaveSettingsSpiffs();        // save settings in Spiffs
                TRIGGERtimer = millis();     // reset last image captured timer (to prevent instant trigger)
            }
        }
    }
#endif

    // updating info refresh rate - dataRefresh
    if (server.hasArg("refreshRate")) {
        String Tvalue = server.arg("refreshRate");   // read value
        if (Tvalue != NULL) {
            int val = Tvalue.toInt();
            if (val > 0 && val <= 600 && val != dataRefresh) {
                log_system_message("Date refresh rate changed to " + Tvalue );
                dataRefresh = val;
                SaveSettingsSpiffs();        // save settings in Spiffs
                TRIGGERtimer = millis();     // reset last image captured timer (to prevent instant trigger)
            }
        }
    }

//    // if image brightness was adjusted - cameraImageBrightness
//      if (server.hasArg("bright")) {
//        String Tvalue = server.arg("bright");   // read value
//          if (Tvalue != NULL) {
//            int val = Tvalue.toInt();
//            if (val >= -2 && val <= 2 && val != cameraImageBrightness) {
//              log_system_message("Camera brightness changed to " + Tvalue );
//              cameraImageBrightness = val;
//              SaveSettingsSpiffs();        // save settings in Spiffs
//              TRIGGERtimer = millis();     // reset last image captured timer (to prevent instant trigger)
//            }
//          }
//       }
//
//    // if image contrast was adjusted - cameraImageContrast
//      if (server.hasArg("cont")) {
//        String Tvalue = server.arg("cont");   // read value
//          if (Tvalue != NULL) {
//            int val = Tvalue.toInt();
//            if (val >= -2 && val <= 2 && val != cameraImageContrast) {
//              log_system_message("Camera contrast changed to " + Tvalue );
//              cameraImageContrast = val;
//              SaveSettingsSpiffs();        // save settings in Spiffs
//              TRIGGERtimer = millis();     // reset last image captured timer (to prevent instant trigger)
//            }
//          }
//       }


    // if cameraImageInvert was changed
    if (server.hasArg("submit")) {
        bool tStore = 0;
        if (server.hasArg("invert")) tStore = 1;
        if (tStore != cameraImageInvert) {     // value has changed
            cameraImageInvert = tStore;
            SaveSettingsSpiffs();
            log_system_message("Invert image changed to " + String(cameraImageInvert));
        }
    }

    // if mask grid check box array was altered
    if (server.hasArg("submit")) {                           // if submit button was pressed
        mask_active = 0;
        bool maskChanged = 0;                                  // flag if the mask has changed
        for (int y = 0; y < mask_rows; y++) {
            for (int x = 0; x < mask_columns; x++) {
                if (server.hasArg(String(x) + String(y))) {
                    // set to active
                    if (mask_frame[x][y] == 0) maskChanged = 1;
                    mask_frame[x][y] = 1;
                    mask_active ++;
                } else {
                    // set to disabled
                    if (mask_frame[x][y] == 1) maskChanged = 1;
                    mask_frame[x][y] = 0;
                }
            }
        }
        if (maskChanged) {
            Image_thresholdH = mask_active * blocksPerMaskUnit;      // reset max trigger setting to max possible
            SaveSettingsSpiffs();                                       // save settings in Spiffs
            log_system_message("Detection mask updated");
        }
    }

    // if emailtime was entered - min time between email sends
    if (server.hasArg("emailtime")) {
        String Tvalue = server.arg("emailtime");   // read value
        int val = Tvalue.toInt();
        if (val > 59 && val < 10000 && val != EmailLimitTime) {
            log_system_message("EmailLimitTime changed to " + Tvalue + " seconds");
            EmailLimitTime = val;
            SaveSettingsSpiffs();     // save settings in Spiffs
        }
    }

    // if triggertime was entered - min time between triggers
    if (server.hasArg("triggertime")) {
        String Tvalue = server.arg("triggertime");   // read value
        int val = Tvalue.toInt();
        if (val > 0 && val < 3600 && val != TriggerLimitTime) {
            log_system_message("Triggertime changed to " + Tvalue + " seconds");
            TriggerLimitTime = val;
            SaveSettingsSpiffs();     // save settings in Spiffs
        }
    }

      // if consecutive detections required - tCounterTrigger
    if (server.hasArg("consec")) {
        String Tvalue = server.arg("consec");   // read value
        int val = Tvalue.toInt();
        if (val > 0 && val <= 100 && val != tCounterTrigger) {
            log_system_message("Consecutive detections required changed to " + Tvalue);
            tCounterTrigger = val;
            SaveSettingsSpiffs();     // save settings in Spiffs
        }
    }

    // if button "toggle illuminator LED" was pressed
    if (server.hasArg("illuminator")) {
        // button was pressed
        checkCameraIsFree();                                // try to avoid using camera if already in use
        if (DetectionEnabled == 1) DetectionEnabled = 2;    // pause motion detecting to stop light triggering it (not required with single core esp32?)
        if (!ReqLEDStatus) {
            ReqLEDStatus = 1;
            digitalWrite(Illumination_led, ledON);
            log_system_message("Illuminator LED turned on");
        } else {
            ReqLEDStatus = 0;
            digitalWrite(Illumination_led, ledOFF);
            log_system_message("Illuminator LED turned off");
        }
        TRIGGERtimer = millis();                                // reset last image captured timer (to prevent instant trigger)
        if (DetectionEnabled == 2) DetectionEnabled = 1;        // re enable detection if it was paused
    }

    // if button "flash" was pressed  - toggle flash enabled
    if (server.hasArg("flash")) {
        log_state("Flash ", UseFlash = !UseFlash);
        SaveSettingsSpiffs();     // save settings in Spiffs
    }

    // if button "toggle motion detection" was pressed
    if (server.hasArg("detection")) {
        if(log_state("Motion detection ", DetectionEnabled = !DetectionEnabled)) {
            TRIGGERtimer = millis();                                // reset last image captured timer (to prevent instant
            TriggerTime = "Not since detection enabled";
        } else {
            digitalWrite(onboardLED, HIGH);   // indicator led off
        }
        SaveSettingsSpiffs();                                     // save settings in Spiffs
    }
}   // root buttons

// ----------------------------------------------------------------
//       -root web page requested    i.e. http://x.x.x.x/
// ----------------------------------------------------------------
void handleRoot() {
    WiFiClient client = server.client();                                                        // open link with client
    String tstr;                                                                                // temp store for building line of html
    webheader(client, "#stdLink:hover { background-color: rgb(180, 180, 0);}");                 // html page header  (with extra formatting)
    log_requested("Home page", client);
    rootButtons();                                                    // handle any user input from page
    // build the HTML code
    client.print("<FORM action='" + HomeLink + "' method='post'>\n");  // used by the buttons (action = the page send it to)
    client.write("<P>");                                               // start of section

    // ---------------------------------------------------------------------------------------------
    //  info which is periodically updated usin AJAX - https://www.w3schools.com/xml/ajax_intro.asp

    // empty lines which are periodically populated via vbscript with live data from http://x.x.x.x/data
    client.println("<span id='ul0'></span>");
    client.println("<br><span id='ul1'></span>");
    client.println("<br><span id='ul2'></span>");
    client.println("<br><span id='ul3'></span>");
    client.println("<br><span id='ul4'></span>");
    client.println("<br><span id='ul5'></span>");

    // Javascript - to periodically update the above info lines from http://x.x.x.x/data

    // get a comma seperated list from http://x.x.x.x/data and populate the blank lines in html above
    client.printf(R"=====(
<script>
  function getData() {
    var xhttp = new XMLHttpRequest();
    xhttp.onreadystatechange=function() {
        if (this.readyState==4 && this.status==200) {
          var rA=this.responseText.split(',');
          for (let i=0;i<rA.length;i++)
            document.getElementById('ul'+i).innerHTML = rA[i];
        }
    };
    xhttp.open('GET','data',true)
    xhttp.send()}
  getData()
  setInterval(getData,%d)
</script>
        )=====", dataRefresh * 1000);
    // ---------------------------------------------------------------------------------------------

    // capture and show a jpg image
    client.write("<br><br><a href='/jpg'>");         // make it a link
    client.write("<img id='im1' src='/jpg' width='320' height='240'/> </a>");     // show image from http://x.x.x.x/jpg

    // javascript to refresh the image periodically
    client.printf(R"=====(
<script>
  const rI=()=>{
    let ts=new Date().getTime(),
     qS='?t='+ts;
    document.getElementById('im1').src='/jpg'+qS;
  }
  setInterval(rI,%d);
</script>
    )=====", (dataRefresh * 1000) + 42);

    // detection mask check grid (right of screen)
    client.write( "<div style='float: right;'>Detection Mask<br>");
    for (int y = 0; y < mask_rows; y++) {
        for (int x = 0; x < mask_columns; x++) {
            client.printf("<input type='checkbox' name='%d%d' %s>\n", x, y, mask_frame[x][y] ? "checked " : "");
        }
        client.write("<BR>");
    }
    client.printf("<BR>%d active", mask_active);
    client.printf("<BR>(%d blocks)", (mask_active * blocksPerMaskUnit));
    client.write("</div>\n");

    // link to help/instructions page on github
    //client.printf("%s<a href='https://github.com/salortiz/ESP32-SecurityCamera/blob/master/README.md'>INSTRUCTIONS</a>%s\n", colBlue, colEnd);
      // <a href='#' id='stdLink' target='popup' onclick=\"window.open('https://github.com/alanesq/CameraWifiMotion/blob/master/readme.txt' ,'popup', 'width=600,height=480'); return false; \">INSTRUCTIONS</a>" + endcolour + " \n";

    // refresh rate of the data frame
    client.write("Above refreshes every "
        "<input type='number' style='width: 40px' name='refreshRate' title='Delay between data refresh on this page (in seconds)'");
    client.printf(" min='1' max='600' value='%d'>seconds\n", dataRefresh);

#if IMAGE_SETTINGS      // Implement adjustment of image settings
    client.write("<br>Set image exposure <input type='number' style='width: 50px' name='exp' min='0' max='1200' value=''>\n"
        " gain <input type='number' style='width: 30px' name='gain' min='0' max='30' value=''>\n"
        // Target brightness brightness cuttoff point
        "<BR>Auto image adjustment, target image brightness "
        "<input type='number' style='width: 40px' name='daynight' title='Brightness level system aims to maintain' min='0' max='255' ");
    client.printf("value='%d'>(0 = disabled)\n", targetBrightness);
#else
    targetBrightness = 0;
#endif
    // minimum seconds between triggers
    client.write("<BR>Minimum time between triggers ");
    client.printf("<input type='number' style='width: 40px' name='triggertime' min='1' max='3600' value='%d'>seconds \n", TriggerLimitTime);

    // consecutive detections required
    client.write(", Consecutive detections required to trigger "
        "<input type='number' style='width: 30px' name='consec' title='The number of changed images detected in a row required to trigger ");
    client.printf("motion detected' min='1' max='100' value='%d'>\n", tCounterTrigger);

#ifdef EMAIL_ENABLED
    // minimum seconds between email sends
    if (emailWhenTriggered) {
        client.write("<BR>Minimum time between E-mails:");
        client.printf("<input type='number' style='width: 60px' name='emailtime' min='60' max='10000' value='%d'>seconds \n", EmailLimitTime);
    }
#endif

    // detection parameters
    if (Image_thresholdH > (mask_active * blocksPerMaskUnit))
        Image_thresholdH = (mask_active * blocksPerMaskUnit);    // make sure high threshold is not greater than max possible
    client.write("<BR>Detection threshold <input type='number' style='width: 40px' name='dblockt' title='Brightness variation in block required ");
    client.printf("to count as changed (0-255)' min='1' max='255' value='%d'>, \n", Block_threshold);
    client.write("Trigger when between <input type='number' style='width: 40px' name='dimagetl' title='Minimum changed blocks in image required to count ");
    client.printf("as motion detected' min='0' max='%d' value='%d'>%\n", (mask_active * blocksPerMaskUnit), Image_thresholdL);
    client.write(" and <input type='number' style='width: 40px' name='dimageth' title='Maximum changed blocks in image required to count as motion ");
    client.printf("detected' min='1' max='%d' value='%d'>% blocks changed", (mask_active * blocksPerMaskUnit), Image_thresholdH);
    client.printf(" out of %d", (mask_active * blocksPerMaskUnit));

    // invert image check box
    client.printf("<br>Invert Image<input type='checkbox' name='invert' %s>\n", cameraImageInvert ? "checked " : "");
    client.write(
        // input submit button
        "&ensp; <input type='submit' name='submit'><BR>\n"
        // Toggle illuminator LED button
        "<input style='height: 30px;' name='illuminator' title='Toggle the Illumination LED On/Off' value='Light' type='submit'> \n"
        // Toggle 'use flash' button
        "<input style='height: 30px;' name='flash' title='Toggle use of flash when capturing image On/Off' value='Flash' type='submit'> \n"
        // Toggle motion detection
        "<input style='height: 30px;' name='detection' title='Motion detection enable/disable' value='Detection' type='submit'> \n");

#ifdef EMAIL_ENABLED
    // Toggle email when motion detection
    client.write("<input style='height: 30px;' name='email' value='Email' title='Send email when motion detected enable/disable' type='submit'> \n");
#endif

#ifdef FTP_ENABLED
    // toggle FTP
    client.write("<input style='height: 30px;' name='ftp' value='ftp' title='FTP images when motion detected enable/disable' type='submit'> \n");
#endif

#if POST_ENABLED
    // toggle POST
    client.write("<input style='height: 30px;' name='post' value='post' title='Send images via POST script when motion detected enable/disable' type='submit'> \n");
#endif

    // Clear images in spiffs
    client.write("<input style='height: 30px;' name='wipeS' value='Wipe Store' title='Delete all images stored in Spiffs' type='submit'> \n");

    // close html page
    client.write("</form>");             // buttons
    webfooter(client);                   // html page footer
    delay(3);
    client.stop();
}   // handle root


// ----------------------------------------------------------------
//     -data web page requested     i.e. http://x.x.x.x/data
// ----------------------------------------------------------------
//
//   This shows information on the root web page which refreshes every few seconds
void handleData(){
    // sd sdcard
    uint32_t SDusedSpace = 0;
    uint32_t SDtotalSpace = 0;
    uint32_t SDfreeSpace = 0;
    if (SD_Present) {
        SDusedSpace = SD_MMC.usedBytes() / (1024 * 1024);
        SDtotalSpace = SD_MMC.totalBytes() / (1024 * 1024);
        SDfreeSpace = SDtotalSpace - SDusedSpace;
    }

    String reply = "";

    // line0 - detection status
    if (DetectionEnabled) {
        reply += "Motion detection enabled: current motion detected is  " + String(latestChanges) + " changed blocks out of " + String(mask_active * blocksPerMaskUnit);
    } else {
        reply += "<font color='#FF0000'>Motion detection disabled</font>";
    }
    latestChanges = 0;        // reset stored motion values
    reply += ",";

    // line1 - motion last detected
    reply += "Motion detection last triggered: " + String(TriggerTime);
    reply += ",";

    // line2 - time/date
    reply += "Current time: " + currentTime(2);
    reply += ",";

    // line3 - image settings
    reply += "Image brightness: " + String(AveragePix);
    reply += " - Exposure: " + String((int)cameraImageExposure);
    reply += " - Gain: " +String((int)cameraImageGain);
    reply += ",";

    // line4 - sd card
    if (SD_Present) {
       reply += "SD Card: " + String(SDusedSpace) + "MB used - " + String(SDfreeSpace) + "MB free";
    }
     reply += ",";

    // line5 - misc info
    // Illumination LED / flash enabled
    if (digitalRead(Illumination_led) == ledON) reply += " {<font color='#FF0000'>Illumination LED is On</font>}&ensp;";
    if (UseFlash) reply += " {<font color='#FF0000'>Flash Enabled</font>}&ensp;";

    // OTA status
#if ENABLE_OTA
    if (OTAEnabled) reply += " {<font color='#FF0000'>OTA updates enabled</font>}&ensp;";
#endif

    // FTP status
#ifdef FTP_ENABLED
    if (ftpImages) reply += " {FTP enabled}&ensp;";
#endif

    // POST status
#if POST_ENABLED
    if (PostImages) reply += " {POST enabled}&ensp;";
#endif

     // email status
#ifdef EMAIL_ENABLED
    if (emailWhenTriggered) reply += " {<font color='#FF0000'>Email sending enabled</font>}&ensp;";
#endif

    //  if system disabled
    if (disableAllFunctions) reply += " {<font color='#FF0000'>ALL FUNCTIONS DISABLED</font>}&ensp;";

    server.send(200, "text/plain", reply); //Send millis value only to client ajax request
}

// ----------------------------------------------------------------
//      -ping web page requested     i.e. http://x.x.x.x/ping
// ----------------------------------------------------------------
void handlePing(){
    WiFiClient client = server.client();          // open link with client
    log_requested("Ping page", client);
    server.send(404, "text/plain", "ok");   // send reply as plain text
}

// ----------------------------------------------------------------
//   Capture and display -live image     i.e. http://x.x.x.x/live
// ----------------------------------------------------------------
// captures an image and displays it using the image view page
void handleLive(){
    WiFiClient client = server.client(); // open link with client
    log_requested("Live page", client);
    capturePhotoSaveSpiffs(false);      // capture an image from camera
    handleImages();                     // display captured image
}

// ----------------------------------------------------------------
//           -capture live image     i.e. http://x.x.x.x/capture
// ----------------------------------------------------------------
// captures an image and displays it using the image view page
void handleCapture(){
    WiFiClient client = server.client();    // open link with client
    log_requested("Capture image", client);
    server.send(404, "text/plain", "capturing live image");   // send reply as plain text
    capturePhotoSaveSpiffs(false);          // capture an image from camera
}

// ----------------------------------------------------------------
//    -Display captured images     i.e. http://x.x.x.x/images
// ----------------------------------------------------------------
// Image width in percent can be specified in URL with http://x.x.x.x/images?width=90
void handleImages(){

    WiFiClient client = server.client();
    log_requested("Stored image page", client);
    webheader(client, "#stdLink:hover { background-color: rgb(180, 180, 0);}");                 // html page header  (with extra formatting)
    String tstr;                                                                                // temp store for building lines of html

    uint16_t ImageToShow = SpiffsFileCounter;          // set current image to display when /img called
    uint16_t ImageWidthSetting = 90;                   // percentage of screen width to use for displaying the image

    // action any user input on page

    // if a image select button was pressed
    if (server.hasArg("button")) {
        String Bvalue = server.arg("button");   // read value
        int val = Bvalue.toInt();
        if (serialDebug) Serial.println("Button " + Bvalue + " was pressed");
        ImageToShow = val;     // select which image to display when /img called
    }

    // if a image width is specified in the URL    (i.e.  '...?width=')
    if (server.hasArg("width")) {
        String Bvalue = server.arg("width");       // read value
        uint16_t val = Bvalue.toInt();
        if (val >= 10 && val <= 100) {
            ImageWidthSetting = val;
        } else {
            log_system_message("Error: Invalid image width specified in URL: " + Bvalue);
        }
    }

    client.write("<FORM action='/images' method='post'>\n");               // used by the buttons (action = the page to send it to)
    client.write("<H1>Stored Images</H1>\n");
    // create the image selection buttons
    for(int i=1; i <= MaxSpiffsImages; i++) {
        client.write("<input style='height: 25px; ");
        if (i == ImageToShow) client.write("background-color: #0f8;");
        client.printf("' name='button' value='%d' type='submit'>\n", i);
    }

    // Insert image time info. from text file
    String TFileName = "/" + String(ImageToShow) + ".txt";
    File file = SPIFFS.open(TFileName, "r");
    if (!file || file.isDirectory()) {
        client.printf("%s<BR>File not found%s\n", colRed, colEnd);
    } else {
        String line = file.readStringUntil('\n');      // read first line of text file
        tstr = "<BR>" + line + "\n";
        client.print(tstr);
    }
    file.close();

    // button to show small version of image in popup window
    client.printf("%s<BR><a href='#' id='stdLink' target='popup' onclick=\"window.open('/img?pic=%d'", colBlue, (ImageToShow + 100));
    client.printf( ",'popup','width=320,height=240'); return false;\">PRE CAPTURE IMAGE</a>%s\n", colEnd);

    // insert image in to html
    client.printf("<BR><img id='img' alt='Camera Image' onerror='QpageRefresh();' width='%d%s' src='/img?pic=%d'>\n", ImageWidthSetting, "%", ImageToShow);

    // close html page
    client.write("</form>");                            // buttons
    webfooter(client);                                  // html page footer
    delay(3);
    client.stop();
}

// ----------------------------------------------------------------
//      -disable all functions     i.e. http://x.x.x.x/disable
// ----------------------------------------------------------------
void handleDisable(){
    WiFiClient client = server.client();          // open link with client
    log_requested("All functions disabled", client);
    disableAllFunctions = 1;
    server.send(404, "text/plain", "disabled!");   // send reply as plain text
}

// generate the html for a table cell
//    idat=the greyscale value, mactive=if in the active area of the mask
static String generateTD(uint16_t idat, bool mactive) {
    String bcol = String(idat, HEX);                                       // block color in hex  (for style command 'background-color: #fff;')
    if (bcol.length() == 1) bcol = "0" + bcol;                             // ensure string length = 2
    String ccolour = "background-color: #" + bcol + bcol + bcol;           // cell background color set to greyscale level
    // Text colour (html style)
    String ctext = "color: #00DD00";                          // default colour (green)
    if (mactive == 1) ctext = "color: #DD0000";               // if in an active frame (red)
    String html = "<td style='" + ccolour + "; " + ctext + ";'>" + String(idat) + "</td>";     // build the html for this cell
    return html;
}

// ---------------------------------------------------------------------
//   -Raw image data web page requested    i.e. http://x.x.x.x/imagedata
// ---------------------------------------------------------------------
// display the raw greyscale image block data
void handleImagedata() {
    WiFiClient client = server.client();          // open link with client
    log_requested("Raw date page", client);

    capture_still();         // capture current image
    webheader(client, "td {border: 1px solid grey; width: 30px; color: red;}");                // add the standard html header with some adnl style
    client.write("<P><br>RAW IMAGE DATA (Blocks) - Detection is "
        + DetectionEnabled ? "enabled" : "disabled");

    // show raw image data in html tables
    // difference between images table
    client.write("<BR><center>Difference<BR><table>\n");
    for (int y = 0; y < H; y++) {
        client.write("<tr>");
        for (int x = 0; x < W; x++) {
            uint16_t timg = abs(current_frame[y][x] - prev_frame[y][x]);
            bool mactive = block_active(x,y);    // is it active in the mask (0 or 1) - "block_active" is in motion.h
            client.write(generateTD(timg, mactive).c_str());
        }
        client.write("</tr>\n");
    }
    client.write("</table>");

    // current image table
    client.write("<BR><BR>Current Frame<BR><table>\n");
    for (int y = 0; y < H; y++) {
        client.write("<tr>");
        for (int x = 0; x < W; x++) {
            bool mactive = block_active(x,y);    // is it active in the mask (0 or 1)
            client.write(generateTD(current_frame[y][x], mactive).c_str());
        }
        client.write("</tr>\n");
    }
    client.write("</table>");

    // previous image table
    client.write("<BR><BR>Previous Frame<BR><table>\n");
    for (int y = 0; y < H; y++) {
        client.write("<tr>");
        for (int x = 0; x < W; x++) {
            bool mactive = block_active(x,y);    // is it active in the mask (0 or 1)
            client.write(generateTD(prev_frame[y][x], mactive).c_str());
        }
        client.write("</tr>\n");
    }
    client.write("</table></center>\n"
        "<BR>If detection is disabled the previous frame only updates when this page is refreshed, "
        "otherwise it automatically refreshes around twice a second\n"
        "<BR>Each block shown here is the average reading from 16x12 pixels on the camera image, "
        "The detection mask selection works on 4x4 groups of blocks\n"
        "<BR>\n");
    webfooter(client);                          // add standard footer html
    delay(3);
    client.stop();
    if (!DetectionEnabled) update_frame();      // if detection disabled copy this frame to previous
}

// ----------------------------------------------------------------
//   -bootlog web page requested    i.e. http://x.x.x.x/bootlog
// ----------------------------------------------------------------
// display boot log from Spiffs
void handleBootLog() {
    WiFiClient client = server.client();          // open link with client
    log_requested("Boot log page", client);
    webheader(client);                            // html page header

    // build the html for /bootlog page
    client.write("<P>\n<br>SYSTEM BOOT LOG<br><br>\n");
    // show contents of bootlog.txt in Spiffs
    File file = SPIFFS.open("/bootlog.txt", "r");
    if (!file || file.isDirectory()) {
        client.printf("%sNo Boot Log Available%s <BR>\n", colRed, colEnd);
    } else {
        String line;
        while(file.available()){
            line = file.readStringUntil('\n');      // read first line of text file
            client.print(line + "<BR>\n");
        }
    }
    file.close();
    client.write("<BR><BR>");
    // close html page
    webfooter(client);                                            // html page footer
    delay(3);
    client.stop();
}

// ----------------------------------------------------------------
//  last stored image page requested     i.e. http://x.x.x.x/img
// ----------------------------------------------------------------
// pic parameter on url selects which file to display
//   if this is (MaxSpiffsImages + 1) this means display of live greyscale image is required
void handleImg() {
    uint16_t ImageToShow = MaxSpiffsImages + 1;     // select live greyscale image as default

    // if a image to show is specified in url
    if (server.hasArg("pic")) {
        String Bvalue = server.arg("pic");
        ImageToShow = Bvalue.toInt();
        // if (ImageToShow < 0 || ImageToShow > (MaxSpiffsImages + 1)) ImageToShow = MaxSpiffsImages + 1;
    }
    if (ImageToShow == 0) ImageToShow = SpiffsFileCounter;   // set to most recent image

    String TFileName = "/" + String(ImageToShow) + JPGX;

    if (ImageToShow > 100) {               // show small image    e.g. 101 = '1s.jpg'
        ImageToShow = ImageToShow - 100;
        TFileName = "/" + String(ImageToShow) + "s.jpg";
    }

    if (ImageToShow == (MaxSpiffsImages + 1)) {           // live greyscale image requested ("grey")
        saveGreyscaleFrame("grey");                         // capture live greyscale image
        TFileName = "/grey.jpg";
    } else {
      log_system_message("Displaying stored image: " + String(ImageToShow));
    }

    // send image file
    File f = SPIFFS.open(TFileName, "r");                         // read file from spiffs
    if (!f) {
        if (serialDebug) {
          Serial.println("Error reading " + TFileName);
        }
    } else {
        size_t sent = server.streamFile(f, "image/jpeg");     // send file to web page
        if (!sent && serialDebug) {
            Serial.println("Error sending " + TFileName);
        }
        f.close();
    }
}

// ----------------------------------------------------------------
//        capture images - store in Spiffs/SD card and FTP
// ----------------------------------------------------------------
// check file saved to spiffs ok - by making sure file exists and is greater than 100 bytes
bool checkPhoto(fs::FS &fs, String IFileName) {
    File f_pic = fs.open( IFileName );
    uint16_t pic_sz = f_pic.size();
    bool tres = ( pic_sz > 100 );
    if (!tres) log_system_message("Error: Problem detected verifying image stored in Spiffs");
    f_pic.close();
    return (tres);
}

// ----------------------------------------------------------------
//              -restart the camera in different mode
// ----------------------------------------------------------------
// switches camera mode - format = PIXFORMAT_GRAYSCALE or PIXFORMAT_JPEG
void RestartCamera(pixformat_t format) {
    esp_camera_deinit();
    bool ok = setupCameraHardware(format);
    if (ok) {
        if (serialDebug) Serial.println("Camera mode switched ok");
    } else {
        // failed so try again
        esp_camera_deinit();
        delay(50);
        ok = setupCameraHardware(format); //esp_camera_init(&config);
        if (ok) {
            if (serialDebug) Serial.println("Camera mode switched ok - 2nd attempt");
        } else {
            UpdateBootlogSpiffs("Camera failed to restart so rebooting camera");        // store in bootlog
            RebootCamera(format);
        }
    }
    TRIGGERtimer = millis();        // reset last image captured timer (to prevent instant trigger)
}

bool capturePhotoSaveSpiffs(bool dostream) {
    checkCameraIsFree();                                                // try to avoid using camera if already in use
    if (DetectionEnabled == 1) DetectionEnabled = 2;                    // pause motion detecting while photo is captured (not required with single core esp32?)
    // increment image count
    SpiffsFileCounter++;
    if (SpiffsFileCounter > MaxSpiffsImages) SpiffsFileCounter = 1;   // reset counter
    //SaveSettingsSpiffs();     // save settings in Spiffs

    // first quickly grab a greyscale image
    saveGreyscaleFrame(String(SpiffsFileCounter) + "s");
    // Capture a high res image
    RestartCamera(PIXFORMAT_JPEG);      // restart camera in jpg mode to take a photo (uses greyscale mode for motion detection)

    bool ok = 0;          // Boolean to indicate if the picture has been taken correctly
    byte TryCount = 0;    // attempt counter to limit retries
    do {                  // try up to 3 times to capture/save image
        TryCount++;
        if (serialDebug)
            Serial.println("Taking a photo... attempt #" + String(TryCount));
        saveJpgFrame(dostream);                                 // capture and save/ftp image
        ok = checkPhoto(SPIFFS, "/" + String(SpiffsFileCounter) + JPGX);     // check if file has been correctly saved in SPIFFS
    } while ( !ok && TryCount < 3);                                            // if there was a problem taking photo try again

    RestartCamera(PIXFORMAT_GRAYSCALE);                                        // restart camera back to greyscale mode for motion detection

    TRIGGERtimer = millis();                                                   // reset retrigger timer to stop instant motion trigger
    if (DetectionEnabled == 2) DetectionEnabled = 1;                           // restart paused motion detecting

    if (!ok) log_system_message("Error: Unable to capture/store image");
    return (ok);
}

// reboot camera (used if camera is failing to respond)
//      restart camera in motion mode, capture a test frame to check it is now responding ok
//      format = PIXFORMAT_GRAYSCALE or PIXFORMAT_JPEG
void RebootCamera(pixformat_t format) {
    log_system_message("ERROR: Problem with camera detected so resetting it");
    // turn camera off then back on
    digitalWrite(PWDN_GPIO_NUM, HIGH);
    delay(200);
    digitalWrite(PWDN_GPIO_NUM, LOW);
    delay(400);
    RestartCamera(PIXFORMAT_GRAYSCALE);    // restart camera in motion mode
    delay(50);
    // try capturing a frame, if still problem reboot esp32
    if (!capture_still()) {
        UpdateBootlogSpiffs("Camera failed to reboot so rebooting esp32");    // store in bootlog
        delay(500);
        ESP.restart();
        delay(5000);      // restart will fail without this delay
    }
    if (format == PIXFORMAT_JPEG) RestartCamera(PIXFORMAT_JPEG);                  // if jpg mode required restart camera again
}

void sendStream(WiFiClient mclient) {
    camera_fb_t *fb = NULL;
    String Filename = currentTime(0) + "-L";            // file name for FTP/POST
    String result = "None yet";
    int frame_num = 0;
    log_system_message("Remote video stream started");
    uint32_t streamStop = (unsigned long)millis() + (maxCamStreamTime * 1000);              // time limit for stream
    while (millis() < streamStop) {
        fb = esp_camera_fb_get();
        if (!fb) {
            if (serialDebug) {
            Serial.println("Camera capture failed - rebooting camera");
            }
            RebootCamera(PIXFORMAT_JPEG);
            fb = esp_camera_fb_get();
        }
        if(fb) { // Success
            result = postImage(mclient, fb->buf, fb->len, Filename + String(++frame_num) + JPGX);
            esp_camera_fb_return(fb);
            if (result.indexOf("has been uploaded") == -1) break;
        } else {
            if (serialDebug) {
                Serial.println("Capture of image failed");
            }
            break;
        }
    }
    log_system_message("Remote video stream end:" + result);
}

// ----------------------------------------------------------------
//              Save jpg in spiffs/sd card and FTP/POST
// ----------------------------------------------------------------
// filesName = name of jpg to save as in spiffs
void saveJpgFrame(bool dostream = false) {
    String BaseFilename = currentTime(0) + "-L" + JPGX;                       // file name for FTP/POST

    // turn flash on if required
    if (UseFlash) {
        if (flashMode == 1 && cameraImageGain > 0) digitalWrite(Illumination_led, ledON);    // only if dark
        if (flashMode == 2) digitalWrite(Illumination_led, ledON);
    }
    // grab frame
    camera_fb_t *fb = esp_camera_fb_get();            // capture frame from camera
    if (!fb) {
        if (serialDebug)
          Serial.println("Camera capture failed - rebooting camera");
        RebootCamera(PIXFORMAT_JPEG);
        fb = esp_camera_fb_get();                       // try again to capture frame
    }

    if (UseFlash) {
        if (flashMode == 3)  { // flash after taking photo (i.e. if flashmode=3)
            digitalWrite(Illumination_led, ledON);
        } else { // turn flash off (if flashmode 3 do this after saving to spiffs to give a delay)
            digitalWrite(Illumination_led, ledOFF);
        }
    }

    if (fb) {        // only attempt to save the new images if one was captured ok
        String FileName = "/" + String(SpiffsFileCounter) + JPGX;      // file name for Spiffs
        // ------------------- save image to Spiffs -------------------
        SPIFFS.remove(FileName);                          // delete old image file if it exists
        File file = SPIFFS.open(FileName, FILE_WRITE);
        if (!file) {
            log_system_message("Failed to create file in Spiffs");
        } else {
            if (file.write(fb->buf, fb->len)) {
                if (serialDebug) {
                    Serial.print("The picture has been saved as ");
                    Serial.print(FileName);
                    Serial.print(" - Size: ");
                    Serial.print(file.size());
                    Serial.println(" bytes");
                }
            } else {
                log_system_message("Error: writing image to Spiffs...will format and try again");
                WipeSpiffs();     // format spiffs
                // reset image counter and name
                SpiffsFileCounter = 1;
                FileName = "/" + String(SpiffsFileCounter) + JPGX;      // file name for Spiffs
                file = SPIFFS.open(FileName, FILE_WRITE);
                if (!file.write(fb->buf, fb->len)) log_system_message("Error: Still unable to write image to Spiffs");
            }
            file.close();
        }
#ifdef SAVE_IFFS_TXT
        // save text file to spiffs with time info.
        FileName.replace("jpg", "txt");
        SPIFFS.remove(FileName);   // delete old file with same name if present
        file = SPIFFS.open(FileName, FILE_WRITE);
        if (!file) {
            log_system_message("Error: Failed to create date file in spiffs");
        } else {
            file.println(currentTime(1));
        }
        file.close();
#endif
        // turn flash off if using mode 3
        if (UseFlash && flashMode == 3) digitalWrite(Illumination_led, ledOFF);
        // ------------------- save image to SD Card -------------------
        FileName = "/" + BaseFilename;
        FileName.replace(":", "_");
        if (SD_Present) {
            // save image
            file = SD_MMC.open(FileName, FILE_WRITE);
            if (!file) {
                log_system_message("Error: Failed to create file on sd-card: " + FileName);
            } else {
                if (file.write(fb->buf, fb->len)) {
                    if (serialDebug) {
                        Serial.println("Saved image to sd card");
                    }
                } else {
                    log_system_message("Error: failed to save image to sd card");
                }
                file.close();
            }
        }
#ifdef FTP_ENABLED
        // ------------------ ftp images to server -------------------
        if (ftpImages) uploadImageByFTP(fb->buf, fb->len, BaseFilename);
#endif
#if POST_ENABLED
        if (PostImages) {
            WiFiClient aclient = WiFiClient();
            postImage(aclient, fb->buf, fb->len, BaseFilename);
            esp_camera_fb_return(fb);    // dispose frame so memory can be released
            if(dostream) sendStream(aclient);
            aclient.stop();
        }
#else
        esp_camera_fb_return(fb);    // dispose frame so memory can be released
#endif
    } else {
        if (serialDebug) {
            Serial.println("Capture of image failed");
        }
    }
}

// ----------------------------------------------------------------
//       Save greyscale frame as jpg in spiffs/sd card/FTP/POST
// ----------------------------------------------------------------
// filesName = name of jpg to save as in spiffs
void saveGreyscaleFrame(String filesName) {
    // grab greyscale frame
    uint8_t * _jpg_buf;
    size_t _jpg_buf_len;
    camera_fb_t * fb = NULL;    // pointer
    fb = esp_camera_fb_get();
    if (!fb) { // failed to capture frame
        log_system_message("error: failed to capture greyscale image");
        return;
    }
    // convert greyscale to jpg
    bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
    esp_camera_fb_return(fb);
    if (!jpeg_converted) {
        log_system_message("grey to jpg image conversion failed");
        return;
    }
    // save image to spiffs
    String FileName = "/" + filesName + JPGX;              // file name in spiffs
    SPIFFS.remove(FileName);                              // delete old image file if it exists
    File file = SPIFFS.open(FileName, FILE_WRITE);        // create new file
    if (!file) log_system_message("Error: creating grey file on Spiffs");
    else if (!file.write(_jpg_buf, _jpg_buf_len)) log_system_message("Error: writing grey image to Spiffs");
    file.close();

    String BaseFileName = currentTime(0) + "-S";
    // save image to sd card
    FileName = "/" + BaseFileName + JPGX;
    FileName.replace(":", "_");
    if (SD_Present) {
        file = SD_MMC.open(FileName, FILE_WRITE);
        if (!file) {
            log_system_message("Error: creating grey image on sd-card");
        } else {
            if (file.write(_jpg_buf, _jpg_buf_len)) {
                if (serialDebug) {
                    Serial.println("Saved grey image to sd card");
                }
            } else {
                log_system_message("Error: writing grey image to sd card");
            }
        }
        file.close();
    }
#ifdef FTP_ENABLED
    // ftp to server
    if (ftpImages) uploadImageByFTP(_jpg_buf, _jpg_buf_len, BaseFilename);
#endif
  heap_caps_free(_jpg_buf);                        // return jpg buffer memory
}

// ----------------------------------------------------------------
//                       -gpio input has triggered
// ----------------------------------------------------------------
void ioDetected(bool iostat) {
    checkCameraIsFree();                                                   // try to avoid using camera if already in use
    if (DetectionEnabled == 1) DetectionEnabled = 2;                       // pause motion detecting (not required with single core esp32?)

    log_system_message("IO input has triggered - status = " + String(iostat));

    // TODO
    // int capres = capturePhotoSaveSpiffs();                       // capture an image

    TRIGGERtimer = millis();                                               // reset retrigger timer to stop instant motion trigger
    if (DetectionEnabled == 2) DetectionEnabled = 1;                       // restart paused motion detecting
}

// ----------------------------------------------------------------
//                       -motion has been detected
// ----------------------------------------------------------------
void MotionDetected(uint16_t changes) {
    if(!checkCameraIsFree()) return;                                        // try to avoid using camera if already in use
    log_system_message("Camera detected motion: " + String(changes));
    TriggerTime = currentTime(0) + " - " + String(changes) + " out of " + String(mask_active * blocksPerMaskUnit);    // store time of trigger and motion detected
    int capres = capturePhotoSaveSpiffs(true);                              // capture an image

#ifdef EMAIL_ENABLED
    // send email if long enough since last motion detection (or if this is the first one)
    if (emailWhenTriggered) {       // add "&& cameraImageGain == 0" to only email during daylight hours (i.e. Chris's setting)
        unsigned long currentMillis = millis();        // get current time
        if ( ((unsigned long)(currentMillis - EMAILtimer) >= (EmailLimitTime * 1000)) || (EMAILtimer == 0) ) {
            EMAILtimer = currentMillis;    // reset timer

            // send an email
            _message[0]=0; _subject[0]=0;          // clear any existing text
            strcat(_subject,"ESPcamera");
            strcat(_message,"Camera triggered at ");
            String tt=currentTime(1);
            strcat(_message, tt.c_str());
            if (!capres) strcat(_message, "\nNote: there was a problem detected when capturing an image");
            sendEmail(_emailReceiver, _subject, _message);
        }
        else log_system_message("Too soon to send another email");
    }
#endif

    TRIGGERtimer = millis();                                       // reset retrigger timer to stop instant motion trigger
    if (DetectionEnabled == 2) DetectionEnabled = 1;               // restart paused motion detecting
}

// ----------------------------------------------------------------
//      -stream requested     i.e. http://x.x.x.x/stream
// ----------------------------------------------------------------
// Sends cam stream - thanks to Uwe Gerlach for the code showing how to do this
void handleStream() {
    WiFiClient client = server.client();          // open link with client
    char buf[32];
    int s;
    camera_fb_t * fb = NULL;

    // log page request including clients IP address
    IPAddress cip = client.remoteIP();
    String clientIP = decodeIP(cip.toString());   // get ip address and check if it is known
    log_system_message("Live stream page requested from: " + clientIP);

    // HTML used in the web page
    const char HEADER[] = "HTTP/1.1 200 OK\r\n" \
                        "Access-Control-Allow-Origin: *\r\n" \
                        "Content-Type: multipart/x-mixed-replace; boundary=123456789000000000000987654321\r\n";
    const char BOUNDARY[] = "\r\n--123456789000000000000987654321\r\n";           // marks end of each image frame
    const char CTNTTYPE[] = "Content-Type: image/jpeg\r\nContent-Length: ";       // marks start of image data
    const int hdrLen = strlen(HEADER);         // length of the stored text, used when sending to web page
    const int bdrLen = strlen(BOUNDARY);
    const int cntLen = strlen(CTNTTYPE);

    checkCameraIsFree();                                           // try to avoid using camera if already in use
    if (DetectionEnabled == 1) DetectionEnabled = 2;               // pause motion detecting while streaming (not required with single core esp32?)

    // send html header
    client.write(HEADER, hdrLen);
    client.write(BOUNDARY, bdrLen);

    RestartCamera(PIXFORMAT_JPEG);                // set camera in to jpeg mode

    // send live images until client disconnects or timeout
    uint32_t streamStop = (unsigned long)millis() + (maxCamStreamTime * 1000);              // time limit for stream
    while (millis() < streamStop )
    {
        if (!client.connected()) break;
        fb = esp_camera_fb_get();                   // capture live image frame
        s = fb->len;                                // store size of image (i.e. buffer length)
        client.write(CTNTTYPE, cntLen);             // send content type html (i.e. jpg image)
        sprintf( buf, "%d\r\n\r\n", s );            // format the image's size as html
        client.write(buf, strlen(buf));             // send image size
        client.write((char *)fb->buf, s);           // send the image data
        client.write(BOUNDARY, bdrLen);             // send html boundary      see https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Content-Type
        esp_camera_fb_return(fb);                   // return frame so memory can be released
    }

    // close client connection
    log_system_message("Video stream stopped");
    delay(3);
    client.stop();

    RestartCamera(PIXFORMAT_GRAYSCALE);                    // restart camera back to greyscale mode for motion detection
    TRIGGERtimer = millis();                               // reset retrigger timer to stop instant motion trigger
    if (DetectionEnabled == 2) DetectionEnabled = 1;       // restart paused motion detecting
}

void handleStrPst() {
    WiFiClient client = server.client();          // open link with client
    // log page request including clients IP address
    IPAddress cip = client.remoteIP();
    String clientIP = decodeIP(cip.toString());   // get ip address and check if it is known
    log_system_message("Stream post requested from: " + clientIP);
    checkCameraIsFree();
    if (DetectionEnabled == 1) DetectionEnabled = 2;
    RestartCamera(PIXFORMAT_JPEG);
    String message = "Streaming...";
    server.send(404, "text/plain", message);   // send reply as plain text
    WiFiClient mclient = WiFiClient();
    sendStream(mclient);
    mclient.stop();
    RestartCamera(PIXFORMAT_GRAYSCALE);                    // restart camera back to greyscale mode for motion detection
    TRIGGERtimer = millis();                               // reset retrigger timer to stop instant motion trigger
    if (DetectionEnabled == 2) DetectionEnabled = 1;       // restart paused motion detecting
}

// ----------------------------------------------------------------
// -show motion detection frame as a JPG      i.e. http://x.x.x.x/jpg
// ----------------------------------------------------------------
void handleJPG() {
    if (checkCameraIsFree() == 0) return;         // try to avoid using camera if already in use

    WiFiClient client = server.client();          // open link with client
    char buf[32];
    uint8_t *jpg_buf;
    size_t jpg_size = 0;

    // capture a frame (greyscale)
    camera_fb_t *fb = esp_camera_fb_get();      // capture frame
    if (!fb) {
        log_system_message("error: failed to capture image");
        //RebootCamera(PIXFORMAT_GRAYSCALE);
        return;
    }

    // convert greyscale to JPG
    //fmt2jpg(fb->buf, fb->len, fb->width, fb->height, fb->format, 31, &jpg_buf, &jpg_size);
    frame2jpg(fb, 31, &jpg_buf, &jpg_size);
    if (serialDebug) Serial.printf("Converted JPG size: %d bytes \n", jpg_size);

    // build and send html
    const char HEADER[] = "HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\n";
    const char CTNTTYPE[] = "Content-Type: image/jpeg\r\nContent-Length: ";
    const int hdrLen = strlen(HEADER);         // length of the stored text, used when sending to web page
    const int cntLen = strlen(CTNTTYPE);
    client.write(HEADER, hdrLen);
    client.write(CTNTTYPE, cntLen);

    // put the length in to 'buf' char array and send
    sprintf( buf, "%d\r\n\r\n", jpg_size);
    client.write(buf, strlen(buf));

    // send the jpg data
    client.write((char *)jpg_buf, jpg_size);

    // close connection
    delay(3);
    client.stop();

    heap_caps_free(jpg_buf);                        // return jpg buffer memory
    esp_camera_fb_return(fb);                       // return greyscale buffer
}

// ----------------------------------------------------------------
//           -testing page     i.e. http://x.x.x.x/test
// ----------------------------------------------------------------
void handleTest(){
    WiFiClient client = server.client();          // open link with client
    log_requested("Test page", client);

    webheader(client);                 // add the standard html header
    client.write("<br>TEST PAGE<br><br>\n");

    // ---------------------------- test section here ------------------------------

    //// demo of how to send a sms message via GSM board (phone number is a String in the format "+447871862749")
    //  sendSMS(phoneNumber, "this is a test message from the gsm demo sketch");

    //// demo of how to request a web page over Wifi
    //  String webpage = requestWebPage("86.3.1.8","/ping",80,800,"");

    //// demo of how to request a web page over GSM
    //    requestWebPageGSM("alanesq.eu5.net", "/temp/q.txt", 80);

#ifdef EMAIL_ENABLED
    client.write("<br>Sending test email<br>\n");
    // send a test email
    _message[0]=0; _subject[0]=0;          // clear any existing text
    strcat(_subject, stitle);
    strcat(_message, "test email");
    sendEmail(_emailReceiver, _subject, _message);
#endif
    // end html page
    webfooter(client);            // add the standard web page footer
    delay(1);
    client.stop();
}

// Auto image adjustment
//   runs every few seconds, called from loop
static void AutoAdjustImage() {
    if(cfsize != FRAME_SIZE_MOTION)
        return;
    float exposureAdjustmentSteps = (cameraImageExposure / 25) + 0.2;    // adjust by higher amount when at higher level (25 / 0.2 = default)
    float gainAdjustmentSteps = 0.5;
    float hyster = 20.0;                                                 // Hysteresis on brightness level
    if (AveragePix > (targetBrightness + hyster)) {
        // too bright
        if (cameraImageGain > 0) {
            cameraImageGain -= gainAdjustmentSteps;
        } else {
            cameraImageExposure -= exposureAdjustmentSteps;
        }
    }
    if (AveragePix < (targetBrightness - hyster)) {    // if too dark
        if (cameraImageExposure >= 1200) {
            cameraImageGain += gainAdjustmentSteps;
        } else {
            cameraImageExposure += exposureAdjustmentSteps;
        }
    }
    // check for over scale
    if (cameraImageExposure < 0) cameraImageExposure = 0;
    if (cameraImageExposure > 1200) cameraImageExposure = 1200;
    if (cameraImageGain < 0) cameraImageGain = 0;
    if (cameraImageGain > 30) cameraImageGain = 30;
    cameraImageSettings(FRAME_SIZE_MOTION);      // apply camera sensor settings
    capture_still();                             // update stored image with the changed image settings to prevent trigger
    update_frame();
}  // autoadjustimage

// ----------------------------------------------------------------
//   -LOOP     LOOP     LOOP     LOOP     LOOP     LOOP     LOOP
// ----------------------------------------------------------------
void loop(void){
    server.handleClient();                                                                    // service any web requests
    if (disableAllFunctions) return;                                                          // if device is disabled
#if ENABLE_EMAIL
    EMAILloop();                                                                              // handle emails
#endif

    // camera motion detection
    if (DetectionEnabled == 1) {
        if (!capture_still()) RebootCamera(PIXFORMAT_GRAYSCALE);                              // capture image, if problem reboot camera and try again
        uint16_t changes = motion_detect();                                                   // find amount of change in current image frame compared to the last one
        update_frame();                                                                       // Copy current stored frame to previous stored frame
        if ( (changes >= Image_thresholdL) && (changes <= Image_thresholdH) ) {               // if enough change to count as motion detected
            if (tCounter >= tCounterTrigger) {                                                // only trigger if movement detected in more than one consequitive frames
                tCounter = 0;
                if ((unsigned long)(millis() - TRIGGERtimer) >= (TriggerLimitTime * 1000) ) { // limit time between triggers
                    TRIGGERtimer = millis();                                                  // update last trigger time
                    // run motion detected procedure (blocked if io high is required)
                    if (ioRequiredHighToTrigger == 0 || SensorStatus == 1) {
                        MotionDetected(changes);
                    } else {
                        log_system_message("Motion detected but io input low so ignored");
                    }
                } else {
                    if (serialDebug) {
                        Serial.println("Too soon to re-trigger");
                    }
                }
            } else {
                if (serialDebug) {
                    Serial.println("Not enough consecutive detections");
                }
            }
        }
    }

    // log when sensor i/o input pin status changes
    bool tstatus = digitalRead(gioPin);
    if (tstatus != SensorStatus) {
        delay(20);
        tstatus = digitalRead(gioPin);        // debounce input
        if (tstatus != SensorStatus) {
            // input pin status has changed
            ioDetected(SensorStatus = tstatus);
        }
    }

    // periodic system tasks
    if ((unsigned long)(millis() - MaintTiming) >= (MaintCheckRate * 1000) ) {
        if (DetectionEnabled && flashIndicatorLED) digitalWrite(onboardLED, !digitalRead(onboardLED));  // flash onboard indicator led
        WIFIcheck();                                        // check if wifi connection is ok
        MaintTiming = millis();                             // reset system tasks timer
        time_t t=now();                                     // read current time to ensure NTP auto refresh keeps triggering (otherwise only triggers when time is required causing a delay in response)
        // check status of illumination led is correct
        if (ReqLEDStatus) {
            digitalWrite(Illumination_led, ledON);
        } else {
            digitalWrite(Illumination_led, ledOFF);
        }
        if (DetectionEnabled == 0) capture_still();         // capture a frame to get a current brightness reading
        if (targetBrightness > 0) AutoAdjustImage();        // auto adjust image sensor settings
    }
} // loop
// --------------------------- E N D -----------------------------
