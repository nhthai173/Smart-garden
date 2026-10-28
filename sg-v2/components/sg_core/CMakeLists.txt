# Lõi tưới: cấu hình, logic tưới, an toàn. KHÔNG được biết gì về mạng -
# không REQUIRES nht_net / nht_webkit / nht_fbrtdb / sg_net. Quy tắc này do
# tools/check_layers.py ép lúc build.
idf_component_register(
    SRCS "app_cfg.c" "app_water.c" "app_boot.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "../../include"      # secret.h (mặc định lúc phát triển)
    REQUIRES nht_store nht_actuator nht_valve nht_sched nht_dinput nht_sensor
             nvs_flash esp_timer esp_driver_gpio esp_system
    PRIV_REQUIRES json
)
