idf_component_register(SRCS
                        "signaling_only_main.c"
                        "power_save_handler.c"
                        "trigger_offer_command.c"
                        "query_resolution_command.c"
                    INCLUDE_DIRS "."
                    REQUIRES
                        "app_common"
                        "signaling_bridge_adapter"
                        "network_coprocessor")
