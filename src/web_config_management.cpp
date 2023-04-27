#include <web_config_management.h>
// #include <config_file_management.h>


// #include <ESP8266WebServer.h>
#include <LittleFS.h>
// #include <FS.h>

#include "sensor_ini_file_storage.h"
#include "sensor_config_file_management.h"

//-- Logging
//#define GSI_DEBUG
#include "GSiDebug.h"

using namespace sensor;

//-- JS FILE SETTINGS AND CONSTANTS ----------------------------------------------------------------
const char JS_FILENAME[]           = "/sensor_config.js";
const char JS_FILENAME_BACKUP[]    = "/sensor_config.js.bu";
const char JS_FILE_LINE[]          = "\tdocument.getElementById(\"%s\").%s = %s;\n";
const char JS_FILE_LINE_QUOTES_S[] = "\tdocument.getElementById(\"%s\").%s = \"%s\";\n";
const char JS_FILE_LINE_QUOTES_D[] = "\tdocument.getElementById(\"%s\").%s = \"%d\";\n";
const char JS_FILE_LINE_QUOTES_F[] = "\tdocument.getElementById(\"%s\").%s = \"%f\";\n";
const char JS_FILE_LINE_QUOTES_F1_1[] = "\tdocument.getElementById(\"%s\").%s = \"%1.1f\";\n";

const char JS_FILE_CHECKED[]       = "checked";
const char JS_FILE_VALUE[]         = "value";
const char JS_FILE_STYLE_DISPLAY[] = "style.display";

const char JS_FILE_NONE[]          = "none";
const char JS_FILE_BLOCK[]         = "block";

const char NOT_FOUND[] = " not found\n";

//-- handleFileRead --------------------------------------------------------------------------------
// send the right file to the client (if it exists)
bool WebConfigManagement::handleFileRead(String path, ESP8266WebServer& server, SensorIniFileStorage &iniFileStorage) 
{ 
  SERIAL_PLN("handleFileRead: " + path);
  if ( path.endsWith("/") ) { path += "index.html"; }   // If a folder is requested, send the index file
  
  if ( path.endsWith("/restart") ) 
  { 
    String response (
      "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01//EN\">"
      "<html><center><h1>Restart</h1></center></html>"
    );
    
    server.send(200, "text/html", response);
    delay(1000);
    ESP.restart();    // Restart the device
  }

  if ( path.endsWith("/submit.html") || path.endsWith("/submit_en.html") ) 
  { 
    String err; 
    if (false == processSubmit( server, err, iniFileStorage ) )
    {
      err += "\nError processing and saving the configuration!";
      server.send(200, "text/plain", err);

      return false;
    }
  } 

  String contentType = getContentType(path);            // Get the MIME type
  // if ( SPIFFS.exists(path) )                            // If the file exists 
  if ( LittleFS.exists(path) )                            // If the file exists 
  {
    // File file = SPIFFS.open(path, "r");                 // Open it
    File file = LittleFS.open(path, "r");                 // Open it
    /*size_t sent =*/ server.streamFile(file, contentType); // And send it to the client
    file.close();                                       // Then close the file again
    return true;
  }
  SERIAL_PLN( F("\tFile not found") );
  return false;                                         // If the file doesn't exist, return false
}


//-- getContentType --------------------------------------------------------------------------------
// convert the file extension to the MIME type
String WebConfigManagement::getContentType(String filename)
{ 
  if      ( filename.endsWith(".html") ) { return "text/html";              } 
  else if ( filename.endsWith(".css")  ) { return "text/css";               } 
  else if ( filename.endsWith(".js")   ) { return "application/javascript"; } 
  else if ( filename.endsWith(".ico")  ) { return "image/x-icon";           } 
  return "text/plain";
}


//-- generateJsFile --------------------------------------------------------------------------------
bool WebConfigManagement::generateJsFile(const SensorIniFileStorage &iniFileStorage)
{
  SERIAL_PLN("Generating a new js file.");

  // Create a back-up file from the original
  if (false == LittleFS.rename(JS_FILENAME, JS_FILENAME_BACKUP) )
  {
    SERIAL_PLN("Error creating js file back-up.");
    return false;
  }

  // Create a new js file
  File jsFile = LittleFS.open(JS_FILENAME, "w");
  if ( false == jsFile )
  {
    SERIAL_PLN("Error creating js file.");
    return false;
  }

  jsFile.println( F("function setValues()\n{") );
  // [network]
  jsFile.printf( JS_FILE_LINE, INI_NET_WIFI_ENABLED, JS_FILE_CHECKED, (true == iniFileStorage.wifi_enabled ? "true" : "false" ) );
  jsFile.printf( JS_FILE_LINE, INI_NET_WIFI_ENABLED, JS_FILE_VALUE,   (true == iniFileStorage.wifi_enabled ? "true" : "false" ) );
  jsFile.printf( JS_FILE_LINE_QUOTES_S, INI_NET_WIFI_AP_SSID, JS_FILE_VALUE, iniFileStorage.wifi_ap_ssid );
  jsFile.printf( JS_FILE_LINE_QUOTES_S, INI_NET_WIFI_AP_PWD,  JS_FILE_VALUE, iniFileStorage.wifi_ap_pwd );
  jsFile.printf( JS_FILE_LINE_QUOTES_D, INI_NET_WIFI_CON_DELAY,         JS_FILE_VALUE, iniFileStorage.wifi_con_delay );
  jsFile.printf( JS_FILE_LINE_QUOTES_D, INI_NET_WIFI_MAX_CON_ATTEMPTS,  JS_FILE_VALUE, iniFileStorage.wifi_max_con_attempts );
  
  // [data upload]
  jsFile.printf( JS_FILE_LINE_QUOTES_D, INI_DATA_FREQ,               JS_FILE_VALUE, iniFileStorage.upload_freq );
  jsFile.printf( JS_FILE_LINE_QUOTES_D, INI_DATA_UPLOAD_TIMEOUT,     JS_FILE_VALUE, iniFileStorage.upload_timeout );
  jsFile.printf( JS_FILE_LINE_QUOTES_S, INI_DATA_DEVICE_ID,          JS_FILE_VALUE, iniFileStorage.device_id );
  jsFile.printf( JS_FILE_LINE_QUOTES_S, INI_DATA_LOCATION,           JS_FILE_VALUE, iniFileStorage.location );
  jsFile.printf( JS_FILE_LINE_QUOTES_S, INI_DATA_MEASUREMENT_ORG,    JS_FILE_VALUE, iniFileStorage.data_measurement_org );
  jsFile.printf( JS_FILE_LINE_QUOTES_S, INI_DATA_MEASUREMENT_BUCKET, JS_FILE_VALUE, iniFileStorage.data_measurement_bucket );
  jsFile.printf( JS_FILE_LINE_QUOTES_S, INI_DATA_MEASUREMENT_NAME,   JS_FILE_VALUE, iniFileStorage.data_measurement_name );

  // server config
  jsFile.printf( JS_FILE_LINE_QUOTES_S, INI_SERVER_ADDRESS,    JS_FILE_VALUE, iniFileStorage.server_address );
  jsFile.printf( JS_FILE_LINE_QUOTES_D, INI_SERVER_PORT,       JS_FILE_VALUE, iniFileStorage.server_port );
  jsFile.printf( JS_FILE_LINE_QUOTES_S, INI_SERVER_AUTH_TOKEN, JS_FILE_VALUE, iniFileStorage.server_auth_token );


  // [display]
  jsFile.printf( JS_FILE_LINE_QUOTES_D, INI_DISP_CONTRAST,    JS_FILE_VALUE, iniFileStorage.display_contrast );
  jsFile.printf( JS_FILE_LINE, INI_DISP_ROTATION, JS_FILE_CHECKED, (true == iniFileStorage.display_rotation ? "true" : "false" ) );
  
  // [sensor]
  jsFile.printf( JS_FILE_LINE_QUOTES_F1_1, INI_SENSOR_TEMP_CORRECTION, JS_FILE_VALUE, iniFileStorage.sensor_temp_correction );

  // [battery]
  jsFile.printf( JS_FILE_LINE_QUOTES_D, INI_BATTERY_MIN_LEVEL, JS_FILE_VALUE, iniFileStorage.batteryMinLevel );
  jsFile.printf( JS_FILE_LINE_QUOTES_D, INI_BATTERY_MAX_LEVEL, JS_FILE_VALUE, iniFileStorage.batteryMaxLevel );


  jsFile.println("}");

  jsFile.close();

  // Remove the back-up file
  if ( false == LittleFS.remove(JS_FILENAME_BACKUP) )
  {
    SERIAL_PLN("Error removing js back-up file.");
  }

  return true;
}


//-- processSubmit ---------------------------------------------------------------------------------
bool WebConfigManagement::processSubmit(ESP8266WebServer& server, String &error, SensorIniFileStorage &iniFileStorage)
{
  bool result = true;

  error = "";
  bool value = false;

  // [network]
  parseSubmit(server, error, INI_NET_WIFI_ENABLED, value );  iniFileStorage.wifi_enabled = value; 
  parseSubmit(server, error, INI_NET_WIFI_AP_SSID, iniFileStorage.wifi_ap_ssid, MAX_LEN_SSID );
  parseSubmit(server, error, INI_NET_WIFI_AP_PWD,  iniFileStorage.wifi_ap_pwd, MAX_LEN_PWD );
    memset(iniFileStorage.wifi_ap_bssid, 0, sizeof(uint8_t) * 6 ); // clean out BSSID

  parseSubmit(server, error, INI_NET_WIFI_CON_DELAY,        iniFileStorage.wifi_con_delay );
  parseSubmit(server, error, INI_NET_WIFI_MAX_CON_ATTEMPTS, iniFileStorage.wifi_max_con_attempts );

  // [data upload]
  parseSubmit(server, error, INI_DATA_FREQ,               iniFileStorage.upload_freq );
  parseSubmit(server, error, INI_DATA_UPLOAD_TIMEOUT,     iniFileStorage.upload_timeout );
  parseSubmit(server, error, INI_DATA_DEVICE_ID,          iniFileStorage.device_id, MAX_LEN_DEVICE_ID );
  parseSubmit(server, error, INI_DATA_LOCATION,           iniFileStorage.location, MAX_LEN_LOCATION );
  parseSubmit(server, error, INI_DATA_MEASUREMENT_ORG,    iniFileStorage.data_measurement_org, MAX_LEN_DATA_MEASUREMENT_ORG );
  parseSubmit(server, error, INI_DATA_MEASUREMENT_BUCKET, iniFileStorage.data_measurement_bucket, MAX_LEN_DATA_MEASUREMENT_BUCKET );
  parseSubmit(server, error, INI_DATA_MEASUREMENT_NAME,   iniFileStorage.data_measurement_name, MAX_LEN_DATA_MEASUREMENT_NAME );


  // [server confing]
  parseSubmit(server, error, INI_SERVER_ADDRESS,     iniFileStorage.server_address, MAX_LEN_SERVER_ADDRESS );
  parseSubmit(server, error, INI_SERVER_PORT,        iniFileStorage.server_port );
  parseSubmit(server, error, INI_SERVER_AUTH_TOKEN,  iniFileStorage.server_auth_token, MAX_LEN_SERVER_AUTH_TOKEN );


  // [display]
  parseSubmit(server, error, INI_DISP_CONTRAST,           iniFileStorage.display_contrast );
  parseSubmit(server, error, INI_DISP_ROTATION, value );  iniFileStorage.display_rotation = value; 

  // [sensor]
  parseSubmit(server, error, INI_SENSOR_TEMP_CORRECTION, iniFileStorage.sensor_temp_correction); 

  // [battery]
  parseSubmit(server, error, INI_BATTERY_MIN_LEVEL, iniFileStorage.batteryMinLevel); 
  parseSubmit(server, error, INI_BATTERY_MAX_LEVEL, iniFileStorage.batteryMaxLevel); 


  if ( 0 < error.length() ) { SERIAL_PLN(error); result = false; }

  result = generateJsFile(iniFileStorage); // New config, new js file
  result |= sensor::SensorConfigFile::writeIniFile( iniFileStorage ); // New config, new ini file

  // u8g2.setContrast(iniFileStorage.display_contrast); // 155 - Home; 127 - Office
  // drawScreen();

  return result;
}


//-- parseSubmit char* -----------------------------------------------------------------------------
bool WebConfigManagement::parseSubmit(ESP8266WebServer& server, String &error, const char *INI_ITEM, 
                                      char *storage, const uint8_t MAX_LEN)
{
  if ( true == server.hasArg( INI_ITEM ) ) 
  {
    SERIAL_PF("%s = %s\n", INI_ITEM, server.arg( INI_ITEM ).c_str() );
    strcpy( storage, server.arg(INI_ITEM).substring(0, MAX_LEN).c_str() );
    SERIAL_PF("\tStorage: \"%s\"\n", storage );
    return true;
  } else { error += INI_ITEM; error += NOT_FOUND; return false; }
}

//-- parseSubmit uint8_t ---------------------------------------------------------------------------
bool WebConfigManagement::parseSubmit(ESP8266WebServer& server, String &error, const char *INI_ITEM, 
                                      uint8_t &storage)
{
  if ( true == server.hasArg( INI_ITEM ) ) 
  {
    SERIAL_PF("%s = %s\n", INI_ITEM, server.arg( INI_ITEM ).c_str() );
    storage = static_cast<uint8_t>( atoi( server.arg(INI_ITEM).c_str() ) );
    return true;
  } else { error += INI_ITEM; error += NOT_FOUND; return false;}
}

//-- parseSubmit uint16_t --------------------------------------------------------------------------
bool WebConfigManagement::parseSubmit(ESP8266WebServer& server, String &error, const char *INI_ITEM,
                                      uint16_t &storage)
{
  if ( true == server.hasArg( INI_ITEM ) ) 
  {
    SERIAL_PF("%s = %s\n", INI_ITEM, server.arg( INI_ITEM ).c_str() );
    storage = static_cast<uint16_t>( atoi( server.arg(INI_ITEM).c_str() ) );
    return true;
  } else { error += INI_ITEM; error += NOT_FOUND; return false;}
}

//-- parseSubmit float -----------------------------------------------------------------------------
bool WebConfigManagement::parseSubmit(ESP8266WebServer& server, String &error, const char *INI_ITEM,
                                      float &storage)
{
  if ( true == server.hasArg( INI_ITEM ) ) 
  {
    SERIAL_PF("%s = %s\n", INI_ITEM, server.arg( INI_ITEM ).c_str() );
    storage = atof( server.arg(INI_ITEM).c_str() );
    return true;
  } else { error += INI_ITEM; error += NOT_FOUND; return false;}
}

//-- parseSubmit bool ------------------------------------------------------------------------------
bool WebConfigManagement::parseSubmit(ESP8266WebServer& server, String &error, const char *INI_ITEM,
                                      bool &storage)
{
  if ( true == server.hasArg( INI_ITEM ) ) 
  {
    SERIAL_PF("%s = %s\n", INI_ITEM, server.arg( INI_ITEM ).c_str() );
    storage = true;
    return true;
  } 
  else 
  { 
    SERIAL_PF("%s = %s (Checkbox not present)\n", INI_ITEM, "false" );
    storage = false;
    return false;
  }
}