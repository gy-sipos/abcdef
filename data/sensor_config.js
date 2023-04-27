function setValues()
{
  document.getElementById("wifi_enabled").checked = true;
  document.getElementById("wifi_enabled").value = true;
  document.getElementById("wifi_ap_ssid").value = "*****";
  document.getElementById("wifi_ap_pwd").value = "*****";
  document.getElementById("wifi_con_delay").value = "150";
  document.getElementById("wifi_max_con_attempts").value = "240";
  document.getElementById("upload_freq").value = "180";
  document.getElementById("upload_timeout").value = "20";
  document.getElementById("device_id").value = "TSH05";
  document.getElementById("location").value = "usHallway";
  document.getElementById("data_measurement_org").value = "mine";
  document.getElementById("data_measurement_bucket").value = "ts_bucket";
  document.getElementById("data_measurement_name").value = "devThermoSensor";
  document.getElementById("server_address").value = "eu-central-1-1.aws.cloud2.influxdata.com";
  document.getElementById("server_port").value = "443";
  document.getElementById("server_auth_token").value = "**";
  document.getElementById("display_contrast").value = "137";
  document.getElementById("display_rotation").checked = true;
  document.getElementById("sensor_temp_correction").value = "0.0";
  document.getElementById("battery_min_level").value = "527";
  document.getElementById("battery_max_level").value = "856";
}