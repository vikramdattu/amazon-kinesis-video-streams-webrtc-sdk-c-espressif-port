set(usrsctp_root_dir ${CMAKE_CURRENT_SOURCE_DIR}/usrsctp)
set(usrsctp_lib_dir ${usrsctp_root_dir}/usrsctplib)

# Register our component with just the ESP-specific file
idf_component_register(
    SRCS
        esp_usrsctp.c # empty file to keep the build sys happy
    INCLUDE_DIRS
        ${usrsctp_lib_dir}
        ${usrsctp_lib_dir}/netinet
        ${usrsctp_lib_dir}/netinet6
    REQUIRES
        lwip
        mbedtls
        esp_common
        pthread
        freertos
)

# Set up environment for usrsctp. Use CACHE FORCE because the inner
# usrsctp/CMakeLists.txt declares these as option() — without CACHE
# our local set() gets overridden by option()'s default at
# add_subdirectory time, and SCTP_USE_LWIP ends up defined on Linux
# (which then drags in lwip-specific headers usrsctp's userspace path
# can't satisfy on a host build).
if(IDF_TARGET STREQUAL "linux")
    set(sctp_use_lwip OFF CACHE BOOL "build with lwip" FORCE)
    set(sctp_use_rtos OFF CACHE BOOL "build with rtos" FORCE)
else()
    set(sctp_use_lwip ON CACHE BOOL "build with lwip" FORCE)
    set(sctp_use_rtos ON CACHE BOOL "build with rtos" FORCE)
endif()

# Add definitions needed by usrsctp
add_definitions(-D__Userspace__)
add_definitions(-DSCTP_PROCESS_LEVEL_LOCKS)
add_definitions(-DSCTP_SIMPLE_ALLOCATOR)
add_definitions(-DHAVE_SYS_QUEUE_H)
add_definitions(-DHAVE_STDATOMIC_H)
add_definitions(-DSCTP_DEBUG)
add_definitions(-DESP_PLATFORM)

if(sctp_use_lwip)
    add_definitions(-DSCTP_USE_LWIP)
endif()

if(sctp_use_rtos)
    add_definitions(-DSCTP_USE_RTOS)
endif()

if(NOT CONFIG_IDF_TARGET STREQUAL "linux")
    add_definitions(-DHAVE_SCONN_LEN)
endif()

# Set up options for usrsctp
set(sctp_build_programs OFF CACHE BOOL "Build usrsctp example programs" FORCE)
set(sctp_build_shared_lib OFF CACHE BOOL "Build usrsctp as shared library" FORCE)
set(sctp_invariants OFF CACHE BOOL "Add runtime checks" FORCE)
set(sctp_inet ON CACHE BOOL "Support IPv4" FORCE)
set(sctp_inet6 OFF CACHE BOOL "Support IPv6" FORCE)
set(SCTP_USE_MBEDTLS_SHA1 ON CACHE BOOL "Build with mbedtls sha1 support" FORCE)
set(sctp_werror OFF CACHE BOOL "Treat warning as error" FORCE)

# Create a custom FindMbedTLS.cmake in the CMAKE_MODULE_PATH
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/FindMbedTLS.cmake" "
# Custom FindMbedTLS.cmake for ESP-IDF
set(MBEDTLS_FOUND TRUE)
set(MBEDTLS_INCLUDE_DIRS \"${IDF_PATH}/components/mbedtls/mbedtls/include\" \"${IDF_PATH}/components/mbedtls/port/include\")
set(MBEDTLS_LIBRARIES mbedtls mbedcrypto mbedx509)
")

# Add the directory with our custom FindMbedTLS.cmake to CMAKE_MODULE_PATH
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_BINARY_DIR}")

# Add compiler flags to suppress warnings. Apple Clang doesn't know about
# `maybe-uninitialized` (only GCC has that name; LLVM uses plain
# `uninitialized`), and treats unknown warning options as errors under
# -Werror. Probe the flag and only add it when supported.
include(CheckCCompilerFlag)
check_c_compiler_flag("-Wno-error=maybe-uninitialized" HAS_NO_ERROR_MAYBE_UNINIT)

add_compile_options(
    -Wno-address-of-packed-member
    -Wno-unused-but-set-variable
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-pointer-sign
    -Wno-implicit-function-declaration
    -Wno-incompatible-pointer-types
    -Wno-format
    -Wno-pedantic
    -Wno-error=pedantic
)
if(HAS_NO_ERROR_MAYBE_UNINIT)
    add_compile_options(-Wno-error=maybe-uninitialized)
endif()

# Add usrsctp as a subdirectory
add_subdirectory(${usrsctp_root_dir} ${CMAKE_CURRENT_BINARY_DIR}/usrsctp)

# so that we can use pthread and heap functions in usrsctp
target_link_libraries(usrsctp PRIVATE idf::pthread idf::heap)

# Link against usrsctp library
target_link_libraries(${COMPONENT_LIB} PUBLIC usrsctp)
