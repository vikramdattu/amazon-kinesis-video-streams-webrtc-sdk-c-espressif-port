set(srcs "src/app_storage.c" "src/app_wifi_prov.c")

# esp_eth is always in REQUIRES (not gated on CONFIG_APP_NETWORK_USE_OPENETH)
# because IDF's early component-requirements scan doesn't see the
# Kconfig values yet — conditional REQUIRES gets dropped, the
# OpenCores ETH path then can't find esp_eth.h. The driver code is
# only compiled when CONFIG_ETH_USE_OPENETH=y, so binary size impact
# on real-hardware builds is just the include surface (a few KB of
# unused headers).
set(requires fatfs spiffs esp_wifi esp_netif esp_event nvs_flash esp_eth)

idf_component_register(SRCS "${srcs}"
                       REQUIRES ${requires}
                       INCLUDE_DIRS "include")

# Create a SPIFFS image from the contents of the 'spiffs_image' directory
# that fits the partition named 'storage'. FLASH_IN_PROJECT indicates that
# the generated image should be flashed when the entire project is flashed to
# the target with 'idf.py -p PORT flash'.
if (CONFIG_APP_COMMON_USE_SPIFFS)
    spiffs_create_partition_image(storage spiffs_image FLASH_IN_PROJECT)
endif()
