/* ----- CHECKLIST -----
Office or Home devices
- different "measurement" (database table) in InfluxDB Cloud
- TS_HOME_01  'i' is not needed for battery report. It was reported as float at the very first case :-((((
- TS_HOME_02  must be defined as the battery monitor will show negative numbers

- Config files: ini / js
  - Network config
  - Device ID
  - BME Sensor correction (+0.8 for TS_HOME_02)
  - Display contrast (Home: 137 | Office: 127 )

- DO NOT FORGET TO SYNC the ini file with the js file
*/

/* CHECKLIST DEFINES */
//#define TS_HOME_02 true


//#ifdef DEBUG_ESP_PORT
//#define DEBUG_MSG(...) DEBUG_ESP_PORT.printf( __VA_ARGS__ )
//#else
//#define DEBUG_MSG(...)
//#endif

//#define DEBUG_ESP_PORT Serial


//-- ESP8266 Fundamentals
#include <Arduino.h>
#include <pins_arduino.h>

//-- Network and web server
#include <ESP8266mDNS.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

//-- Sensor related
#define SENSOR_BME280

#ifdef SENSOR_BME280
  // BME280 Sensor (Temperature, Humidity and Air pressure )
  #include <BME280I2C.h>
  #include <Wire.h>
#else
  #include <AHT10.h> // AHT10 (Sensor Temperature and Humidty)
#endif


//-- Logging
// #define GSI_DEBUG
#include <GSiDebug.h>

//-- Screen and font management
#include <U8g2lib.h>

//-- SPIFFS for file management and ini file
#include "FS.h"
#include <SPIFFSIniFile.h> //-- in the header the inline open must be changed for using LittleFS.open() !
#include "LittleFS.h"  

//-- ini and js file conent definitions
//#include "sensor_config_file_content.h"

#include "sensor_config_file_management.h"
#include "sensor_ini_file_storage.h"

#include "web_config_management.h"

sensor::SensorIniFileStorage g_iniStorage;
sensor::WebConfigManagement g_webConfMan;

//-- WatchDog to avoid infinite data sending
#include <Ticker.h>

//-- OTA --------------------------------
#include <ArduinoOTA.h>


/*
Function cycle
  1. Wake-up
  2. Connect to Wi-Fi
     Screen update
  3. Read Sensor data
     Screen update
  4. Connect to Server
     Screen update
  5. Submit sensor data
     Screen update
     OK / NOK
  6. Go to deep sleep
*/

//-- OFFICE VERSION + BUTTON
// WEMOS D1 Mini
// -- NOKIA5110 Screen
// PIN_DC  D3 NOK, D4 NOK
#define PIN_CS    D4 //D4
#define PIN_DC    D8 //D8 is LOW During DeepSleep. If it is HIGH the Screen is OFF
#define PIN_DATA  D2
#define PIN_CLOCK D1
// -- BME280
#define BME280_SCL D6
#define BME280_SDA D5
// -- BUTTON
#define BTN_CONFIG D7

//U8G2_PCD8544_84X48_1_4W_SW_SPI(rotation, clock, data, cs, dc [, reset]);
U8G2_PCD8544_84X48_1_4W_SW_SPI  u8g2(U8G2_R2, PIN_CLOCK, PIN_DATA, PIN_CS, PIN_DC);
////U8G2_PCD8544_84X48_1_4W_SW_SPI  u8g2(U8G2_R0, PIN_CLOCK, PIN_DATA, PIN_CS, PIN_DC); // R2

// Rotation: U8G2_R2 180o or U8G2_R0 No rotation

#ifdef SENSOR_BME280
  //-- BME820 ----------------------------------------------------------------------------------------
  BME280I2C::Settings settings(   BME280::OSR_X1,
    BME280::OSR_X1,
    BME280::OSR_X1,
    BME280::Mode_Forced,
    BME280::StandbyTime_1000ms,
    BME280::Filter_Off,
    BME280::SpiEnable_False,
    BME280I2C::I2CAddr_0x76 // I2C address. I2C specific.
  );

  BME280I2C g_bme(settings);
  

#else
  //-- AHT10 -----------------------------------------------------------------------------------------
  AHT10 sensorAHT10( AHT10_ADDRESS_0X38 );
#endif

//-- To store different sensor values
float g_temp(0), g_hum(0), g_pres(0);
  int16_t g_battery = 100;

  uint64_t g_timeStamp = 0;
  char g_txTemprD[4] = "-00";
  char g_txTemprR[3] = ".0";
  char g_txHumid[8]  = "RH 100%";

struct DataReportValues //-- keeps the collection of the data to report
{
  const char *deviceId;
  const char *location;
  float tempr = 0.0;
  float humid = 0.0;
  float press = 0.0;
  int16_t battery = 100;
  uint64_t timeStamp = 0;
  // uptime is calculated on the fly by the influx::submitData function

  DataReportValues(const char *deviceId, const char* location);
};


DataReportValues::DataReportValues(const char *deviceId, const char* location):deviceId(deviceId),
  location(location)   {}

//-- ICON DISPLAY FIELDS ---------------------------------------------------------------------------
typedef union //-- Maintaining what to update on screen
{
  struct
  {
    unsigned wifi           : 1;
    unsigned inet           : 1;
    unsigned upload         : 1;
    unsigned like           : 1;
    unsigned dislike        : 1;
    unsigned pclosed        : 1;
    unsigned popen          : 1;
    unsigned padding        : 1;
  } fields;
  uint8_t allFields;
} DisplayIcons;

DisplayIcons g_dispIcons;

const char ICON_DISLIKE[2] = "\x52";
const char ICON_LIKE[2]    = "\x49";
const char ICON_UPLOAD[2]  = "\x43";
const char ICON_INET[2]    = "\x4E";
const char ICON_WIFI[2]    = "\x51";
const char ICON_PCLOSED[2] = "\x4F";
const char ICON_POPEN[2]   = "\x44";

const uint8_t MASK_ICON_CLEAR = 0;

//-- FORWARD DECLARATIONS --------------------------------------------------------------------------
void screenV2();
void drawScreen();
void handleTickerUploadTimeout();

//-- NETWORK RELATED -------------------------------------------------------------------------------
const char AP_SSID[] = "ESP-ThermoSensor";
const char AP_PWD[]  = "11223344";

const uint8_t SCREEN_UPDATE_TIME = 54; // 54 ms

//-- Global variables ------------------------------------------------------------------------------
bool g_isInSetupMode = false;
Ticker g_uploadTimeOutTicker;
Ticker g_batLevelTicker;



//== NETWORK ======================================================================================
WiFiEventHandler wifiStaConnectHandler;

//-- onSTAConnected ---------------------------------------------------------------------------------
void onSTAConnected(const WiFiEventSoftAPModeStationConnected &w)
{
  SERIAL_PLN("Client connected.");
  SERIAL_PF( "Connection id: %d\n", w.aid );
  SERIAL_PF( "Client MAC address: %x-%x-%x-%x-%x-%x\n", w.mac[0], w.mac[1], w.mac[2], w.mac[3], w.mac[4], w.mac[5]);
}

// -- connectToWiFiV3 ------------------------------------------------------------------------------
// Connects to the known WiFi AP
// If the BSSID is known let's try to connect to it. If it is not known, use just the SSID and PWD
// If connecting to the AP is successful, the BSSID is saved with the channel -> ConfigChanged -> true
// If connecting to the AP is usuccessful, clear the BSSID and the channel -> ConfigChanged -> true
// return false if connection is unsuccessful / true if successful
bool connectToWiFiV4(sensor::SensorIniFileStorage& senConf, bool &isConfigChanged )
{
  isConfigChanged = false;

  WiFi.persistent( true );
  SERIAL_PF("\n\nconnecting to :%s", senConf.wifi_ap_ssid);
  WiFi.mode(WIFI_STA);
  yield();

  uint16_t bssid_check = 0;
  for ( uint8_t i = 0; i < 6; ++i ) { bssid_check += senConf.wifi_ap_bssid[i]; }

  if ( 0 == bssid_check )
  { // no known BSSID
    SERIAL_P(" (No BSSID) ");
    WiFi.begin( senConf.wifi_ap_ssid, senConf.wifi_ap_pwd );
    //iniFileStorage.wifi_con_delay = iniFileStorage.wifi_con_delay * 2;
    //  60 attempts is the minimum with 150 ms delay
    if ( 240 > senConf.wifi_max_con_attempts ) { senConf.wifi_max_con_attempts += 60; }
    else { senConf.wifi_max_con_attempts = 240; }
    SERIAL_PF("Con_attempts: %d; Con_delays: %d\n", senConf.wifi_max_con_attempts, senConf.wifi_con_delay);
    
    //iniFileStorage.wifi_max_con_attempts = iniFileStorage.wifi_max_con_attempts * 2;
  }
  else
  {
    SERIAL_P(" (BSSID known) ");
    WiFi.begin(senConf.wifi_ap_ssid, 
               senConf.wifi_ap_pwd, 
               senConf.wifi_ap_channel, 
               senConf.wifi_ap_bssid, 
               true);
  }


  // display the wifi icon
  g_dispIcons.fields.wifi = true;
  drawScreen();

  uint16_t wifiConAttCntr = senConf.wifi_max_con_attempts;
  while (WiFi.status() != WL_CONNECTED) 
  {
    delay( senConf.wifi_con_delay - 54 );
    --wifiConAttCntr;
    if ( 0 == wifiConAttCntr ) 
    { 
      memset(senConf.wifi_ap_bssid, 0, sizeof( uint8_t) * 6 ); // clean out the BSSID
      senConf.wifi_ap_channel = 0;
      isConfigChanged = true;
      
      return false; 
    }
    SERIAL_P( F(".") );

    //-- blink Wifi sign
    g_dispIcons.fields.wifi = !g_dispIcons.fields.wifi;
    drawScreen();
  }

  g_dispIcons.fields.wifi = true;
  drawScreen();

  SERIAL_PF("\nWiFi connected. IP address: %s\n", WiFi.localIP().toString().c_str() );
  if ( 0 == bssid_check )
  {
    isConfigChanged = true;

    const uint8_t *mac = WiFi.BSSID();
    memcpy( senConf.wifi_ap_bssid, mac, sizeof( uint8_t ) * 6 );
    SERIAL_PF("wifi_ap_bssid: %x-%x-%x-%x-%x-%x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // iniFileStorage.wifi_con_delay /= 2;
    // iniFileStorage.wifi_max_con_attempts /= 2;
  }

  if ( WiFi.channel() != senConf.wifi_ap_channel ) // The channel value has changed
  { 
    isConfigChanged = true; 
    senConf.wifi_ap_channel = WiFi.channel();
  }

  SERIAL_PF("AP-BSSID: %s\n", WiFi.BSSIDstr().c_str() );
  SERIAL_PF("AP Channel: %d\n", WiFi.channel() );
  SERIAL_PF("Con_attempts: %d; Con_delays: %d\n", senConf.wifi_max_con_attempts, senConf.wifi_con_delay);

  return true;
}


//-- WEBSERVER Management --------------------------------------------------------------------------
ESP8266WebServer server(80);    // Create a webserver object that listens for HTTP request on port 80
const char NOT_FOUND[] = " not found\n";





namespace influx
{
  struct DataReportConfig
  {
    const char *server_address = 0;
    uint16_t server_port = 0;
    const char *server_auth_token = 0;
    const char *data_org = 0;
    const char *data_bucket = 0;
    const char *data_measurement_name = 0;
    
    DataReportConfig( const char *srv_addr, uint16_t srv_port,
                      const char *srv_auth_token,
                      const char *data_org, const char *data_bucket,
                      const char *data_meas_name );


  };

  DataReportConfig::DataReportConfig( const char *srv_addr,uint16_t srv_port, 
                    const char *srv_auth_token, const char *data_org, const char *data_bucket,
                    const char *data_meas_name ):
                    server_address(srv_addr), server_port(srv_port), server_auth_token(srv_auth_token),
                    data_org(data_org), data_bucket(data_bucket), data_measurement_name(data_meas_name)
  {}

  const char SERVER_REQ_URL_V2[] = "/api/v2/write?precision=s"; //org=mine&bucket=ts_bucket&precision=s";

//== REPORTING =====================================================================================
//-- SUBMIT DATA -----------------------------------------------------------------------------------
// Upload the data to the server
bool submitData(const DataReportConfig& rptConf, const DataReportValues& rptValues)
{
  #ifdef SENSOR_BME280
    const char PAYLOAD_STRING[] = "%s,deviceId=%s,location=%s temperature=%.2f,humidity=%.2f,pressure=%.2f,battery=%di,uptime=%llu.%llu";
  #else
    const char PAYLOAD_STRING[] = "%s,deviceId=%s,location=%s temperature=%.2f,humidity=%.2f,battery=%di,uptime=%llu.%llu";
  #endif

  SERIAL_PLN("Initiating data upload.");
  // Use WiFiClientSecure class to create TLS connection
  BearSSL::WiFiClientSecure tcpClient;
  tcpClient.setInsecure();

  SERIAL_PF( "\nConnecting to: %s:%d\n", rptConf.server_address, rptConf.server_port );

  g_dispIcons.fields.inet = true;
  drawScreen();

  if ( !tcpClient.connect( rptConf.server_address, rptConf.server_port ) ) 
  {
    g_dispIcons.fields.dislike = true;
    drawScreen();

    SERIAL_PLN( F("Connection failed") );
    return false;
  }
  drawScreen();

  g_dispIcons.fields.upload = true;
  drawScreen();

  // Send HTTPS request
  String strReq;

  strReq += F("POST ");
    strReq += influx::SERVER_REQ_URL_V2;
    strReq += "&org=";          strReq += rptConf.data_org;
    strReq += "&bucket=";     strReq += rptConf.data_bucket;
    strReq += F(" HTTP/1.1\r\n");

  strReq += F("Host: ");  strReq += rptConf.server_address; strReq += F(" \r\n");

  strReq += F("User-Agent: ESP8266 Sensor Agent\r\n");
  strReq += F("Connection: close\r\n");
  strReq += F("Authorization: Token ");
  strReq += rptConf.server_auth_token; strReq += F(" \r\n");

  //strReq += F("Content-Type: application/x-www-form-urlencoded\r\n");

  char payloadBuffer[256]     = { 0 };
  uint64_t timeDiff = millis() - rptValues.timeStamp;
  sprintf( payloadBuffer, 
    PAYLOAD_STRING,  //   "%s,deviceID=%s,location=%s temperature=%.2f,humidity=%.2f,pressure=%.2f,battery=%di,uptime=%llu.%llu";
    rptConf.data_measurement_name, // e.g. "homeThermoSensor"
    rptValues.deviceId, //device ID,
    rptValues.location, //location,
    rptValues.tempr, // Temperature
    #ifdef SENSOR_BME280
      rptValues.humid,  // Humidity
    #endif
    rptValues.press, // Air pressure
    rptValues.battery, // Battery level
    ( timeDiff / 1000), ((timeDiff / 100) - timeDiff / 1000) // Uptime
  );
  
  uint16_t len = strlen( payloadBuffer );

  strReq += F("Content-Length: ");
  char payloadLength[5] = { 0 };
  sprintf( payloadLength, "%d", len );
  strReq += payloadLength;
  strReq += F("\r\n\r\n");
  strReq += payloadBuffer;
  strReq += F("\r\n");
  strReq += F("\r\n");

  SERIAL_P( F("Request: ") ); SERIAL_PLN( strReq );

  if ( tcpClient.write(  strReq.c_str() ) == 0 ) 
  {
    g_dispIcons.fields.dislike = true;
    drawScreen();

    SERIAL_PLN( F("Failed to send request.") );
    return false;
  }
  SERIAL_PLN( F("Request sent.") ) ;
  
  // Check HTTP status
  char httpStatus[32] = {0};
  tcpClient.readBytesUntil('\r', httpStatus, sizeof( httpStatus ));
  if ( strcmp( httpStatus, "HTTP/1.1 204 No Content" ) != 0) 
  {
    g_dispIcons.fields.dislike = true;
    drawScreen();

    SERIAL_P( F("Unexpected HTTP response: ") ); SERIAL_PLN( httpStatus );
    return false;
  }
  else
  {
    SERIAL_PLN( F("Received HTTP 204 No Content."));
  }

  // Skip HTTP headers
  const char endOfHeaders[] = "\r\n\r\n";
  if ( !tcpClient.find( endOfHeaders ) ) 
  {
    g_dispIcons.fields.dislike = true;
    drawScreen();

    SERIAL_PLN( F("Invalid response. Cannot find \\r\\n\\r\\n") );
    return false;
  }

  return true;
}
}; // namespace influx

// ##############################################



//== SENSORS =======================================================================================
//-- READ SENSOR DATA ------------------------------------------------------------------------------
void readSensors()
{
  #ifdef SENSOR_BME280
    BME280::TempUnit tempUnit(BME280::TempUnit_Celsius);
    BME280::PresUnit presUnit(BME280::PresUnit_hPa);
    g_bme.read(g_pres, g_temp, g_hum, tempUnit, presUnit);
  #else
    g_temp = sensorAHT10.readTemperature();
    g_hum  = sensorAHT10.readHumidity();
    SERIAL_PF("Tempr: %.2f, RH: %.2f%%\r\n", g_temp, g_hum );
  #endif

  g_temp += g_iniStorage.sensor_temp_correction;

  /* Battery adjustments
    #1
    4.20 -> 859
    2.50 -> 513
    
    859 - 513 = 346

    #2
    4.20 => 225 (100%)
    3.98 -> 213 ( 94%)
    2.50 => 134 (  0%)
    224 - 133 = 91

    #4
    4.20 => 853 (100%)
    3.33 -> 677 ( 79%)
    2.50 => 508 (  0%)  ZERO_BASE
    853 - 508 = 345     RANGE_DIFF

    -- Office 01 --------------
      4.20 149 |  859
      4.06 144 |  831
      2.75  98 |  562
      ---------+-------
      859 - 562 = 297 |  ((A0 - 297) / 562 ) * 100

    ------------
      4.2 100%
      2.75 0%
    ------------

    Battery level = ( ( A0 - zeroBase ) / rangeDiff ) * 100;
    ==>  4.20 -> 868; Theoretical max
    ==>  4.17 -> 862; Practical max
    ==>  2.75 -> 568; zeroBase
    ==>  868 - 568 = 300;  rangeDiff 

  */

  // #ifdef TS_HOME_02  
  //   const int16_t ZERO_BASE = 134;
  //   const float RANGE_DIFF = 91;
  // #else
  //   const int16_t ZERO_BASE = 568;
  //   const float RANGE_DIFF = 300;
  // #endif

  // const _iniFile& ini = iniFileStorage;
  const sensor::SensorIniFileStorage& ini = g_iniStorage;

  int16_t batteryLevel = analogRead(A0);
  //g_battery = ( ( batteryLevel - ZERO_BASE )  / RANGE_DIFF ) * 100; 
  //g_battery = batteryLevel;
  g_battery = (  (float)( batteryLevel - ini.batteryMinLevel )  / (ini.batteryMaxLevel - ini.batteryMinLevel) ) * 100;

  // Corrections there may be minimal differencies. Making sure that the range is 0-100
  if ( g_battery > 100 ) { g_battery = 100; }
  if ( g_battery <   0 ) { g_battery =   0; }

  Serial.printf("A0: %d | Battery level: %d%%\n", batteryLevel, g_battery );
}

//-- handleTickerUploadTimeout ---------------------------------------------------------------------
void handleTickerUploadTimeout()
{
  // If this function fired, it means that the data submit took way too long.
  // To avoid infinite uptime and total discharging a WatchDog stops the process. After that goes
  // to deep sleep.
  SERIAL_PLN( F("Data upload watchdog fired! Going to deep sleep.") );
  g_dispIcons.fields.dislike = true;
  g_dispIcons.fields.pclosed = true;
  drawScreen();

  ESP.deepSleep(  g_iniStorage.upload_freq * 10e5, WAKE_RF_DEFAULT );
}


#ifdef SENSOR_BME280
//-- SETUP BME280-----------------------------------------------------------------------------------
bool setupBME280() //-- TDDO Sensor Error Display
{
  uint8_t detectCounter = 10;
  while(!g_bme.begin())
  {
    SERIAL_PLN( F("Could not find BME280I2C sensor!") );
    delay(1000);
    --detectCounter;

    if ( 0 == detectCounter ) { return false; }
  }

  switch(g_bme.chipModel())
  {
     case BME280::ChipModel_BME280:
       SERIAL_PLN( F("Found BME280 sensor! Success.") );
       break;
     case BME280::ChipModel_BMP280:
       SERIAL_PLN( F("Found BMP280 sensor! No Humidity available.") );
       break;
     default:
       SERIAL_PLN( F("Found UNKNOWN sensor! Error!") );
  }

  // Change some settings before using.
  settings.tempOSR = BME280::OSR_X4;

  g_bme.setSettings(settings);
  delay(125);

  return true;
}
#endif

//== SCREENS =======================================================================================
//-- DRAW ICONS ------------------------------------------------------------------------------------
void drawIcons()
{
  u8g2.setFont(u8g2_font_open_iconic_www_1x_t); // Font: W & H: 8 Pixel

  if ( true == g_dispIcons.fields.like      ) { u8g2.drawStr( 0,  8, "\x49"); } // like
  if ( true == g_dispIcons.fields.dislike   ) { u8g2.drawStr( 0, 18, "\x52"); } // dislike
  
  if ( true == g_dispIcons.fields.wifi      ) { u8g2.drawStr( 0, 48, "\x48"); } // wifi
  if ( true == g_dispIcons.fields.inet      ) { u8g2.drawStr( 0, 38, "\x4E"); } // inet
  if ( true == g_dispIcons.fields.upload    ) { u8g2.drawStr( 0, 28, "\x43"); } // upload

  u8g2.setFont(u8g2_font_open_iconic_thing_1x_t);
  if ( true == g_dispIcons.fields.popen     ) { u8g2.drawStr(10, 48, "\x44"); } // padlock open
  if ( true == g_dispIcons.fields.pclosed   ) { u8g2.drawStr( 0,  8, "\x4F"); } // padlock close
}

//-- SCREEN V1 -------------------------------------------------------------------------------------
  //-- RH XX%
  //-- ------
  //-- 22.5 oC
void screenV1()
{
  u8g2.firstPage();

  do 
  {
  //  uint8_t i = 0;
  //      for ( i = 0; i <= 25; i +=5 ) { u8g2.drawVLine(i, 0, 49); }
 
    u8g2.setFont(u8g2_font_helvB08_tf);
    u8g2.drawStr(42, 10, "RH" );
    uint8_t width = u8g2.getStrWidth(g_txHumid);
    u8g2.drawStr(84 - width, 10, g_txHumid );

    u8g2.drawHLine(25, 16, 64);

    //-- Temperature drawing
    width = u8g2.getStrWidth("\xb0\x43");
    u8g2.drawStr(83 - width, 32, "\xb0\x43");

    u8g2.setFont(u8g2_font_helvB14_tn);
    u8g2.drawStr(70, 48, g_txTemprR );

    u8g2.setFont(u8g2_font_helvB24_tr);
    width = u8g2.getStrWidth(g_txTemprD);
    u8g2.drawStr(70 - width, 48, g_txTemprD );
  } 
  while ( u8g2.nextPage() );
}

//-- SCREEN V2 -------------------------------------------------------------------------------------
  //-- 22.5 oC
  //-- ------
  //-- RH XX%
void screenV2()
{
  u8g2.firstPage();

  do 
  {
    //u8g2.drawFrame(0, 0, 84, 48);
    //  uint8_t i = 0;
    //      for ( i = 0; i <= 25; i +=5 ) { u8g2.drawVLine(i, 0, 49); }
    uint8_t width = 0;

    //-- Relative Humidity "RH 45%"
    u8g2.setFont(u8g2_font_helvB08_tf);
    width = u8g2.getStrWidth(g_txHumid);
    //u8g2.drawStr( 25 + (((84 - 25) - width)/2.00), 48, g_txHumid );
    u8g2.drawStr( 25, 48, g_txHumid );

    //-- Horizontal Line
    u8g2.drawHLine(25, 36, 84);

    //-- Temperature drawing
    u8g2.setFont(u8g2_font_helvR10_tf);
    width = u8g2.getStrWidth("\xb0\x43");  // width: 20   "oC"
    u8g2.drawStr(83 - width, 11, "\xb0\x43");

    //-- Temperature decimal
    u8g2.setFont(u8g2_font_logisoso34_tn);
    width = u8g2.getStrWidth(g_txTemprD);
    u8g2.drawStr(63 - width, 34, g_txTemprD );
    
    // Temperature remainder - 1 digit
    u8g2.setFont(u8g2_font_logisoso22_tn);
    width = u8g2.getStrWidth(g_txTemprR);
    u8g2.drawStr(84 - width, 34, g_txTemprR );

    u8g2.drawBox(66, 32, 3, 3); // The dot
  
    drawIcons();

    //-- Battery
    u8g2.drawFrame( 72, 45, 12,  3); // Body
    u8g2.drawFrame( 71, 46,  2,  1); // Top pin
    for ( uint8_t i = 0; i < g_battery / 10; i++ ) { u8g2.drawVLine(73 + 9 - i, 46, 1); } // Fill the body
    u8g2.setFont(u8g2_font_4x6_tf);
    char batValue[7] = { 0 }; // "100\0" 
    sprintf(batValue, "%d", (g_battery > 100 ? 100 : g_battery ) );
    width = u8g2.getStrWidth( batValue );
    u8g2.drawStr(84 - width, 44, batValue );
  } 
  while ( u8g2.nextPage() );
}

//-- SCREEN V3 -------------------------------------------------------------------------------------
  //-- 22.5 oC 47 rhum
void screenV3()
{
  u8g2.firstPage();
  do 
  {
   //u8g2.drawFrame(0, 0, 84, 48);
        // uint8_t i = 0;
        //for ( i = 0; i <= 85; i +=5 ) { u8g2.drawVLine(i, 0, 49); }
        // for ( i = 0; i <= 50; i +=5 ) { u8g2.drawHLine(0, i, 85); }
   
    uint8_t width = 0;

    //-- Temperature drawing
    u8g2.setFontPosTop();
    u8g2.setFont(u8g2_font_helvB24_tr);
    width = u8g2.getStrWidth(g_txTemprD);
    u8g2.drawStr(34 - width, -2, g_txTemprD ); //u8g2.drawStr(0, 0, g_txTemprD );

    u8g2.setFont(u8g2_font_helvB08_tf);
    u8g2.drawStr(36, -1, "\xb0\x43");

    u8g2.setFontPosBaseline();
    u8g2.setFont(u8g2_font_helvB14_tn);
    u8g2.drawStr(38, 24, g_txTemprR );

    u8g2.drawBox(35, 22, 2, 2); // The dot

    //-- Relative Humidity "RH 45%"
    u8g2.setFontPosTop();
    u8g2.setFont(u8g2_font_helvB18_tr);
    
    char buffer[4] = { 0 };
    sprintf( buffer, "%d", static_cast<int>(g_hum) );
    width = u8g2.getStrWidth( buffer );
    u8g2.drawStr( (84 - width), 5, buffer );
    
    u8g2.setFont(u8g2_font_profont10_tr);
    width = u8g2.getStrWidth("rhum");
    u8g2.drawStr( (84 - width), -1, "rhum" );

    //Had a thought to display history at the bottom, but it would require file management.
    //not in the mood of now
  } 
  while ( u8g2.nextPage() );
}


//-- SCREEN V4 -------------------------------------------------------------------------------------
  //-- 22.5 oC
  //-- ------
  //-- RH XX%
void screenV4()
{
  u8g2.firstPage();
  do 
  {
   u8g2.drawFrame(0, 0, 84, 48);
   uint8_t i = 0;
   for ( i = 0; i <= 85; i +=5 ) { u8g2.drawVLine(i, 0, 49); }
   for ( i = 0; i <= 50; i +=5 ) { u8g2.drawHLine(0, i, 85); }
   
    uint8_t width = 0;

    //-- Temperature drawing
    // u8g2.setFont(u8g2_font_helvB24_tr);
    //u8g2.setFont( u8g2_font_fub25_tr );
    u8g2.setFontPosTop();

    u8g2.setFont(u8g2_font_helvB08_tf);
    width = u8g2.getStrWidth( "\xb0\x43" );
    u8g2.drawStr(84- width, -1, "\xb0\x43");

    u8g2.setFontPosBaseline();
    u8g2.setFont(u8g2_font_logisoso16_tr);
    width = u8g2.getStrWidth( g_txTemprR );
    u8g2.drawStr(84 - width, 26, g_txTemprR );

    u8g2.drawBox(84 - width - 4, 22, 3, 3); // The dot
    width += 4;

    u8g2.setFont( u8g2_font_logisoso26_tr );
    width += u8g2.getStrWidth(g_txTemprD);
    u8g2.drawStr(84 - width - 1, 26, g_txTemprD ); //u8g2.drawStr(0, 0, g_txTemprD );

    //-- Relative Humidity "RH 45%"
   // u8g2.setFontPosTop();
    //u8g2.setFont(u8g2_font_helvB18_tr);
    
    u8g2.setFont(u8g2_font_helvB12_tr);
    width = u8g2.getStrWidth( "%" );
    u8g2.drawStr( (84 - width), 48, "%" );

    char buffer[4] = { 0 };
    sprintf( buffer, "%d", static_cast<int>(g_hum) );

    u8g2.setFont(u8g2_font_helvB18_tr);
    width += u8g2.getStrWidth( buffer );
    u8g2.drawStr( (84 - width), 48, buffer );
    
    //u8g2.setFont(u8g2_font_profont10_tr);
    //width = u8g2.getStrWidth("rhum");
    //u8g2.drawStr( (84 - width), -1, "rhum" );

    // u8g2.drawStr( 25, 48, g_txHumid );


    // //-- Relative Humidity "RH 45%"
    // u8g2.setFont(u8g2_font_helvB08_tf);
    // width = u8g2.getStrWidth(g_txHumid);
    // //u8g2.drawStr( 25 + (((84 - 25) - width)/2.00), 48, g_txHumid );
    // u8g2.drawStr( 25, 48, g_txHumid );

    // //-- Horizontal Line
    // u8g2.drawHLine(25, 36, 84);

    // //-- Temperature drawing
    // u8g2.setFont(u8g2_font_helvR10_tf);
    // width = u8g2.getStrWidth("\xb0\x43");  // width: 20   "oC"
    // u8g2.drawStr(83 - width, 11, "\xb0\x43");

    // //-- Temperature decimal
    // u8g2.setFont(u8g2_font_logisoso34_tn);
    // width = u8g2.getStrWidth(g_txTemprD);
    // u8g2.drawStr(63 - width, 34, g_txTemprD );
    
    // // Temperature remainder - 1 digit
    // u8g2.setFont(u8g2_font_logisoso22_tn);
    // width = u8g2.getStrWidth(g_txTemprR);
    // u8g2.drawStr(84 - width, 34, g_txTemprR );

    // u8g2.drawBox(66, 32, 3, 3); // The dot
  
    // drawIcons();

    // //-- Battery
    // u8g2.drawFrame( 72, 45, 12,  3); // Body
    // u8g2.drawFrame( 71, 46,  2,  1); // Top pin
    // for ( uint8_t i = 0; i < g_battery / 10; i++ ) { u8g2.drawVLine(73 + 9 - i, 46, 1); } // Fill the body
    // u8g2.setFont(u8g2_font_4x6_tf);
    // char batValue[5] = { 0 }; // "100\0" 
    // sprintf(batValue, "%d", (g_battery > 100 ? 100 : g_battery ) );
    // width = u8g2.getStrWidth( batValue );
    // u8g2.drawStr(84 - width, 44, batValue );
  } 
  while ( u8g2.nextPage() );
}


//-- drawScreen ------------------------------------------------------------------------------------
void drawScreen()
{
  //screenV1();
  screenV2();
  //screenV3();
  //screenV4();
}

//-- printScreenLine -------------------------------------------------------------------------------
const uint8_t LINE_HEIGHT = 13;
uint8_t g_linePos = LINE_HEIGHT;
void printScreenLine(const char *text)
{
  u8g2.firstPage();
  do 
  {
    //u8g2.setFont(u8g2_font_helvB08_tf);
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.setCursor(0, g_linePos);
    u8g2.print(text);
  } 
  while ( u8g2.nextPage() );

  //g_linePos += LINE_HEIGHT;
}


//-- screenAPStarted -------------------------------------------------------------------------------
void screenAPinit()
{
  u8g2.firstPage();
  do 
  {
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(0, 7, " AP MODE STARTED" );
    u8g2.drawHLine(0, 9, 84);
    u8g2.drawStr(0, 18,  "Switching on WiFi." );
    u8g2.drawStr(0, 26, "Please wait." );
    //u8g2.drawStr(0, 34, txt.c_str() );
  }
  while ( u8g2.nextPage() );
}


//-- screenAPStarted -------------------------------------------------------------------------------
void screenAPStarted(bool isOtaActive = false )
{
  u8g2.firstPage();
  do 
  {
    String txt;
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(0, 7, " AP MODE STARTED" );
    u8g2.drawHLine(0, 9, 84);
    u8g2.drawStr(0, 18,  AP_SSID );
    txt = "Passwd: "; txt += AP_PWD;
    u8g2.drawStr(0, 26, txt.c_str() );
    txt = "IP: "; txt += WiFi.softAPIP().toString();
    u8g2.drawStr(0, 34, txt.c_str() );

    if ( true == isOtaActive ) 
    { 
      
      u8g2.drawHLine(0, 36, 84);
      u8g2.drawStr(0, 45, "OTA active" );
    }
  }
  while ( u8g2.nextPage() );
}


//-- screenOTAStarted ------------------------------------------------------------------------------
/*
  - OTA UPDATE -
  ----------------
  Progress: 33%
  +++++
  "Error text next row"
  
  Completed. Restart.
*/
void screenOTA(uint8_t progress = 0, const char *ERROR_TEXT = NULL, bool isComplete = false )
{
  // onStart
  // onError
  // onProgress
  // onEnd
  
  // File or Sketch
  char txt[15] = { 0 };
  u8g2.firstPage();
  do 
  {
    u8g2.setFont(u8g2_font_5x7_tf);
    uint8_t width = u8g2.getStrWidth( "- OTA UPDATE -" );
    u8g2.drawStr((84 - width) / 2, 7, "- OTA UPDATE -" );
    u8g2.drawHLine(0, 9, 84);

    sprintf( txt, "Progress: %d%%", progress );
    u8g2.drawStr(0, 18, txt );
    u8g2.drawBox(0, 20, (progress/100.0 ) * 84, 4 );

    if ( NULL != ERROR_TEXT ) 
    {
      u8g2.drawStr(0, 32, "Error:");
      u8g2.drawStr(0, 40, ERROR_TEXT );
    }

    if ( true == isComplete )
    {
      u8g2.drawStr(0, 47, "Completed. Wait!");
    }

  }
  while ( u8g2.nextPage() );
}



//-- screenBatteryMonitor --------------------------------------------------------------------------
void screenBatteryMonitor( int16_t batteryLevel  = 0 )
{
  char batLevel[7] = { 0 };
  sprintf( batLevel, "%04d", batteryLevel );
  
  u8g2.firstPage();
  do
  {
    u8g2.setFont(u8g2_font_5x7_tf);
    uint8_t width = u8g2.getStrWidth( "BATTERY LEVEL" );
    u8g2.drawStr((84 - width) / 2, 7, "BATTERY LEVEL" );
    u8g2.drawHLine(0, 9, 84);

    u8g2.setFont(u8g2_font_helvB24_tr);
    width = u8g2.getStrWidth( batLevel);
    u8g2.drawStr(84 - width, 48, batLevel );
  } 
  while ( u8g2.nextPage() );
}

//-- convertSensorData -----------------------------------------------------------------------------
void convertSensorDataToChar()
{
  // Convert sensor data to Character representation
  sprintf(g_txTemprD, "%2d",  static_cast<int>(g_temp) );
  sprintf(g_txTemprR, "%1d", static_cast<int>(  ( fabs(g_temp) - ( (int)( fabs(g_temp)) % 100 )) * 10 ));
  sprintf(g_txHumid, "RH %3d%%", static_cast<int>(g_hum) );
}


//------- OTA --------------------------------------------------------------------------------------
//-- setupOTA --------------------------------------------------------------------------------------
uint8_t g_otaPercent = 0;
const char OTA_ERR_AUTH_FAILED[] ="Auth Failed"   ;
const char OTA_ERR_BEGN_FAILED[] ="Begin Failed"  ;
const char OTA_ERR_CONN_FAILED[] ="Connect Failed";
const char OTA_ERR_RECV_FAILED[] ="Receive Failed";
const char OTA_ERR_END_FAILED[]  ="End Failed"    ;

void setupOTA()
{
  ArduinoOTA.setPort(8266);            // Port defaults to 8266
  ArduinoOTA.setHostname("myesp8266"); // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setPassword(".EspThermoSensor.");   // No authentication by default

  ArduinoOTA.onStart([]() 
  {
    g_batLevelTicker.detach();
    g_uploadTimeOutTicker.detach();
    yield();

    server.close();
    yield();
    server.stop();
    yield();

    String type;
    if ( ArduinoOTA.getCommand() == U_FLASH ) { type = "sketch"; } 
    else 
    { /* U_FS */  
      // NOTE: if updating FS this would be the place to unmount FS using FS.end()
      type = "filesystem"; 
      LittleFS.end(); 
    }
    
    SERIAL_PLN("Start updating " + type);
    screenOTA();
  });

  ArduinoOTA.onEnd([]() 
  {
    screenOTA( g_otaPercent, 0, true );
    SERIAL_PLN("\nEnd");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) 
  {
    g_otaPercent = progress / (total / 100);
    SERIAL_PF("Progress: %u%%\r", g_otaPercent );
    if ( 0 == g_otaPercent % 2 ) { screenOTA( g_otaPercent ); };
  });


  ArduinoOTA.onError([](ota_error_t error) 
  {
    SERIAL_PF( "Error[%u]: ", error );
    if      (error == OTA_AUTH_ERROR)    { SERIAL_PLN( OTA_ERR_AUTH_FAILED ); screenOTA( g_otaPercent, OTA_ERR_AUTH_FAILED); } 
    else if (error == OTA_BEGIN_ERROR)   { SERIAL_PLN( OTA_ERR_BEGN_FAILED ); screenOTA( g_otaPercent, OTA_ERR_BEGN_FAILED); }
    else if (error == OTA_CONNECT_ERROR) { SERIAL_PLN( OTA_ERR_CONN_FAILED ); screenOTA( g_otaPercent, OTA_ERR_CONN_FAILED); }
    else if (error == OTA_RECEIVE_ERROR) { SERIAL_PLN( OTA_ERR_RECV_FAILED ); screenOTA( g_otaPercent, OTA_ERR_RECV_FAILED); } 
    else if (error == OTA_END_ERROR)     { SERIAL_PLN( OTA_ERR_END_FAILED  ); screenOTA( g_otaPercent, OTA_ERR_END_FAILED ); }
  }
  );

  ArduinoOTA.begin();
}


void handleSetupMode()
{
  SERIAL_PLN("Config mode active. Starting Access Point mode.");
  printScreenLine("Config mode started.");
  
  wifiStaConnectHandler = WiFi.onSoftAPModeStationConnected(onSTAConnected);
  if ( false == g_iniStorage.wifi_enabled )
  {
    WiFi.forceSleepWake();
    delay(1000);
    WiFi.mode(WIFI_AP);
    do
    {
      SERIAL_PLN("Waking up modem.");
      screenAPinit();
      delay(1000);
    }
    while ( false == WiFi.softAP( AP_SSID, AP_PWD ) );
  }
  else
  {
    WiFi.mode(WIFI_AP);
    WiFi.softAP( AP_SSID, AP_PWD );
    delay(1000);
  }

  SERIAL_PF("AP IP address: %s\n", WiFi.softAPIP().toString().c_str() );
  screenAPStarted();

  server.onNotFound([]() {                            // If the client requests any URI
  if (!g_webConfMan.handleFileRead(server.uri(), server, g_iniStorage))                  // send it if it exists
    server.send(404, "text/plain", "404: Not Found"); // otherwise, respond with a 404 (Not Found) error
  });

  server.begin();                           // Start the server
  SERIAL_PLN( F("HTTP server started") );

  setupOTA();
  SERIAL_PLN( F("OTA started") );
  screenAPStarted( true );
}

//-- handleSensorModeWiFiConnect -------------------------------------------------------------------
void handleSensorModeWiFiConnectV2()
{
  // Connect to WiFi 
  bool isConfigChanged = false;
  bool isConnectionSuccessful = connectToWiFiV4( g_iniStorage, isConfigChanged );

  // Write Ini file if configuration has changed
  if ( true == isConfigChanged )
  {
    SERIAL_PLN("NetworkConfig changed.");

    bool result = sensor::SensorConfigFile::writeIniFile(g_iniStorage);
    SERIAL_PF("Write IniFile: %d\n", result ) ;
  }

  if ( false == isConnectionSuccessful )
  {
    SERIAL_PLN( F("Failed to connect to WiFi AP. Going to sleep.") );
    isConfigChanged = true;
  
    g_dispIcons.allFields = 0;
    g_dispIcons.fields.wifi = true;
    g_dispIcons.fields.dislike = true;
    g_dispIcons.fields.pclosed = true;
    
    drawScreen();
    ESP.deepSleep( g_iniStorage.upload_freq * 10e5, WAKE_RF_DEFAULT );
  }

}


//-- handleSensorMode ------------------------------------------------------------------------------
void handleSensorMode()
{
  readSensors();
  // //!!!!!!!!!!!!!!!!!!!!
  // g_temp = 22.5;
  // g_hum = 56;
  // //!!!!!!!!!!!!!!!!!!!!
  
  // sprintf(g_txTemprD, "%2d",  static_cast<int>(g_temp) );
  // sprintf(g_txTemprR, "%1d", static_cast<int>(  (g_temp - ( (int)(g_temp) % 100 )) * 10) );
  // sprintf(g_txHumid, "RH %3d%%", static_cast<int>(g_hum) );

  convertSensorDataToChar();
  drawScreen();

  g_uploadTimeOutTicker.attach(g_iniStorage.upload_timeout, handleTickerUploadTimeout );
  
  influx::DataReportConfig rptConfig( g_iniStorage.server_address,
                                      g_iniStorage.server_port,
                                      g_iniStorage.server_auth_token,
                                      g_iniStorage.data_measurement_org,
                                      g_iniStorage.data_measurement_bucket,
                                      g_iniStorage.data_measurement_name
                                  );
  

  DataReportValues rptValues( g_iniStorage.device_id, g_iniStorage.location );
  rptValues.tempr = g_temp;
  rptValues.humid = g_hum;
  #ifdef SENSOR_BME280
    rptValues.press = g_pres;
  #endif
  rptValues.battery = g_battery;
  rptValues.timeStamp = g_timeStamp;
  
  if ( true == influx::submitData(rptConfig, rptValues ) )
  {
    
    g_dispIcons.allFields = 0;
    //g_dispIcons.fields.like = true;
  }
  Serial.printf("Diff: %llu ms\n", millis() - g_timeStamp );


  // if ( true == submitData() )
  // {
  //   g_dispIcons.allFields = 0;
  //   //g_dispIcons.fields.like = true;
  // }
  // Serial.printf("Diff: %llu ms\n", millis() - g_timeStamp );

  ///////////////////
  g_dispIcons.fields.pclosed = true;
  drawScreen();
  
  ESP.deepSleep(  g_iniStorage.upload_freq * 10e5, WAKE_RF_DEFAULT );
}

//-- handleTickerBatteryMonitor() ------------------------------------------------------------------
void handleTickerBatteryMonitor()
{
  int16_t batteryLevel = analogRead(A0);
  screenBatteryMonitor( batteryLevel );

  SERIAL_PF("Battery level: %d\r\n", batteryLevel );
}

uint64_t g_btnTimeStamp = 0;

//-- handleSetupModeLoop ---------------------------------------------------------------------------
void handleSetupModeLoop()
{
  // handle HTTP requests and clients
  server.handleClient();

  // handle OTA requests and clients
  ArduinoOTA.handle();

  if ( !digitalRead( BTN_CONFIG) )
  {
    if ( ( millis() - g_btnTimeStamp > 150 ) )
    {
      SERIAL_PLN( "Config button pushed. Time passed" );
      if ( false == g_batLevelTicker.active() ) { g_batLevelTicker.attach_ms(300, handleTickerBatteryMonitor ); }
      else { g_batLevelTicker.detach(); screenAPStarted( true ); }

      g_btnTimeStamp = millis();
    }
    else
    {
      SERIAL_PLN( "Config button pushed. Early." );
    }
  }
  else { g_btnTimeStamp = millis(); }

}

//==================================================================================================
//== MAIN FUNCTIONS VARIANTS =======================================================================
/**/
//-- SETUP INI FILE TESTING ------------------------------------------------------------------------
;


void setupIniFile() 
{
  Serial.begin(115200);
  Serial.println( F("\n\nStarting...") );

  sensor::SensorConfigFile senConFile;

  // Mount the SPIFFS  
  if ( false == LittleFS.begin() )  
  { 
    SERIAL_PLN("FATAL ERROR: LittleFS.begin() failed"); 
    // printScreenLine("File system error.");
  }
  
  // sensor::SensorIniFileStorage iniStorage;
  // Read the Ini file 
  if ( false == senConFile.readIniFile(g_iniStorage) )
  {
    SERIAL_PLN("FATAL ERROR: Failed to read the ini file"); 
    // printScreenLine("ini file error.");
  }

  // delay(3000);
  
  // strcpy(g_iniStorage.location, "usDani" );
  // strcpy(g_iniStorage.data_measurement_bucket, "ts_sensor");
  // strcpy(g_iniStorage.auth_token, "blah-blah-blah-blah-blah-blah-");

  // // Write the Ini file 
  // if ( false == senConFile.writeIniFile(g_iniStorage) )
  // {
  //   SERIAL_PLN("FATAL ERROR: Failed to write the ini file"); 
  //   // printScreenLine("ini file error.");
  // }

}


void setupWebServer()
{
  SERIAL_PLN("Config mode active. Starting Access Point mode.");
  // printScreenLine("Config mode started.");
  
  wifiStaConnectHandler = WiFi.onSoftAPModeStationConnected(onSTAConnected);
  if ( false == g_iniStorage.wifi_enabled )
  {
    WiFi.forceSleepWake();
    delay(1000);
    WiFi.mode(WIFI_AP);
    do
    {
      SERIAL_PLN("Waking up modem.");
      screenAPinit();
      delay(1000);
    }
    while ( false == WiFi.softAP( AP_SSID, AP_PWD ) );
  }
  else
  {
    WiFi.mode(WIFI_AP);
    WiFi.softAP( AP_SSID, AP_PWD );
    delay(1000);
  }

  SERIAL_PF("AP IP address: %s\n", WiFi.softAPIP().toString().c_str() );
  // screenAPStarted();

  server.onNotFound([]() {                            // If the client requests any URI
  if ( !g_webConfMan.handleFileRead( server.uri(), server, g_iniStorage ) )                  // send it if it exists
    server.send(404, "text/plain", "404: Not Found"); // otherwise, respond with a 404 (Not Found) error
  });

  server.begin();                           // Start the server
  SERIAL_PLN( F("HTTP server started") );

  // setupOTA();
  // SERIAL_PLN( F("OTA started") );
  // screenAPStarted( true );
}


void setupInflux() 
{
  // influx::DataReportConfig rptConfig = { 
  //   g_iniStorage.server_address, 
  //   g_iniStorage.server_port,
  //   g_iniStorage.server_auth_token,
  //   g_iniStorage.data_measurement_org,
  //   g_iniStorage.data_measurement_bucket,
  //   g_iniStorage.data_measurement_name
  //   };

  influx::DataReportConfig rptConfig( g_iniStorage.server_address,
                                      g_iniStorage.server_port,
                                      g_iniStorage.server_auth_token,
                                      g_iniStorage.data_measurement_org,
                                      g_iniStorage.data_measurement_bucket,
                                      g_iniStorage.data_measurement_name
                                    );

  DataReportValues rptValues( "TSH99", "usBoxR" );
  // rptValues.deviceId = "TSH99";
  // rptValues.location = "usBoxR";
  rptValues.tempr = 1.0;
  rptValues.humid = 2.0;
  rptValues.press = 1000.0;
  rptValues.battery = 4.0;
  rptValues.timeStamp = 1024;

  influx::submitData( rptConfig, rptValues );
}




//-- SETUP FULL ------------------------------------------------------------------------------------
void setupFull() 
{
  pinMode( BTN_CONFIG, INPUT_PULLUP); // Extra button for swith on config mode.
  g_timeStamp = millis();

  #ifdef GSI_DEBUG
    Serial.begin(115200);
    Serial.println( F("\n\nStarting...") );
  #endif

  g_isInSetupMode = !digitalRead(BTN_CONFIG);
  ///////////////g_isInSetupMode = true;
  SERIAL_PF("Config mode triggered: %s\n", ( true == g_isInSetupMode ? "yes" : "no" ) );

  // Init screen
  u8g2.begin();
  u8g2.setContrast( 155 ); // 155 - Home; 127 - Office

  sensor::SensorConfigFile senConFile;

  // Mount the LittleFS  
  if ( false == LittleFS.begin() )  
  { 
    SERIAL_PLN("FATAL ERROR: LittleFS.begin() failed"); 
    printScreenLine("File system error.");
  
    if ( true == g_isInSetupMode )
    {
      delay(2000);
      handleSetupMode();
      return;
    }

    delay(35000); // TODO find-up a better error strategy
    ESP.restart();
  }
  
  // sensor::SensorIniFileStorage iniStorage;
  // Read the Ini file 
  if ( false == senConFile.readIniFile(g_iniStorage) )
  {
    SERIAL_PLN("FATAL ERROR: Failed to read the ini file"); 
    printScreenLine("ini file error.");

    if ( true == g_isInSetupMode )
    {
      delay(2000);
      handleSetupMode();
      return;
    }

    delay(35000); // TODO find-up a better error strategy
    ESP.restart();
  }
  
  // Set-up screen
  u8g2.setContrast( g_iniStorage.display_contrast); // 155 - Home; 127 - Office
  u8g2.setDisplayRotation( g_iniStorage.display_rotation == true ? U8G2_R0 : U8G2_R2 );
  
  // Set-up BME280 sensor
  Wire.begin(BME280_SDA, BME280_SCL);
  //printScreenLine("Checking BME Sensors.");
  #ifdef SENSOR_BME280
    if ( true == setupBME280() )
    {
      //delay(125);
      readSensors();
    }
    else 
  #else
    g_temp = sensorAHT10.readTemperature();
    g_hum  = sensorAHT10.readHumidity();
    if ( AHT10_ERROR == g_temp || AHT10_ERROR != g_hum )
  #endif
  {  
    printScreenLine( "Sensors Error!");
    delay(10000);
  }

  g_dispIcons.allFields = 0b00000000;
  convertSensorDataToChar();
  drawScreen();

  if ( false == g_iniStorage.wifi_enabled && false == g_isInSetupMode )
  {
    SERIAL_PLN("WiFi is disabled. Updating and showing sensor data.")
    g_dispIcons.fields.pclosed = true;
    drawScreen();
    ESP.deepSleep(  g_iniStorage.upload_freq * 10e5, WAKE_RF_DISABLED );
  }

  if ( true == g_isInSetupMode )
  {
    handleSetupMode();
  }
  else
  {
    handleSensorModeWiFiConnectV2();
  }

  // //!!!!!!!!!!!!!!!!!!!!
  // g_temp = 22.5;
  // g_hum = 56;
  // //!!!!!!!!!!!!!!!!!!!!

}



//-- LOOP-------------------------------------------------------------------------------------------
void loopEmpty()
{

}


//-- handleSetupModeLoop ---------------------------------------------------------------------------
void loopWebServer()
{
  // handle HTTP requests and clients
  server.handleClient();

  // // handle OTA requests and clients
  // ArduinoOTA.handle();

  if ( !digitalRead( BTN_CONFIG) )
  {
    if ( ( millis() - g_btnTimeStamp > 150 ) )
    {
      SERIAL_PLN( "Config button pushed. Time passed" );
      if ( false == g_batLevelTicker.active() ) { g_batLevelTicker.attach_ms(300, handleTickerBatteryMonitor ); }
      else { g_batLevelTicker.detach(); screenAPStarted( true ); }

      g_btnTimeStamp = millis();
    }
    else
    {
      SERIAL_PLN( "Config button pushed. Early." );
    }
  }
  else { g_btnTimeStamp = millis(); }

}

void loopFull() 
{
  // TODO 
  // - set-up mode must have a max idle time 3 minutes ?
  // - after the idle time the device must go back to the normal sensor operation
  // - messages for error cases, set-up mode are missing
  if (true == g_isInSetupMode ) 
  { 
    handleSetupModeLoop(); 
  } 
  else 
  { 
    handleSensorMode(); 
  }
}



//==================================================================================================
//-- THE MAIN FUNCTIONS: SETUP AND LOOP ------------------------------------------------------------
void setup() 
{
  
  // setupIniFile();
  // handleSensorModeWiFiConnectV2();
  // setupInflux();
  // setupWebServer();
  setupFull();
}


void loop() 
{
  // loopWebServer();
  loopFull();
}




////////////////// OLD CODE
// //== FILE MANAGEMENT ===============================================================================
// //-- printErrorMessage -----------------------------------------------------------------------------
// void printErrorMessage(uint8_t e, bool eol = true)
// {
//   switch (e) {
//   case SPIFFSIniFile::errorNoError:
//     Serial.print("no error");
//     break;
//   case SPIFFSIniFile::errorFileNotFound:
//     Serial.print("file not found");
//     break;
//   case SPIFFSIniFile::errorFileNotOpen:
//     Serial.print("file not open");
//     break;
//   case SPIFFSIniFile::errorBufferTooSmall:
//     Serial.print("buffer too small");
//     break;
//   case SPIFFSIniFile::errorSeekError:
//     Serial.print("seek error");
//     break;
//   case SPIFFSIniFile::errorSectionNotFound:
//     Serial.print("section not found");
//     break;
//   case SPIFFSIniFile::errorKeyNotFound:
//     Serial.print("key not found");
//     break;
//   case SPIFFSIniFile::errorEndOfFile:
//     Serial.print("end of file");
//     break;
//   case SPIFFSIniFile::errorUnknownError:
//     Serial.print("unknown error");
//     break;
//   default:
//     Serial.print("unknown error value");
//     break;
//   }
//   if (eol)
//     Serial.println();
// }

// //-- parseIniString char* --------------------------------------------------------------------------
// bool parseIniString(const SPIFFSIniFile &ini, const char *INI_SECTION, const char *INI_ITEM, 
//                     char *iniBuffer, const size_t &INI_BUFFER_LEN, char *storage, uint16_t maxLen )
// {
//   if ( false == ini.getValue(INI_SECTION, INI_ITEM, iniBuffer, INI_BUFFER_LEN, storage, maxLen ) )
//   {
//     SERIAL_PF("Error parsing: %s / %s => ", INI_SECTION, INI_ITEM); printErrorMessage(ini.getError());
//     return false;
//   }
//   SERIAL_PF("%s = %s\n", INI_ITEM, storage);
//   return true;
// }

// //-- parseIniNumber uint8_t ------------------------------------------------------------------------
// bool parseIniNumber(const SPIFFSIniFile &ini, const char *INI_SECTION, const char *INI_ITEM, 
//                     char *iniBuffer, const size_t &INI_BUFFER_LEN, uint8_t &storage )
// {
//   uint16_t value = 0;
//   if ( false == ini.getValue(INI_SECTION, INI_ITEM, iniBuffer, INI_BUFFER_LEN, value ) )
//   {
//     SERIAL_PF("Error parsing: %s / %s => ", INI_SECTION, INI_ITEM); printErrorMessage(ini.getError());
//     return false;
//   }
//   storage = static_cast<uint8_t>( value );
//   SERIAL_PF("%s = %d\n", INI_ITEM, storage);
//   return true;
// }  

// //-- parseIniNumber int8_t ------------------------------------------------------------------------
// bool parseIniNumber(const SPIFFSIniFile &ini, const char *INI_SECTION, const char *INI_ITEM, 
//                     char *iniBuffer, const size_t &INI_BUFFER_LEN, int8_t &storage )
// {
//   int value = 0; // int comes from the library
//   if ( false == ini.getValue(INI_SECTION, INI_ITEM, iniBuffer, INI_BUFFER_LEN, value ) )
//   {
//     SERIAL_PF("Error parsing: %s / %s => ", INI_SECTION, INI_ITEM); printErrorMessage(ini.getError());
//     return false;
//   }
//   storage = static_cast<int8_t>( value );
//   SERIAL_PF("%s = %d\n", INI_ITEM, storage);
//   return true;
// }  

// //-- parseIniNumber uint16_t -----------------------------------------------------------------------
// bool parseIniNumber(const SPIFFSIniFile &ini, const char *INI_SECTION, const char *INI_ITEM, 
//                     char *iniBuffer, const size_t &INI_BUFFER_LEN, uint16_t &storage )
// {
//   if ( false == ini.getValue(INI_SECTION, INI_ITEM, iniBuffer, INI_BUFFER_LEN, storage ) )
//   {
//     SERIAL_PF("Error parsing: %s / %s => ", INI_SECTION, INI_ITEM); printErrorMessage(ini.getError());
//     return false;
//   }
//   SERIAL_PF("%s = %d\n", INI_ITEM, storage);
//   return true;
// }  

// //-- parseIniNumber int32_t -----------------------------------------------------------------------
// bool parseIniNumber(const SPIFFSIniFile &ini, const char *INI_SECTION, const char *INI_ITEM, 
//                     char *iniBuffer, const size_t &INI_BUFFER_LEN, int32_t &storage )
// {
//   if ( false == ini.getValue(INI_SECTION, INI_ITEM, iniBuffer, INI_BUFFER_LEN, storage ) )
//   {
//     SERIAL_PF("Error parsing: %s / %s => ", INI_SECTION, INI_ITEM); printErrorMessage(ini.getError());
//     return false;
//   }
//   SERIAL_PF("%s = %d\n", INI_ITEM, storage);
//   return true;
// }  

// //-- parseIniNumber float --------------------------------------------------------------------------
// bool parseIniNumber(const SPIFFSIniFile &ini, const char *INI_SECTION, const char *INI_ITEM, 
//                     char *iniBuffer, const size_t &INI_BUFFER_LEN, float &storage )
// {
//   if ( false == ini.getValue(INI_SECTION, INI_ITEM, iniBuffer, INI_BUFFER_LEN, storage ) )
//   {
//     SERIAL_PF("Error parsing: %s / %s => ", INI_SECTION, INI_ITEM); printErrorMessage(ini.getError());
//     return false;
//   }
//   SERIAL_PF("%s = %f\n", INI_ITEM, storage);
//   return true;
// }  

// //-- parseIniNumber bool --------------------------------------------------------------------------
// bool parseIniBool(const SPIFFSIniFile &ini, const char *INI_SECTION, const char *INI_ITEM, 
//                   char *iniBuffer, const size_t &INI_BUFFER_LEN, bool &storage )
// {
//   if ( false == ini.getValue(INI_SECTION, INI_ITEM, iniBuffer, INI_BUFFER_LEN, storage ) )
//   {
//     SERIAL_PF("Error parsing: %s / %s => ", INI_SECTION, INI_ITEM); printErrorMessage(ini.getError());
//     return false;
//   }
//   SERIAL_PF("%s = %s\n", INI_ITEM, ( true == storage ? "true" : "false") );
//   return true;
// }  


// //-- readIniFile -----------------------------------------------------------------------------------
// bool readIniFile()
// {
//   const size_t INI_BUFFER_LEN = 512;
//   char iniBuffer[INI_BUFFER_LEN];

//   //-- Initialise, load and validate the Ini file --------------------------------------------------
//   // Open the Ini file  
//   SPIFFSIniFile ini(INI_FILENAME, (char *)"r" );
//   if (false == ini.open() ) 
//   {
//     SERIAL_PF( "Ini file '%s' does not exisit\n", INI_FILENAME);
//     return false;
//   }
//   SERIAL_PLN("Ini file exists");

//   // Check the file is valid. This can be used to warn if any lines
//   // are longer than the buffer.
//   if ( false == ini.validate(iniBuffer, INI_BUFFER_LEN) )
//   {
//     SERIAL_PF( "ini file '%s' is not valid\n", ini.getFilename() );
//     printErrorMessage(ini.getError());
//     return false;
//   }

//   uint8_t res = 0;

//   //-- Process the ini file content ----------------------------------------------------------------
//     // [network]
//     SERIAL_PF("[%s]\n", INI_NET_SECTION);

//     bool value = false;
//     res += !parseIniBool(ini, INI_NET_SECTION, INI_NET_WIFI_ENABLED, iniBuffer, INI_BUFFER_LEN, value ); 
//       iniFileStorage.wifi_enabled = value; 
//     res += !parseIniString(ini, INI_NET_SECTION, INI_NET_WIFI_AP_SSID,           iniBuffer, INI_BUFFER_LEN, iniFileStorage.wifi_ap_ssid, MAX_LEN_SSID ); 
//     res += !parseIniString(ini, INI_NET_SECTION, INI_NET_WIFI_AP_PWD,            iniBuffer, INI_BUFFER_LEN, iniFileStorage.wifi_ap_pwd,  MAX_LEN_PWD  ); 
//     res += !parseIniNumber(ini, INI_NET_SECTION, INI_NET_WIFI_CON_DELAY,         iniBuffer, INI_BUFFER_LEN, iniFileStorage.wifi_con_delay); 
//     res += !parseIniNumber(ini, INI_NET_SECTION, INI_NET_WIFI_MAX_CON_ATTEMPTS,  iniBuffer, INI_BUFFER_LEN, iniFileStorage.wifi_max_con_attempts); 

//     //-- BSSID
//     if ( false == ini.getMACAddress(INI_NET_SECTION, INI_NET_WIFI_AP_BSSID, iniBuffer, INI_BUFFER_LEN, iniFileStorage.wifi_ap_bssid ) )
//     {
//       SERIAL_PF("Error parsing: %s / %s => ", INI_NET_SECTION, INI_NET_WIFI_AP_BSSID);
//       printErrorMessage(ini.getError());
//       return false;
//     }
//     const uint8_t *mac = iniFileStorage.wifi_ap_bssid;
//     SERIAL_PF("wifi_ap_bssid: %x-%x-%x-%x-%x-%x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

//     //-- Channel
//     res += !parseIniNumber(ini, INI_NET_SECTION, INI_NET_WIFI_AP_CHANNEL, iniBuffer, INI_BUFFER_LEN, iniFileStorage.wifi_ap_channel); 

//   //------------------------------------------------------------------
//     // [data upload]
//     SERIAL_PF("[%s]\n", INI_DATA_SECTION);

//     res += !parseIniNumber(ini, INI_DATA_SECTION, INI_DATA_FREQ,               iniBuffer, INI_BUFFER_LEN, iniFileStorage.upload_freq); 
//     res += !parseIniString(ini, INI_DATA_SECTION, INI_DATA_DEVICE_ID,          iniBuffer, INI_BUFFER_LEN, iniFileStorage.device_id, MAX_LEN_DEVICE_ID ); 
//     res += !parseIniNumber(ini, INI_DATA_SECTION, INI_DATA_UPLOAD_TIMEOUT,     iniBuffer, INI_BUFFER_LEN, iniFileStorage.upload_timeout ); 
//     res += !parseIniString(ini, INI_DATA_SECTION, INI_DATA_SERVER_REQUEST_URL, iniBuffer, INI_BUFFER_LEN, iniFileStorage.server_request_url, MAX_LEN_SERVER_REQUEST_URL ); 
//     res += !parseIniString(ini, INI_DATA_SECTION, INI_DATA_MEASUREMENT_NAME,   iniBuffer, INI_BUFFER_LEN, iniFileStorage.data_measurement_name, MAX_LEN_DATA_MEASUREMENT_NAME ); 
  
//   //------------------------------------------------------------------
//     // [display]
//     SERIAL_PF("[%s]\n", INI_DISP_SECTION);
//     res += !parseIniNumber(ini, INI_DISP_SECTION, INI_DISP_CONTRAST, iniBuffer, INI_BUFFER_LEN, iniFileStorage.display_contrast); 
//     res += !parseIniBool(ini, INI_DISP_SECTION, INI_DISP_ROTATION, iniBuffer, INI_BUFFER_LEN, value ); 
//       iniFileStorage.display_rotation = value;
  
//   //------------------------------------------------------------------
//     // [bme sensor]
//     SERIAL_PF("[%s]\n", INI_BME_SENSOR_SECTION);
//     res += !parseIniNumber(ini, INI_BME_SENSOR_SECTION, INI_BME_SENSOR_TEMP_CORRECTION,  iniBuffer, INI_BUFFER_LEN, iniFileStorage.bme_sensor_temp_correction ); 

//   //------------------------------------------------------------------
//     // [battery]
//     SERIAL_PF("[%s]\n", INI_BATTERY_SECTION);
//     res += !parseIniNumber(ini, INI_BATTERY_SECTION, INI_BATTERY_MIN_LEVEL, iniBuffer, INI_BUFFER_LEN, iniFileStorage.batteryMinLevel ); 
//     res += !parseIniNumber(ini, INI_BATTERY_SECTION, INI_BATTERY_MAX_LEVEL, iniBuffer, INI_BUFFER_LEN, iniFileStorage.batteryMaxLevel ); 


//   ini.close();
//   return !res;
// }

// //-- writeIniFile ----------------------------------------------------------------------------------
// bool writeIniFile()
// {
//   SERIAL_PLN("Writing the new Ini file.");

//   // Create a back-up file from the original
//   if (false == SPIFFS.rename(INI_FILENAME, INI_FILENAME_BACKUP) )
//   {
//     SERIAL_PLN("Error creating ini file back-up.");
//     return false;
//   }

//   // Create a new ini file
//   File iniFile = SPIFFS.open(INI_FILENAME, "w");
//   if ( false == iniFile )
//   {
//     SERIAL_PLN("Error creating ini file.");
//     return false;
//   }

//   // [network]
//   iniFile.printf("[%s]\n", INI_NET_SECTION);
//   iniFile.printf("%s=%s\n", INI_NET_WIFI_ENABLED, (true == iniFileStorage.wifi_enabled ? "true" : "false" ) );
//   iniFile.printf("%s=%s\n", INI_NET_WIFI_AP_SSID, iniFileStorage.wifi_ap_ssid );
//   iniFile.printf("%s=%s\n", INI_NET_WIFI_AP_PWD, iniFileStorage.wifi_ap_pwd );
//   const uint8_t *mac = iniFileStorage.wifi_ap_bssid; 
//   iniFile.printf("%s=%x-%x-%x-%x-%x-%x\n", INI_NET_WIFI_AP_BSSID, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5] );
//   iniFile.printf("%s=%d\n", INI_NET_WIFI_AP_CHANNEL, iniFileStorage.wifi_ap_channel );
//   iniFile.printf("%s=%d\n", INI_NET_WIFI_CON_DELAY, iniFileStorage.wifi_con_delay );
//   iniFile.printf("%s=%d\n", INI_NET_WIFI_MAX_CON_ATTEMPTS, iniFileStorage.wifi_max_con_attempts );

//   // [data upload]
//   iniFile.printf("[%s]\n", INI_DATA_SECTION);
//   iniFile.printf("%s=%d\n", INI_DATA_FREQ, iniFileStorage.upload_freq );
//   iniFile.printf("%s=%s\n", INI_DATA_DEVICE_ID, iniFileStorage.device_id );
//   iniFile.printf("%s=%d\n", INI_DATA_UPLOAD_TIMEOUT, iniFileStorage.upload_timeout );
//   iniFile.printf("%s=%s\n", INI_DATA_SERVER_REQUEST_URL, iniFileStorage.server_request_url);
//   iniFile.printf("%s=%s\n", INI_DATA_MEASUREMENT_NAME, iniFileStorage.data_measurement_name);
  
//   // [display]
//   iniFile.printf("[%s]\n", INI_DISP_SECTION);
//   iniFile.printf("%s=%d\n", INI_DISP_CONTRAST, iniFileStorage.display_contrast );
//   iniFile.printf("%s=%s\n", INI_DISP_ROTATION, (true == iniFileStorage.display_rotation ? "true" : "false" ) );

//   // [bme sensor]
//   iniFile.printf("[%s]\n", INI_BME_SENSOR_SECTION);
//   iniFile.printf("%s=%1.1f\n", INI_BME_SENSOR_TEMP_CORRECTION, iniFileStorage.bme_sensor_temp_correction );

//   // [battery]
//   iniFile.printf("[%s]\n", INI_BATTERY_SECTION);
//   iniFile.printf("%s=%d\n", INI_BATTERY_MIN_LEVEL, iniFileStorage.batteryMinLevel );
//   iniFile.printf("%s=%d\n", INI_BATTERY_MAX_LEVEL, iniFileStorage.batteryMaxLevel );

//   iniFile.close();

//   // SERIAL_PLN("Reading back the Ini file.");
//   // iniFile = SPIFFS.open(INI_FILENAME, "r");
//   // if ( false == iniFile )
//   // {
//   //   SERIAL_PLN("Error opening ini file.");
//   //   return false;
//   // }

//   // String str;

//   // do
//   // {
//   //   str = iniFile.readStringUntil('\n');
//   //   SERIAL_PLN( str.c_str() );
//   // } 
//   // while ( str.length() != 0 );

//   iniFile.close();

//   // Remove the back-up file
//   if ( false == SPIFFS.remove(INI_FILENAME_BACKUP) )
//   {
//     SERIAL_PLN("Error removing back-up file.");
//   }

//   return true;
// }

//-- generateJsFile --------------------------------------------------------------------------------

// bool generateJsFile()
// {
//   SERIAL_PLN("Generating a new js file.");

//   // Create a back-up file from the original
//   if (false == SPIFFS.rename(JS_FILENAME, JS_FILENAME_BACKUP) )
//   {
//     SERIAL_PLN("Error creating js file back-up.");
//     return false;
//   }

//   // Create a new js file
//   File jsFile = SPIFFS.open(JS_FILENAME, "w");
//   if ( false == jsFile )
//   {
//     SERIAL_PLN("Error creating js file.");
//     return false;
//   }

//   jsFile.println( F("function setValues()\n{") );
//   // [network]
//   jsFile.printf( JS_FILE_LINE, INI_NET_WIFI_ENABLED, JS_FILE_CHECKED, (true == iniFileStorage.wifi_enabled ? "true" : "false" ) );
//   jsFile.printf( JS_FILE_LINE, INI_NET_WIFI_ENABLED, JS_FILE_VALUE,   (true == iniFileStorage.wifi_enabled ? "true" : "false" ) );
//   jsFile.printf( JS_FILE_LINE_QUOTES_S, INI_NET_WIFI_AP_SSID, JS_FILE_VALUE, iniFileStorage.wifi_ap_ssid );
//   jsFile.printf( JS_FILE_LINE_QUOTES_S, INI_NET_WIFI_AP_PWD,  JS_FILE_VALUE, iniFileStorage.wifi_ap_pwd );
  
//   // [data upload]
//   jsFile.printf( JS_FILE_LINE_QUOTES_D, INI_DATA_FREQ,               JS_FILE_VALUE, iniFileStorage.upload_freq );
//   jsFile.printf( JS_FILE_LINE_QUOTES_S, INI_DATA_DEVICE_ID,          JS_FILE_VALUE, iniFileStorage.device_id );
//   jsFile.printf( JS_FILE_LINE_QUOTES_D, INI_DATA_UPLOAD_TIMEOUT,     JS_FILE_VALUE, iniFileStorage.upload_timeout );
//   jsFile.printf( JS_FILE_LINE_QUOTES_S, INI_DATA_SERVER_REQUEST_URL, JS_FILE_VALUE, iniFileStorage.server_request_url );
//   jsFile.printf( JS_FILE_LINE_QUOTES_S, INI_DATA_MEASUREMENT_NAME,   JS_FILE_VALUE, iniFileStorage.data_measurement_name );
  
//   // [display]
//   jsFile.printf( JS_FILE_LINE_QUOTES_D, INI_DISP_CONTRAST,    JS_FILE_VALUE, iniFileStorage.display_contrast );
//   jsFile.printf( JS_FILE_LINE, INI_DISP_ROTATION, JS_FILE_CHECKED, (true == iniFileStorage.display_rotation ? "true" : "false" ) );
  
//   // [bme sensor]
//   jsFile.printf( JS_FILE_LINE_QUOTES_F1_1, INI_BME_SENSOR_TEMP_CORRECTION, JS_FILE_VALUE, iniFileStorage.bme_sensor_temp_correction );


//   // [battery]
//   jsFile.printf( JS_FILE_LINE_QUOTES_D, INI_BATTERY_MIN_LEVEL, JS_FILE_VALUE, iniFileStorage.batteryMinLevel );
//   jsFile.printf( JS_FILE_LINE_QUOTES_D, INI_BATTERY_MAX_LEVEL, JS_FILE_VALUE, iniFileStorage.batteryMaxLevel );


//   jsFile.println("}");

//   jsFile.close();

//   // Remove the back-up file
//   if ( false == SPIFFS.remove(JS_FILENAME_BACKUP) )
//   {
//     SERIAL_PLN("Error removing js back-up file.");
//   }

//   return true;
// }


// //-- handleSetupMode -------------------------------------------------------------------------------
// void handleSetupMode()
// {
//   SERIAL_PLN("Config mode active. Starting Access Point mode.");
//   printScreenLine("Config mode started.");
  
//   wifiStaConnectHandler = WiFi.onSoftAPModeStationConnected(onSTAConnected);
//   if ( false == iniFileStorage.wifi_enabled )
//   {
//     WiFi.forceSleepWake();
//     delay(1000);
//     WiFi.mode(WIFI_AP);
//     do
//     {
//       SERIAL_PLN("Waking up modem.");
//       screenAPinit();
//       delay(1000);
//     }
//     while ( false == WiFi.softAP( AP_SSID, AP_PWD ) );
//   }
//   else
//   {
//     WiFi.mode(WIFI_AP);
//     WiFi.softAP( AP_SSID, AP_PWD );
//     delay(1000);
//   }

//   SERIAL_PF("AP IP address: %s\n", WiFi.softAPIP().toString().c_str() );
//   screenAPStarted();

//   server.onNotFound([]() {                            // If the client requests any URI
//   if (!handleFileRead(server.uri()))                  // send it if it exists
//     server.send(404, "text/plain", "404: Not Found"); // otherwise, respond with a 404 (Not Found) error
//   });

//   server.begin();                           // Start the server
//   SERIAL_PLN( F("HTTP server started") );

//   setupOTA();
//   SERIAL_PLN( F("OTA started") );
//   screenAPStarted( true );
// }

// -- connectToWiFiV3 ------------------------------------------------------------------------------
// Connects to the known WiFi AP
// If the BSSID is known let's try to connect to it. If it is not known, use just the SSID and PWD
// If connecting to the AP is successful, the BSSID is saved with the channel -> ConfigChanged -> true
// If connecting to the AP is usuccessful, clear the BSSID and the channel -> ConfigChanged -> true
// return false if connection is unsuccessful / true if successful
// bool connectToWiFiV3(bool &isConfigChanged)
// {
//   isConfigChanged = false;

//   WiFi.persistent( true );
//   SERIAL_PF("\n\nconnecting to :%s", iniFileStorage.wifi_ap_ssid);
//   WiFi.mode(WIFI_STA);
//   yield();

//   uint16_t bssid_check = 0;
//   for ( uint8_t i = 0; i < 6; ++i ) { bssid_check += iniFileStorage.wifi_ap_bssid[i]; }

//   if ( 0 == bssid_check )
//   { // no known BSSID
//     SERIAL_P(" (No BSSID) ");
//     WiFi.begin( iniFileStorage.wifi_ap_ssid, iniFileStorage.wifi_ap_pwd );
//     //iniFileStorage.wifi_con_delay = iniFileStorage.wifi_con_delay * 2;
//     //  60 attempts is the minimum with 150 ms delay
//     if ( 240 > iniFileStorage.wifi_max_con_attempts ) { iniFileStorage.wifi_max_con_attempts += 60; }
//     else { iniFileStorage.wifi_max_con_attempts = 240; }
//     SERIAL_PF("Con_attempts: %d; Con_delays: %d\n", iniFileStorage.wifi_max_con_attempts, iniFileStorage.wifi_con_delay);
    
//     //iniFileStorage.wifi_max_con_attempts = iniFileStorage.wifi_max_con_attempts * 2;
//   }
//   else
//   {
//     SERIAL_P(" (BSSID known) ");
//     WiFi.begin(iniFileStorage.wifi_ap_ssid, 
//                iniFileStorage.wifi_ap_pwd, 
//                iniFileStorage.wifi_ap_channel, 
//                iniFileStorage.wifi_ap_bssid, 
//                true);
//   }


//   // display the wifi icon
//   g_dispIcons.fields.wifi = true;
//   drawScreen();

//   uint16_t wifiConAttCntr = iniFileStorage.wifi_max_con_attempts;
//   while (WiFi.status() != WL_CONNECTED) 
//   {
//     delay( iniFileStorage.wifi_con_delay - 54 );
//     --wifiConAttCntr;
//     if ( 0 == wifiConAttCntr ) 
//     { 
//       memset(iniFileStorage.wifi_ap_bssid, 0, sizeof( uint8_t) * 6 ); // clean out the BSSID
//       iniFileStorage.wifi_ap_channel = 0;
//       isConfigChanged = true;
      
//       return false; 
//     }
//     SERIAL_P( F(".") );

//     //-- blink Wifi sign
//     g_dispIcons.fields.wifi = !g_dispIcons.fields.wifi;
//     drawScreen();
//   }

//   g_dispIcons.fields.wifi = true;
//   drawScreen();

//   SERIAL_PF("\nWiFi connected. IP address: %s\n", WiFi.localIP().toString().c_str() );
//   if ( 0 == bssid_check )
//   {
//     isConfigChanged = true;

//     const uint8_t *mac = WiFi.BSSID();
//     memcpy( iniFileStorage.wifi_ap_bssid, mac, sizeof( uint8_t ) * 6 );
//     SERIAL_PF("wifi_ap_bssid: %x-%x-%x-%x-%x-%x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

//     // iniFileStorage.wifi_con_delay /= 2;
//     // iniFileStorage.wifi_max_con_attempts /= 2;
//   }

//   if ( WiFi.channel() != iniFileStorage.wifi_ap_channel ) // The channel value has changed
//   { 
//     isConfigChanged = true; 
//     iniFileStorage.wifi_ap_channel = WiFi.channel();
//   }

//   SERIAL_PF("AP-BSSID: %s\n", WiFi.BSSIDstr().c_str() );
//   SERIAL_PF("AP Channel: %d\n", WiFi.channel() );
//   SERIAL_PF("Con_attempts: %d; Con_delays: %d\n", iniFileStorage.wifi_max_con_attempts, iniFileStorage.wifi_con_delay);

//   return true;
// }

// //-- parseSubmit char* -----------------------------------------------------------------------------
// bool parseSubmit(ESP8266WebServer& server, String &error, const char *INI_ITEM, char *storage, const uint8_t MAX_LEN)
// {
//   if ( true == server.hasArg( INI_ITEM ) ) 
//   {
//     SERIAL_PF("%s = %s\n", INI_ITEM, server.arg( INI_ITEM ).c_str() );
//     strcpy( storage, server.arg(INI_ITEM).substring(0, MAX_LEN).c_str() );
//     SERIAL_PF("\tStorage: \"%s\"\n", storage );
//     return true;
//   } else { error += INI_ITEM; error += NOT_FOUND; return false; }
// }

// //-- parseSubmit uint8_t ---------------------------------------------------------------------------
// bool parseSubmit(ESP8266WebServer& server, String &error, const char *INI_ITEM, uint8_t &storage)
// {
//   if ( true == server.hasArg( INI_ITEM ) ) 
//   {
//     SERIAL_PF("%s = %s\n", INI_ITEM, server.arg( INI_ITEM ).c_str() );
//     storage = static_cast<uint8_t>( atoi( server.arg(INI_ITEM).c_str() ) );
//     return true;
//   } else { error += INI_ITEM; error += NOT_FOUND; return false;}
// }

// //-- parseSubmit uint16_t --------------------------------------------------------------------------
// bool parseSubmit(ESP8266WebServer& server, String &error, const char *INI_ITEM, uint16_t &storage)
// {
//   if ( true == server.hasArg( INI_ITEM ) ) 
//   {
//     SERIAL_PF("%s = %s\n", INI_ITEM, server.arg( INI_ITEM ).c_str() );
//     storage = static_cast<uint16_t>( atoi( server.arg(INI_ITEM).c_str() ) );
//     return true;
//   } else { error += INI_ITEM; error += NOT_FOUND; return false;}
// }

// //-- parseSubmit float -----------------------------------------------------------------------------
// bool parseSubmit(ESP8266WebServer& server, String &error, const char *INI_ITEM, float &storage)
// {
//   if ( true == server.hasArg( INI_ITEM ) ) 
//   {
//     SERIAL_PF("%s = %s\n", INI_ITEM, server.arg( INI_ITEM ).c_str() );
//     storage = atof( server.arg(INI_ITEM).c_str() );
//     return true;
//   } else { error += INI_ITEM; error += NOT_FOUND; return false;}
// }

// //-- parseSubmit bool ------------------------------------------------------------------------------
// bool parseSubmit(ESP8266WebServer& server, String &error, const char *INI_ITEM, bool &storage)
// {
//   if ( true == server.hasArg( INI_ITEM ) ) 
//   {
//     SERIAL_PF("%s = %s\n", INI_ITEM, server.arg( INI_ITEM ).c_str() );
//     storage = true;
//     return true;
//   } 
//   else 
//   { 
//     SERIAL_PF("%s = %s (Checkbox not present)\n", INI_ITEM, "false" );
//     storage = false;
//     return false;
//   }
// }

// //-- processSubmit ---------------------------------------------------------------------------------
// bool processSubmit(String &error)
// {
//   bool result = true;

//   error = "";
//   bool value = false;

//   // [network]
//   parseSubmit(server, error, INI_NET_WIFI_ENABLED, value );  iniFileStorage.wifi_enabled = value; 
//   parseSubmit(server, error, INI_NET_WIFI_AP_SSID, iniFileStorage.wifi_ap_ssid, MAX_LEN_SSID );
//     memset(iniFileStorage.wifi_ap_bssid, 0, sizeof(uint8_t) * 6 ); // clean out BSSID
//   parseSubmit(server, error, INI_NET_WIFI_AP_PWD,  iniFileStorage.wifi_ap_pwd, MAX_LEN_PWD );

//   // [data upload]
//   parseSubmit(server, error, INI_DATA_FREQ,               iniFileStorage.upload_freq );
//   parseSubmit(server, error, INI_DATA_DEVICE_ID,          iniFileStorage.device_id, MAX_LEN_DEVICE_ID );
//   parseSubmit(server, error, INI_DATA_UPLOAD_TIMEOUT,     iniFileStorage.upload_timeout );
//   parseSubmit(server, error, INI_DATA_SERVER_REQUEST_URL, iniFileStorage.server_request_url, MAX_LEN_SERVER_REQUEST_URL );
//   parseSubmit(server, error, INI_DATA_MEASUREMENT_NAME,   iniFileStorage.data_measurement_name, MAX_LEN_DATA_MEASUREMENT_NAME );

//   // [display]
//   parseSubmit(server, error, INI_DISP_CONTRAST,           iniFileStorage.display_contrast );
//   parseSubmit(server, error, INI_DISP_ROTATION, value );  iniFileStorage.display_rotation = value; 

//   // [bme sensor]
//   parseSubmit(server, error, INI_BME_SENSOR_TEMP_CORRECTION, iniFileStorage.bme_sensor_temp_correction); 

//   // [battery]
//   parseSubmit(server, error, INI_BATTERY_MIN_LEVEL, iniFileStorage.batteryMinLevel); 
//   parseSubmit(server, error, INI_BATTERY_MAX_LEVEL, iniFileStorage.batteryMaxLevel); 


//   if ( 0 < error.length() ) { SERIAL_PLN(error); result = false; }

//   result = generateJsFile(); // New config, new js file
//   result |= writeIniFile();  // New config, new ini file

//   u8g2.setContrast(iniFileStorage.display_contrast); // 155 - Home; 127 - Office
//   drawScreen();

//   return result;
// }

// //-- getContentType --------------------------------------------------------------------------------
// // convert the file extension to the MIME type
// String getContentType(String filename)
// { 
//   if      ( filename.endsWith(".html") ) { return "text/html";              } 
//   else if ( filename.endsWith(".css")  ) { return "text/css";               } 
//   else if ( filename.endsWith(".js")   ) { return "application/javascript"; } 
//   else if ( filename.endsWith(".ico")  ) { return "image/x-icon";           } 
//   return "text/plain";
// }

// //-- handleFileRead --------------------------------------------------------------------------------
// // send the right file to the client (if it exists)
// bool handleFileRead(String path) 
// { 
//   SERIAL_PLN("handleFileRead: " + path);
//   if ( path.endsWith("/") ) { path += "index.html"; }   // If a folder is requested, send the index file
  
//   if ( path.endsWith("/restart") )
//   { 
//     String response (
//       "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01//EN\">"
//       "<html><center><h1>Restart</h1></center></html>"
//     );
    
//     server.send(200, "text/html", response);
//     delay(1000);
//     ESP.restart();    // Restart the device
//   }

//   if ( path.endsWith("/submit.html") ) 
//   { 
//     String err; 
//     if (false == processSubmit( err ) )
//     {
//       err += "\nError processing and saving the configuration!";
//       server.send(200, "text/plain", err);

//       return false;
//     }
//   } 

//   String contentType = getContentType(path);            // Get the MIME type
//   if ( SPIFFS.exists(path) )                            // If the file exists 
//   {
//     File file = SPIFFS.open(path, "r");                 // Open it
//     //size_t sent = 
//     server.streamFile(file, contentType); // And send it to the client
//     file.close();                                       // Then close the file again
//     return true;
//   }
//   SERIAL_PLN( F("\tFile not found") );
//   return false;                                         // If the file doesn't exist, return false
// }

// // ##############################################
// namespace influx
// {
//   // InfluxDB Cloud URL
//   const char SERVER_REQ_URL[] = "/api/v2/write?org=mine&bucket=ts_bucket&precision=s";
//   const char SERVER_NAME[]    = "eu-central-1-1.aws.cloud2.influxdata.com"; // AWS EU region InfluxDB
//   const uint16_t SERVER_PORT  = 443;               // Secure port for HTTP
  
//   // OFFICE
//   // officeThermoSensor,deviceID=TS-Test temperature=23.2,humidity=25.7,pressure=1021.42,battery=84i,uptime=11.103
//   // const char PAYLOAD_STRING[] = "officeThermoSensor,deviceID=%s temperature=%d.%d,humidity=%d.%d,pressure=%d.%d,battery=%di,uptime=%llu.%llu";
  

//   // HOME
//   //const char MEASUREMENT_NAME[] = "homeThermoSensor";
//   //const char MEASUREMENT_NAME[] = "officeThermoSensor";
//   const char MEASUREMENT_NAME[] = "devThermoSensor";

// };

// //== REPORTING =====================================================================================
// //-- SUBMIT DATA -----------------------------------------------------------------------------------
// // Upload the data to the server
// bool submitData_Influx()
// {
//   SERIAL_PLN("Initiating data upload.");
//   // Use WiFiClientSecure class to create TLS connection
//   BearSSL::WiFiClientSecure tcpClient;
//   tcpClient.setInsecure();

//   SERIAL_PF( "\nConnecting to: %s:%d\n", influx::SERVER_NAME, influx::SERVER_PORT);

//   g_dispIcons.fields.inet = true;
//   drawScreen();

//   if ( !tcpClient.connect( influx::SERVER_NAME, influx::SERVER_PORT ) ) 
//   {
//     g_dispIcons.fields.dislike = true;
//     drawScreen();

//     SERIAL_PLN( F("Connection failed") );
//     return false;
//   }
//   drawScreen();


//   //SERIAL_P( F("Requesting URL: ") ); SERIAL_PLN( SERVER_REQ_URL );
//   //SERIAL_P( F("Requesting URL: ") ); SERIAL_PLN( iniFileStorage.server_request_url );
//   SERIAL_P( F("Requesting URL: ") ); SERIAL_PLN( influx::SERVER_REQ_URL );
//   SERIAL_PLN();
//   SERIAL_PLN( F("REQUEST:") );

//   g_dispIcons.fields.upload = true;
//   drawScreen();

//   // Send HTTPS request
//   String strReq;
//   //strReq += F("POST ");   strReq += SERVER_REQ_URL;  strReq += F(" HTTP/1.1\r\n");
//   //strReq += F("POST ");   strReq += iniFileStorage.server_request_url;  strReq += F(" HTTP/1.1\r\n");
//   strReq += F("POST ");   strReq += influx::SERVER_REQ_URL;  strReq += F(" HTTP/1.1\r\n");
//   strReq += F("Host: ");  strReq += influx::SERVER_NAME;     strReq += F(" \r\n");

//   strReq += F("User-Agent: ESP8266 Sensor Agent\r\n");
//   strReq += F("Connection: close\r\n");
//   strReq += F("Authorization: Token ");
//   strReq += influx::SERVER_TOKEN; strReq += F(" \r\n");

//   //strReq += F("Content-Type: application/x-www-form-urlencoded\r\n");

//   char payloadBuffer[201]     = { 0 };
//   uint64_t timeDiff = millis() - g_timeStamp;
//   sprintf( payloadBuffer, 
//     influx::PAYLOAD_STRING,  // "%s,deviceID=%s temperature=%.2f,humidity=%.2f,pressure=%.2f,battery=%di,uptime=%llu.%llu"
//     iniFileStorage.data_measurement_name, // e.g. "homeThermoSensor"
//     iniFileStorage.device_id, //DEVICE_ID,
//     g_temp, // Temperature
//     g_hum,  // Humidity
//     g_pres, // Air pressure
//     g_battery, // Battery level
//     ( timeDiff / 1000), ((timeDiff / 100) - timeDiff / 1000) // Uptime
//   );
  
//   uint16_t len = strlen( payloadBuffer );

//   strReq += F("Content-Length: ");
//   char payloadLength[5] = { 0 };
//   sprintf( payloadLength, "%d", len );
//   strReq += payloadLength;
//   strReq += F("\r\n\r\n");
//   strReq += payloadBuffer;
//   strReq += F("\r\n");
//   strReq += F("\r\n");

//   if ( tcpClient.write(  strReq.c_str() ) == 0 ) 
//   {
//     g_dispIcons.fields.dislike = true;
//     drawScreen();

//     SERIAL_PLN( F("Failed to send request.") );
//     return false;
//   }
//   SERIAL_PLN( F("Request sent.") ) ;
//   SERIAL_PLN( strReq.c_str() );
  
//   // Check HTTP status
//   char httpStatus[32] = {0};
//   tcpClient.readBytesUntil('\r', httpStatus, sizeof( httpStatus ));
//   if ( strcmp( httpStatus, "HTTP/1.1 204 No Content" ) != 0) 
//   {
//     g_dispIcons.fields.dislike = true;
//     drawScreen();

//     SERIAL_P( F("Unexpected HTTP response: ") ); SERIAL_PLN( httpStatus );
//     return false;
//   }
//   else
//   {
//     SERIAL_PLN( F("Received HTTP 204 No Content."));
//   }
  

//   // Skip HTTP headers
//   const char endOfHeaders[] = "\r\n\r\n";
//   if ( !tcpClient.find( endOfHeaders ) ) 
//   {
//     g_dispIcons.fields.dislike = true;
//     drawScreen();

//     SERIAL_PLN( F("Invalid response. Cannot find \\r\\n\\r\\n") );
//     return false;
//   }

//   return true;
// }
