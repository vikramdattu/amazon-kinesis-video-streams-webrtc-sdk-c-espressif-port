set(srcs
    "test_app_main.c"
    "test_signaling_serializer.c"
    "test_base64.c"
    "test_hex.c"
    "test_crc32.c"
    "test_state_machine.c")

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES "unity"
                       WHOLE_ARCHIVE)
