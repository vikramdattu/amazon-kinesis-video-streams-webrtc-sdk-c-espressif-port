# Source / requires lists. Anything that compiles on every target
# goes in the base lists; target-specific files / deps are appended
# under conditional blocks below.
set(srcs
    "src/message_utils.c"
    "src/webrtc_mem_utils.c"
    "src/esp_work_queue.c")
set(requires esp_common freertos nvs_flash)

if(${IDF_TARGET} STREQUAL "linux")
    # Linux IDF target: libc-backed `flash_wrapper.h` + a host
    # `esp_timer_get_time()` (no esp_timer component on Linux).
    list(APPEND srcs "src/flash_wrapper_linux.c")
else()
    # Real ESP targets: NVS-aware flash_wrapper + ESP CLI / WiFi
    # CLI / SNTP-based time helpers.
    list(APPEND srcs
        "src/esp_cli.c"
        "src/wifi_cli.c"
        "src/esp_webrtc_time.c"
        "src/flash_wrapper.c")
    list(APPEND requires esp_wifi console mqtt esp_timer)
    if(ENABLE_STREAMING_ONLY)
        list(APPEND requires webrtc_bridge)
    endif()
endif()

idf_component_register(SRCS "${srcs}"
                       REQUIRES ${requires}
                       INCLUDE_DIRS "include")
if (ENABLE_SIGNALLING_ONLY)
    add_definitions(-DENABLE_SIGNALLING_ONLY)
elseif (ENABLE_STREAMING_ONLY)
    add_definitions(-DENABLE_STREAMING_ONLY)
endif ()

