idf_component_register(SRCS "nht_fbrtdb.c" "nht_sse.c" INCLUDE_DIRS "include"
                       REQUIRES esp_http_client esp-tls json esp_timer mbedtls)
