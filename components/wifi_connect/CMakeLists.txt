idf_component_register(SRCS "wifi_connect.c"
                    INCLUDE_DIRS "."
                    REQUIRES 
                    "esp_netif"
                    "esp_wifi"
                    "log"
                    )
