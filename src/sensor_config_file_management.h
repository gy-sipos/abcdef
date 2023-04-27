#ifndef __SENSOR_CONFIG_FILE_MANAGEMENT_H__
#define __SENSOR_CONFIG_FILE_MANAGEMENT_H__

#include <Arduino.h>

class SPIFFSIniFile;


namespace sensor
{

//-- INI FILE SETTINGS AND CONSTANTS ---------------------------------------------------------------
//-----------
const char INI_NET_SECTION[] = "network";
const char INI_NET_WIFI_ENABLED[]    = "wifi_enabled";
const char INI_NET_WIFI_AP_SSID[]    = "wifi_ap_ssid";  // max length is 32 characters
const char INI_NET_WIFI_AP_PWD[]     = "wifi_ap_pwd";   // the maximum password length for WPA2-PSK is 64 characters
const char INI_NET_WIFI_AP_BSSID[]   = "wifi_ap_bssid";
const char INI_NET_WIFI_AP_CHANNEL[] = "wifi_ap_channel";
const char INI_NET_WIFI_CON_DELAY[]   = "wifi_con_delay"; // how much miliseconds to wait between each attempt - 150 ms the default
const char INI_NET_WIFI_MAX_CON_ATTEMPTS[] = "wifi_max_con_attempts"; // how many times to try connecting to the AP - 60 the default

const uint8_t MAX_LEN_SSID = 32;  
const uint8_t MAX_LEN_PWD  = 64;

//-----------
const char INI_DATA_SECTION[]            = "data upload";
const char INI_DATA_FREQ[]               = "upload_freq";
const char INI_DATA_UPLOAD_TIMEOUT[]     = "upload_timeout"; // how many seconds to wait for a successful upload
const char INI_DATA_DEVICE_ID[]          = "device_id";   // max length is 15 characters
const char INI_DATA_LOCATION[]           = "location";    // max length is 15 characters
const char INI_DATA_MEASUREMENT_ORG[]    = "data_measurement_org"; // e.g. mine 
const char INI_DATA_MEASUREMENT_BUCKET[] = "data_measurement_bucket"; // e.g. ts_bucket 
const char INI_DATA_MEASUREMENT_NAME[]   = "data_measurement_name"; // e.g. homeThermoSensor, devThermoSensor 

const uint8_t MAX_LEN_DEVICE_ID               =  15;  
const uint8_t MAX_LEN_LOCATION                =  15;  
const uint8_t MAX_LEN_DATA_MEASUREMENT_ORG    =  31;
const uint8_t MAX_LEN_DATA_MEASUREMENT_BUCKET =  31;
const uint8_t MAX_LEN_DATA_MEASUREMENT_NAME   =  31;

//-----------
const char INI_SERVER_SECTION[]    = "server config";
const char INI_SERVER_ADDRESS[]    = "server_address";  // max length is 255 characters, eu-central-1-1.aws.cloud2.influxdata.com
const char INI_SERVER_PORT[]       = "server_port";  // 4443
const char INI_SERVER_AUTH_TOKEN[] = "server_auth_token";  // max length is 255 characters, access token

const uint8_t MAX_LEN_SERVER_ADDRESS    = 255;
const uint8_t MAX_LEN_SERVER_AUTH_TOKEN = 255;

//-----------
const char INI_DISP_SECTION[]   = "display";
const char INI_DISP_CONTRAST[]  = "display_contrast";
const char INI_DISP_ROTATION[]  = "display_rotation";

//-----------
const char INI_SENSOR_SECTION[]         = "sensor";
const char INI_SENSOR_TEMP_CORRECTION[] = "sensor_temp_correction";


const char INI_BATTERY_SECTION[]   = "battery";
const char INI_BATTERY_MIN_LEVEL[] = "battery_min_level";  // The A0 level at 2.75V
const char INI_BATTERY_MAX_LEVEL[] = "battery_max_level";  // The A0 level at 4.20V

// The config file name
const char INI_FILENAME[]        = "/sensor_config.ini";
const char INI_FILENAME_BACKUP[] = "/sensor_config.ini.bu";

struct SensorIniFileStorage;

//--------------------------------------------------------------------------------------------------
class SensorConfigFile
{
public:
  bool readIniFile(SensorIniFileStorage &iniFileStorage);
  static bool writeIniFile(const SensorIniFileStorage &iniFileStorage);

  void printErrorMessage(uint8_t errorCode, bool eol = true);

private:
  //-- parseIniString char* --------------------------------------------------------------------------
  bool parseIniString(const SPIFFSIniFile &ini, const char *INI_SECTION, const char *INI_ITEM, 
                    char *iniBuffer, const size_t &INI_BUFFER_LEN, char *storage, uint16_t maxLen );

  //-- parseIniNumber uint8_t ------------------------------------------------------------------------
  bool parseIniNumber(const SPIFFSIniFile &ini, const char *INI_SECTION, const char *INI_ITEM, 
                      char *iniBuffer, const size_t &INI_BUFFER_LEN, uint8_t &storage );

  //-- parseIniNumber int8_t ------------------------------------------------------------------------
  bool parseIniNumber(const SPIFFSIniFile &ini, const char *INI_SECTION, const char *INI_ITEM, 
                      char *iniBuffer, const size_t &INI_BUFFER_LEN, int8_t &storage );


  //-- parseIniNumber uint16_t -----------------------------------------------------------------------
  bool parseIniNumber(const SPIFFSIniFile &ini, const char *INI_SECTION, const char *INI_ITEM, 
                      char *iniBuffer, const size_t &INI_BUFFER_LEN, uint16_t &storage );

  //-- parseIniNumber int32_t -----------------------------------------------------------------------
  bool parseIniNumber(const SPIFFSIniFile &ini, const char *INI_SECTION, const char *INI_ITEM, 
                      char *iniBuffer, const size_t &INI_BUFFER_LEN, int32_t &storage );

  //-- parseIniNumber float --------------------------------------------------------------------------
  bool parseIniNumber(const SPIFFSIniFile &ini, const char *INI_SECTION, const char *INI_ITEM, 
                      char *iniBuffer, const size_t &INI_BUFFER_LEN, float &storage );

  //-- parseIniNumber bool --------------------------------------------------------------------------
  bool parseIniBool(const SPIFFSIniFile &ini, const char *INI_SECTION, const char *INI_ITEM, 
                    char *iniBuffer, const size_t &INI_BUFFER_LEN, bool &storage );
};

}; // sensor
#endif // __SENSOR_CONFIG_FILE_MANAGEMENT_H__