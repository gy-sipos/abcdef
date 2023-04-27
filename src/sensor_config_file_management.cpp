
#include "sensor_ini_file_storage.h"
#include "sensor_config_file_management.h"

#include <SPIFFSIniFile.h>
#include <LittleFS.h>

#include <GSiDebug.h>

using namespace sensor;

bool SensorConfigFile::readIniFile(SensorIniFileStorage &iniFileStorage)
{
  const size_t INI_BUFFER_LEN = 512;
  char iniBuffer[INI_BUFFER_LEN];

  //-- Initialise, load and validate the Ini file --------------------------------------------------
  // Open the Ini file  
  SPIFFSIniFile ini(INI_FILENAME, (char *)"r" );
  if (false == ini.open() ) 
  {
    SERIAL_PF( "Ini file '%s' does not exisit\n", INI_FILENAME);
    return false;
  }
  SERIAL_PLN("Ini file exists");

  // Check the file is valid. This can be used to warn if any lines
  // are longer than the buffer.
  if ( false == ini.validate(iniBuffer, INI_BUFFER_LEN) )
  {
    SERIAL_PF( "ini file '%s' is not valid\n", ini.getFilename() );
    printErrorMessage(ini.getError());
    return false;
  }

  uint8_t res = 0;

  //-- Process the ini file content ----------------------------------------------------------------
    // [network]
      // wifi_enabled=true
      // wifi_ap_ssid=*****
      // wifi_ap_pwd=*****
      // wifi_ap_bssid=00-00-00-00-00-00
      // wifi_ap_channel=*
      // wifi_con_delay=150
      // wifi_max_con_attempts=240
    SERIAL_PF("[%s]\n", INI_NET_SECTION);

    bool value = false;
    res += !parseIniBool(ini, INI_NET_SECTION, INI_NET_WIFI_ENABLED, iniBuffer, INI_BUFFER_LEN, value ); 
      iniFileStorage.wifi_enabled = value; 
    res += !parseIniString(ini, INI_NET_SECTION, INI_NET_WIFI_AP_SSID,           iniBuffer, INI_BUFFER_LEN, iniFileStorage.wifi_ap_ssid, MAX_LEN_SSID ); 
    res += !parseIniString(ini, INI_NET_SECTION, INI_NET_WIFI_AP_PWD,            iniBuffer, INI_BUFFER_LEN, iniFileStorage.wifi_ap_pwd,  MAX_LEN_PWD  ); 
    res += !parseIniNumber(ini, INI_NET_SECTION, INI_NET_WIFI_CON_DELAY,         iniBuffer, INI_BUFFER_LEN, iniFileStorage.wifi_con_delay); 
    res += !parseIniNumber(ini, INI_NET_SECTION, INI_NET_WIFI_MAX_CON_ATTEMPTS,  iniBuffer, INI_BUFFER_LEN, iniFileStorage.wifi_max_con_attempts); 

    //-- BSSID
    if ( false == ini.getMACAddress(INI_NET_SECTION, INI_NET_WIFI_AP_BSSID, iniBuffer, INI_BUFFER_LEN, iniFileStorage.wifi_ap_bssid ) )
    {
      SERIAL_PF("Error parsing: %s / %s => ", INI_NET_SECTION, INI_NET_WIFI_AP_BSSID);
      printErrorMessage(ini.getError());
      return false;
    }
    
    #ifdef GSI_DEBUG
      const uint8_t *mac = iniFileStorage.wifi_ap_bssid;
      SERIAL_PF("wifi_ap_bssid: %x-%x-%x-%x-%x-%x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    #endif
    //-- Channel
    res += !parseIniNumber(ini, INI_NET_SECTION, INI_NET_WIFI_AP_CHANNEL, iniBuffer, INI_BUFFER_LEN, iniFileStorage.wifi_ap_channel); 

  //------------------------------------------------------------------
    // [data upload]
      // upload_freq=180   ; seconds
      // upload_timeout=20 ; seconds
      // device_id=TSH06   ; ThermoSensor Home 06
      // location=usBedR   ; upstairs Bed Room
      // data_measurement_org=mine
      // data_measurement_bucket=ts_bucket
      // data_measurement_name=devThermoSensor
    SERIAL_PF("[%s]\n", INI_DATA_SECTION);
    res += !parseIniNumber(ini, INI_DATA_SECTION, INI_DATA_FREQ,               iniBuffer, INI_BUFFER_LEN, iniFileStorage.upload_freq); 
    res += !parseIniNumber(ini, INI_DATA_SECTION, INI_DATA_UPLOAD_TIMEOUT,     iniBuffer, INI_BUFFER_LEN, iniFileStorage.upload_timeout ); 
    res += !parseIniString(ini, INI_DATA_SECTION, INI_DATA_DEVICE_ID,          iniBuffer, INI_BUFFER_LEN, iniFileStorage.device_id, MAX_LEN_DEVICE_ID ); 
    res += !parseIniString(ini, INI_DATA_SECTION, INI_DATA_LOCATION,           iniBuffer, INI_BUFFER_LEN, iniFileStorage.location, MAX_LEN_LOCATION ); 
    res += !parseIniString(ini, INI_DATA_SECTION, INI_DATA_MEASUREMENT_ORG,    iniBuffer, INI_BUFFER_LEN, iniFileStorage.data_measurement_org, MAX_LEN_DATA_MEASUREMENT_ORG ); 
    res += !parseIniString(ini, INI_DATA_SECTION, INI_DATA_MEASUREMENT_BUCKET, iniBuffer, INI_BUFFER_LEN, iniFileStorage.data_measurement_bucket, MAX_LEN_DATA_MEASUREMENT_BUCKET ); 
    res += !parseIniString(ini, INI_DATA_SECTION, INI_DATA_MEASUREMENT_NAME,   iniBuffer, INI_BUFFER_LEN, iniFileStorage.data_measurement_name, MAX_LEN_DATA_MEASUREMENT_NAME ); 
  

  //------------------------------------------------------------------
    // [server config]
      // server_address=eu-central-1-1.aws.cloud2.influxdata.com
      // server_port=443
      // server_auth_token=**
    SERIAL_PF("[%s]\n", INI_SERVER_SECTION);
    res += !parseIniString(ini, INI_SERVER_SECTION, INI_SERVER_ADDRESS,    iniBuffer, INI_BUFFER_LEN, iniFileStorage.server_address, MAX_LEN_SERVER_ADDRESS ); 
    res += !parseIniNumber(ini, INI_SERVER_SECTION, INI_SERVER_PORT,       iniBuffer, INI_BUFFER_LEN, iniFileStorage.server_port); 
    res += !parseIniString(ini, INI_SERVER_SECTION, INI_SERVER_AUTH_TOKEN, iniBuffer, INI_BUFFER_LEN, iniFileStorage.server_auth_token, MAX_LEN_SERVER_AUTH_TOKEN ); 


  //------------------------------------------------------------------
    // [display]
      // display_contrast=137
      // display_rotation=false
    SERIAL_PF("[%s]\n", INI_DISP_SECTION);
    res += !parseIniNumber(ini, INI_DISP_SECTION, INI_DISP_CONTRAST, iniBuffer, INI_BUFFER_LEN, iniFileStorage.display_contrast); 
    res += !parseIniBool(ini, INI_DISP_SECTION, INI_DISP_ROTATION, iniBuffer, INI_BUFFER_LEN, value ); 
      iniFileStorage.display_rotation = value;
  
  //------------------------------------------------------------------
    // [bme sensor]
      // bme_sensor_temp_correction=0.0
    SERIAL_PF("[%s]\n", INI_SENSOR_SECTION);
    res += !parseIniNumber(ini, INI_SENSOR_SECTION, INI_SENSOR_TEMP_CORRECTION,  iniBuffer, INI_BUFFER_LEN, iniFileStorage.sensor_temp_correction ); 

  //------------------------------------------------------------------
    // [battery]
    SERIAL_PF("[%s]\n", INI_BATTERY_SECTION);
    res += !parseIniNumber(ini, INI_BATTERY_SECTION, INI_BATTERY_MIN_LEVEL, iniBuffer, INI_BUFFER_LEN, iniFileStorage.batteryMinLevel ); 
    res += !parseIniNumber(ini, INI_BATTERY_SECTION, INI_BATTERY_MAX_LEVEL, iniBuffer, INI_BUFFER_LEN, iniFileStorage.batteryMaxLevel ); 

  ini.close();
  return !res;
}
  
//--------------------------------------------------------------------------------------------------
bool SensorConfigFile::writeIniFile(const SensorIniFileStorage &iniFileStorage)
{
  SERIAL_PLN("Writing the new Ini file.");

  // Create a back-up file from the original
  if (false == LittleFS.rename(INI_FILENAME, INI_FILENAME_BACKUP) )
  {
    SERIAL_PLN("Error creating ini file back-up.");
    return false;
  }

  // Create a new ini file
  File iniFile = LittleFS.open(INI_FILENAME, "w");
  if ( false == iniFile )
  {
    SERIAL_PLN("Error creating ini file.");
    return false;
  }

  // [network]
  iniFile.printf("[%s]\n", INI_NET_SECTION);
  iniFile.printf("%s=%s\n", INI_NET_WIFI_ENABLED, (true == iniFileStorage.wifi_enabled ? "true" : "false" ) );
  iniFile.printf("%s=%s\n", INI_NET_WIFI_AP_SSID, iniFileStorage.wifi_ap_ssid );
  iniFile.printf("%s=%s\n", INI_NET_WIFI_AP_PWD, iniFileStorage.wifi_ap_pwd );
  const uint8_t *mac = iniFileStorage.wifi_ap_bssid; 
  iniFile.printf("%s=%x-%x-%x-%x-%x-%x\n", INI_NET_WIFI_AP_BSSID, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5] );
  iniFile.printf("%s=%d\n", INI_NET_WIFI_AP_CHANNEL, iniFileStorage.wifi_ap_channel );
  iniFile.printf("%s=%d\n", INI_NET_WIFI_CON_DELAY, iniFileStorage.wifi_con_delay );
  iniFile.printf("%s=%d\n", INI_NET_WIFI_MAX_CON_ATTEMPTS, iniFileStorage.wifi_max_con_attempts );

  // [data upload]
  iniFile.printf("[%s]\n", INI_DATA_SECTION);
  iniFile.printf("%s=%d\n", INI_DATA_FREQ, iniFileStorage.upload_freq );
  iniFile.printf("%s=%d\n", INI_DATA_UPLOAD_TIMEOUT, iniFileStorage.upload_timeout );
  iniFile.printf("%s=%s\n", INI_DATA_DEVICE_ID, iniFileStorage.device_id );
  iniFile.printf("%s=%s\n", INI_DATA_LOCATION, iniFileStorage.location );
  iniFile.printf("%s=%s\n", INI_DATA_MEASUREMENT_ORG, iniFileStorage.data_measurement_org);
  iniFile.printf("%s=%s\n", INI_DATA_MEASUREMENT_BUCKET, iniFileStorage.data_measurement_bucket);
  iniFile.printf("%s=%s\n", INI_DATA_MEASUREMENT_NAME, iniFileStorage.data_measurement_name);


  // [server config]
  iniFile.printf("[%s]\n", INI_SERVER_SECTION);
  iniFile.printf("%s=%s\n", INI_SERVER_ADDRESS, iniFileStorage.server_address);
  iniFile.printf("%s=%d\n", INI_SERVER_PORT, iniFileStorage.server_port );
  iniFile.printf("%s=%s\n", INI_SERVER_AUTH_TOKEN, iniFileStorage.server_auth_token);


  // [display]
  iniFile.printf("[%s]\n", INI_DISP_SECTION);
  iniFile.printf("%s=%d\n", INI_DISP_CONTRAST, iniFileStorage.display_contrast );
  iniFile.printf("%s=%s\n", INI_DISP_ROTATION, (true == iniFileStorage.display_rotation ? "true" : "false" ) );

  // [bme sensor]
  iniFile.printf("[%s]\n", INI_SENSOR_SECTION);
  iniFile.printf("%s=%1.1f\n", INI_SENSOR_TEMP_CORRECTION, iniFileStorage.sensor_temp_correction );

  // [battery]
  iniFile.printf("[%s]\n", INI_BATTERY_SECTION);
  iniFile.printf("%s=%d\n", INI_BATTERY_MIN_LEVEL, iniFileStorage.batteryMinLevel );
  iniFile.printf("%s=%d\n", INI_BATTERY_MAX_LEVEL, iniFileStorage.batteryMaxLevel );

  iniFile.close();

  SERIAL_PLN("Reading back the Ini file.");
  iniFile = LittleFS.open(INI_FILENAME, "r");
  if ( false == iniFile )
  {
    SERIAL_PLN("Error opening ini file.");
    return false;
  }

  String str;
  do
  {
    str = iniFile.readStringUntil('\n');
    SERIAL_PLN( str.c_str() );
  } 
  while ( str.length() != 0 );

  iniFile.close();

  // Remove the back-up file
  if ( false == LittleFS.remove(INI_FILENAME_BACKUP) )
  {
    SERIAL_PLN("Error removing back-up file.");
  }

  return true;
}


//== FILE MANAGEMENT ===============================================================================
//-- printErrorMessage -----------------------------------------------------------------------------
void SensorConfigFile::printErrorMessage(uint8_t errorCode, bool eol /*true */ )
{
  switch ( errorCode ) {
  case SPIFFSIniFile::errorNoError:
    Serial.print("no error");
    break;
  case SPIFFSIniFile::errorFileNotFound:
    Serial.print("file not found");
    break;
  case SPIFFSIniFile::errorFileNotOpen:
    Serial.print("file not open");
    break;
  case SPIFFSIniFile::errorBufferTooSmall:
    Serial.print("buffer too small");
    break;
  case SPIFFSIniFile::errorSeekError:
    Serial.print("seek error");
    break;
  case SPIFFSIniFile::errorSectionNotFound:
    Serial.print("section not found");
    break;
  case SPIFFSIniFile::errorKeyNotFound:
    Serial.print("key not found");
    break;
  case SPIFFSIniFile::errorEndOfFile:
    Serial.print("end of file");
    break;
  case SPIFFSIniFile::errorUnknownError:
    Serial.print("unknown error");
    break;
  default:
    Serial.print("unknown error value");
    break;
  }
  if (eol)
    Serial.println();
}

//-- parseIniString char* --------------------------------------------------------------------------
bool SensorConfigFile::parseIniString(const SPIFFSIniFile &ini, const char *INI_SECTION, const char *INI_ITEM, 
                    char *iniBuffer, const size_t &INI_BUFFER_LEN, char *storage, uint16_t maxLen )
{
  if ( false == ini.getValue(INI_SECTION, INI_ITEM, iniBuffer, INI_BUFFER_LEN, storage, maxLen ) )
  {
    SERIAL_PF("Error parsing: %s / %s => ", INI_SECTION, INI_ITEM); printErrorMessage(ini.getError());
    return false;
  }
  SERIAL_PF("%s = %s\n", INI_ITEM, storage);
  return true;
}

//-- parseIniNumber uint8_t ------------------------------------------------------------------------
bool SensorConfigFile::parseIniNumber(const SPIFFSIniFile &ini, const char *INI_SECTION, const char *INI_ITEM, 
                    char *iniBuffer, const size_t &INI_BUFFER_LEN, uint8_t &storage )
{
  uint16_t value = 0;
  if ( false == ini.getValue(INI_SECTION, INI_ITEM, iniBuffer, INI_BUFFER_LEN, value ) )
  {
    SERIAL_PF("Error parsing: %s / %s => ", INI_SECTION, INI_ITEM); printErrorMessage(ini.getError());
    return false;
  }
  storage = static_cast<uint8_t>( value );
  SERIAL_PF("%s = %d\n", INI_ITEM, storage);
  return true;
}  

//-- parseIniNumber int8_t ------------------------------------------------------------------------
bool SensorConfigFile::parseIniNumber(const SPIFFSIniFile &ini, const char *INI_SECTION, const char *INI_ITEM, 
                    char *iniBuffer, const size_t &INI_BUFFER_LEN, int8_t &storage )
{
  int value = 0; // int comes from the library
  if ( false == ini.getValue(INI_SECTION, INI_ITEM, iniBuffer, INI_BUFFER_LEN, value ) )
  {
    SERIAL_PF("Error parsing: %s / %s => ", INI_SECTION, INI_ITEM); printErrorMessage(ini.getError());
    return false;
  }
  storage = static_cast<int8_t>( value );
  SERIAL_PF("%s = %d\n", INI_ITEM, storage);
  return true;
}  

//-- parseIniNumber uint16_t -----------------------------------------------------------------------
bool SensorConfigFile::parseIniNumber(const SPIFFSIniFile &ini, const char *INI_SECTION, const char *INI_ITEM, 
                    char *iniBuffer, const size_t &INI_BUFFER_LEN, uint16_t &storage )
{
  if ( false == ini.getValue(INI_SECTION, INI_ITEM, iniBuffer, INI_BUFFER_LEN, storage ) )
  {
    SERIAL_PF("Error parsing: %s / %s => ", INI_SECTION, INI_ITEM); printErrorMessage(ini.getError());
    return false;
  }
  SERIAL_PF("%s = %d\n", INI_ITEM, storage);
  return true;
}  

//-- parseIniNumber int32_t -----------------------------------------------------------------------
bool SensorConfigFile::parseIniNumber(const SPIFFSIniFile &ini, const char *INI_SECTION, const char *INI_ITEM, 
                    char *iniBuffer, const size_t &INI_BUFFER_LEN, int32_t &storage )
{
  if ( false == ini.getValue(INI_SECTION, INI_ITEM, iniBuffer, INI_BUFFER_LEN, storage ) )
  {
    SERIAL_PF("Error parsing: %s / %s => ", INI_SECTION, INI_ITEM); printErrorMessage(ini.getError());
    return false;
  }
  SERIAL_PF("%s = %d\n", INI_ITEM, storage);
  return true;
}  

//-- parseIniNumber float --------------------------------------------------------------------------
bool SensorConfigFile::parseIniNumber(const SPIFFSIniFile &ini, const char *INI_SECTION, const char *INI_ITEM, 
                    char *iniBuffer, const size_t &INI_BUFFER_LEN, float &storage )
{
  if ( false == ini.getValue(INI_SECTION, INI_ITEM, iniBuffer, INI_BUFFER_LEN, storage ) )
  {
    SERIAL_PF("Error parsing: %s / %s => ", INI_SECTION, INI_ITEM); printErrorMessage(ini.getError());
    return false;
  }
  SERIAL_PF("%s = %f\n", INI_ITEM, storage);
  return true;
}  

//-- parseIniNumber bool --------------------------------------------------------------------------
bool SensorConfigFile::parseIniBool(const SPIFFSIniFile &ini, const char *INI_SECTION, const char *INI_ITEM, 
                  char *iniBuffer, const size_t &INI_BUFFER_LEN, bool &storage )
{
  if ( false == ini.getValue(INI_SECTION, INI_ITEM, iniBuffer, INI_BUFFER_LEN, storage ) )
  {
    SERIAL_PF("Error parsing: %s / %s => ", INI_SECTION, INI_ITEM); printErrorMessage(ini.getError());
    return false;
  }
  SERIAL_PF("%s = %s\n", INI_ITEM, ( true == storage ? "true" : "false") );
  return true;
}  
