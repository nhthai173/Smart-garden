# PCNT chỉ có trên một số chip; trên ESP32-C3 thì không. nht_pulse tự chuyển
# sang đếm bằng ngắt GPIO, nhưng vẫn khai báo esp_driver_pcnt để chip nào có
# thì dùng được (component rỗng trên chip không hỗ trợ, vô hại).
idf_component_register(SRCS "nht_adc.c" "nht_pulse.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_adc esp_driver_gpio esp_driver_pcnt esp_timer)
