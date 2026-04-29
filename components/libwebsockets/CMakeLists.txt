
set(lws_requires mbedtls freertos esp_timer)
# On real ESP targets we need lwip's `<netinet/in.h>` shim, and
# `driver` for esp_spi_flash. On the IDF Linux target neither
# applies — `CONFIG_LWIP_ENABLE` is off (host POSIX sockets are
# used instead), so lwip's `port/include/netinet/in.h` would
# shadow the system header without providing the `lwip/inet.h`
# it transitively `#include`s.
if(NOT IDF_TARGET STREQUAL "linux")
    list(APPEND lws_requires driver lwip)
endif()

# Apply local patches under `patches/` to the libwebsockets submodule
# before its CMakeLists is read. Idempotent: `git apply --check`
# succeeds only when the patch isn't already applied, so re-runs
# (e.g. `idf.py reconfigure` after a fresh build dir) skip cleanly.
#
# These are upstream-warmcat candidates; once they merge upstream we
# can drop the local patches and bump the submodule.
set(_lws_dir "${CMAKE_CURRENT_SOURCE_DIR}/libwebsockets")
file(GLOB _lws_patches "${CMAKE_CURRENT_SOURCE_DIR}/patches/*.patch")
list(SORT _lws_patches)
foreach(_p IN LISTS _lws_patches)
    execute_process(
        COMMAND git apply --check "${_p}"
        WORKING_DIRECTORY "${_lws_dir}"
        RESULT_VARIABLE _can_apply
        OUTPUT_QUIET ERROR_QUIET)
    if(_can_apply EQUAL 0)
        execute_process(
            COMMAND git apply "${_p}"
            WORKING_DIRECTORY "${_lws_dir}"
            RESULT_VARIABLE _apply_rc)
        if(NOT _apply_rc EQUAL 0)
            message(FATAL_ERROR "Failed to apply libwebsockets patch: ${_p}")
        endif()
        message(STATUS "Applied libwebsockets patch: ${_p}")
    else()
        message(STATUS "Skipping already-applied libwebsockets patch: ${_p}")
    endif()
endforeach()

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
    # On the unix plat libwebsockets defaults to OpenSSL for crypto.
    # We use mbedtls everywhere else (kvs_signaling, kvs_webrtc, dtls).
    # Force LWS to use mbedtls too so HMAC_CTX etc. resolve to mbedtls
    # types instead of trying to pull in OpenSSL headers.
    set(LWS_WITH_MBEDTLS ON CACHE BOOL "" FORCE)
    # Skip the upstream lws test apps + minimal examples on the Linux
    # IDF target — they fail to link against IDF's static archive set
    # (need lws_open and other libc-platform-only symbols, plus pull
    # in things like JSON policy parser that we don't need).
    set(LWS_WITHOUT_TESTAPPS ON CACHE BOOL "" FORCE)
    set(LWS_WITH_MINIMAL_EXAMPLES OFF CACHE BOOL "" FORCE)
    # On real ESP targets `LWS_WITH_FILE_OPS=OFF` (see below) trims
    # binary size; lejp-conf and secure-streams policy parsing aren't
    # used. On Linux those code paths still get compiled (because
    # they're file-system-relevant on POSIX) and reference `lws_open`,
    # which is itself only compiled under `LWS_WITH_FILE_OPS`. Turn
    # file_ops back on for the Linux IDF target so the symbol resolves.
    set(LWS_WITH_FILE_OPS ON CACHE INTERNAL "" FORCE)
endif()

# Add libwebsockets as a subdirectory
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/libwebsockets
                 ${CMAKE_CURRENT_BINARY_DIR}/libwebsockets)

target_link_libraries(${COMPONENT_LIB} INTERFACE websockets)

# On the IDF Linux target, the IDF lwip component is registered (other
# components transitively REQUIRE it) but `CONFIG_LWIP_ENABLE=n` so it
# provides no headers. The propagated `port/include` directory still
# shadows the host's `<netinet/in.h>` with a wrapper that forwards to
# `lwip/inet.h` — which isn't on the path. Filter the lwip dirs out
# of the `websockets` target's INCLUDE_DIRECTORIES so `<netinet/in.h>`
# resolves to glibc directly.
if(IDF_TARGET STREQUAL "linux")
    get_target_property(_lws_inc websockets INCLUDE_DIRECTORIES)
    if(_lws_inc)
        list(FILTER _lws_inc EXCLUDE REGEX "components/lwip/")
        set_target_properties(websockets PROPERTIES INCLUDE_DIRECTORIES "${_lws_inc}")
    endif()
endif()
