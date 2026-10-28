# Điểm ráp: chỉ main.c và (bản mô phỏng) self-test. Không chứa logic.
idf_component_register(
    SRCS "main.c" "app_selftest.c"
    INCLUDE_DIRS "." "../include"
    REQUIRES sg_core sg_net nht_net nht_sensor nht_sched
             nvs_flash esp_driver_gpio esp_system app_update
)
