set(PROJECT_NAME "temp_modbus")

set(CSOURCES
    "modbus.c"
    "mqtt.c"
    "app_main.c"
)

idf_component_register(SRCS ${CSOURCES}
                    INCLUDE_DIRS ".")
