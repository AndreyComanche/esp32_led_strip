idf_component_register(SRCS "midi_led_main.c" "class_driver.c" "led_strip_encoder.c"
                       PRIV_REQUIRES esp_driver_rmt usb esp_driver_gpio
                       INCLUDE_DIRS ".")
