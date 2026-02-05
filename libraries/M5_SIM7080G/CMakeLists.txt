idf_component_register(
  SRCS
    "src/M5_SIM7080G.cpp"
    "src/SIM7080G_Network.cpp"
    "src/SIM7080G_GNSS.cpp"
    "src/SIM7080G_MQTT.cpp"
    "src/SIM7080G_HTTP.cpp"
  INCLUDE_DIRS
    "src"
  REQUIRES
    driver
    esp_timer
)

