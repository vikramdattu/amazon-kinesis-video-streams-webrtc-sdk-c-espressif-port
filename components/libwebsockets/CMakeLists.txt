
set(lws_requires mbedtls freertos esp_timer)
# `driver` doesn't exist on the IDF Linux target; libwebsockets only
# pulls it in via deprecated esp_spi_flash.h.
if(NOT IDF_TARGET STREQUAL "linux")
    list(APPEND lws_requires driver)
endif()

# Register as ESP-IDF component
idf_component_register(
    INCLUDE_DIRS
        "libwebsockets/include"
    REQUIRES "${lws_requires}"
    )

# LWS_WITH_CUSTOM_HEADERS is `option(... ON)` upstream and lws_config.h
# emits its #cmakedefine, so manually pre-defining via CFLAGS is a
# duplicate that produces -Wmacro-redefined under -Werror on the unix
# plat. Keep only LWS_WITH_CUSTOM_HTTP_HEADERS, which has no upstream
# option/cmakedefine and we genuinely want enabled.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DLWS_WITH_CUSTOM_HTTP_HEADERS=1")

# Disable deprecation warnings for esp_spi_flash.h
# Suppress unused variable warnings from upstream libwebsockets code
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-error=cpp -Wno-deprecated-declarations -Wno-unused-variable")

# Disable libwebsockets export targets before adding subdirectory
set(LWS_WITH_EXPORT_LWSTARGETS OFF CACHE BOOL "Disable lws export targets" FORCE)

# Force extensions to be enabled before subdirectory is added
set(LWS_WITHOUT_EXTENSIONS OFF CACHE INTERNAL "Enable extensions" FORCE)
set(LWS_WITH_FILE_OPS OFF CACHE INTERNAL "Disable file operations" FORCE)
set(LWS_WITH_THREADPOOL ON CACHE INTERNAL "Enable threadpool" FORCE)

# libwebsockets's CMakeLists-implied-options.txt force-enables LWS_WITH_ESP32
# (and from it LWS_PLAT_FREERTOS) whenever ESP_PLATFORM is set. ESP_PLATFORM
# is set on every IDF build — including the Linux IDF target — but the IDF
# Linux target is a POSIX environment, not real FreeRTOS. The FreeRTOS plat
# pulls in IDF's portmacro_idf.h / pthread.h with macOS-incompatible includes
# (`__assert_func`, `<sys/features.h>`). Force the ESP32 path off on Linux
# so libwebsockets falls through to its `plat/unix` selection.
if(IDF_TARGET STREQUAL "linux")
    set(LWS_WITH_ESP32 OFF CACHE BOOL "" FORCE)
    set(LWS_PLAT_FREERTOS OFF CACHE BOOL "" FORCE)
endif()

# Add libwebsockets as a subdirectory
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/libwebsockets
                 ${CMAKE_CURRENT_BINARY_DIR}/libwebsockets)

target_link_libraries(${COMPONENT_LIB} INTERFACE websockets)
