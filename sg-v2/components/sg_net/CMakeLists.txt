# Lớp adapter mạng: web server + Firebase. Chỉ đứng trên sg_core, không có
# chiều ngược lại. Rút mạng thì cả component này im lặng, tưới vẫn chạy.
idf_component_register(
    SRCS "app_http.c" "app_cloud.c" "app_bench.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "." "../../include"  # web_assets.h (sinh), version.h
    REQUIRES sg_core nht_net nht_webkit nht_fbrtdb
    PRIV_REQUIRES json esp_timer esp_system esp_wifi esp_http_server app_update
)
