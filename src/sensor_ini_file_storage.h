#ifndef __SENSOR_INI_FILE_STORAGE_H__
#define __SENSOR_INI_FILE_STORAGE_H__

#include <Arduino.h>

namespace sensor
{

struct SensorIniFileStorage
{
  // network section
  bool wifi_enabled = true;
  char wifi_ap_ssid[33]    = { 0 }; // 32 + 1 (0)
  char wifi_ap_pwd[65]     = { 0 }; // 64 + 1 (0)
  uint8_t wifi_ap_bssid[6] = { 0xFF };
  int32_t wifi_ap_channel  = 0;
  uint16_t wifi_con_delay  = 0; // how much miliseconds to wait between each attempt - 150 ms the default
  uint16_t wifi_max_con_attempts = 0; // how many times to try connecting to the AP - 60 the default

  // data upload section
  uint16_t upload_freq   = 0;
  uint8_t upload_timeout = 0;
  char device_id[16] = { 0 }; // 15 + 1 (0)
  char location[16]  = { 0 };  // 15 + 1 (0)
  char data_measurement_org[32]    = { 0 };
  char data_measurement_bucket[32] = { 0 };
  char data_measurement_name[32]   = { 0 };

  // server config section
  char server_address[256]    = { 0 }; // 255 + 1 (0)
  uint16_t server_port        = 0;
  char server_auth_token[256] = { 0 }; // 255 + 1 (0)

  // display section
  uint8_t display_contrast = 0; // 0 - 255
  bool display_rotation    = false; // false => no rotation, true => up-side-down

  // sensor temp correction
  float sensor_temp_correction = 0;

  // battery levels
  uint16_t batteryMinLevel = 0;
  uint16_t batteryMaxLevel = 0;
};

}; // namespace 
#endif // __SENSOR_INI_FILE_STORAGE_H__