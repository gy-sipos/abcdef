#ifndef __WEB_CONFIG_MANAGEMENT_H__
#define __WEB_CONFIG_MANAGEMENT_H__

#include <Arduino.h>
#include <ESP8266WebServer.h>

// class ESP8266WebServer;

namespace sensor
{

struct SensorIniFileStorage;

class WebConfigManagement
{

public:

  //-- handleFileRead ------------------------------------------------------------------------------
  // send the right file to the client (if it exists)
  bool handleFileRead(String path, ESP8266WebServer& server, SensorIniFileStorage &iniFileStorage);


private:
  //-- getContentType ------------------------------------------------------------------------------
  // convert the file extension to the MIME type
  String getContentType(String filename);

  //-- generateJsFile ------------------------------------------------------------------------------
  bool generateJsFile(const SensorIniFileStorage &iniFileStorage);

  //-- processSubmit -------------------------------------------------------------------------------
  bool processSubmit(ESP8266WebServer& server, String &error, SensorIniFileStorage &iniFileStorage);

  //-- parseSubmit ---------------------------------------------------------------------------------
  bool parseSubmit(ESP8266WebServer& server, String &error, const char *INI_ITEM, char *storage, 
                  const uint8_t MAX_LEN);
  
  //-- parseSubmit uint8_t -------------------------------------------------------------------------
  bool parseSubmit(ESP8266WebServer& server, String &error, const char *INI_ITEM, uint8_t &storage);

  //-- parseSubmit uint16_t ------------------------------------------------------------------------
  bool parseSubmit(ESP8266WebServer& server, String &error, const char *INI_ITEM, uint16_t &storage);

  //-- parseSubmit float ---------------------------------------------------------------------------
  bool parseSubmit(ESP8266WebServer& server, String &error, const char *INI_ITEM, float &storage);

  //-- parseSubmit bool ----------------------------------------------------------------------------
  bool parseSubmit(ESP8266WebServer& server, String &error, const char *INI_ITEM, bool &storage);

};

}; // namespace

#endif // __WEB_AND_CONFIG_MANAGEMENT_H__
