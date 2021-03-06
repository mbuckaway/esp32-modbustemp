#pragma once

#include <stdint.h>

// Enumeration of all supported CIDs for device (used in parameter definition table)
enum {
    CID_INP_DATA_TEMPERATURE = 0,
    CID_INP_DATA_HUMIDITY,
    CID_COUNT,
};

#pragma pack(push, 1)
typedef struct
{
    float inputs[CID_COUNT];
} input_reg_params_t;
#pragma pack(pop)

/**
 * @brief Returns temperature value
 */
float get_temperature(void);

/**
 * @brief Returns temperature value
 */
float get_humidity(void);

/**
 * @brief Clears the input structure to reset the data to all zero
 */
void clearmodbus(void);

/**
 * @brief Reads the data from the modbus for the solar controller.
 * The minimum amount of time between calls to this function is 500ms. Errors will occur if it is called
 * too quickly.
 * @returns input_reg_params_t struct with the data
 */
void read_modbus(void);

/**
 * @brief Modbus master initialization routine sets up the GPIO for UART communications
 * and starts up the modbus master library. This routine must be called before any communications
 * with modbus slaves.
 * @returns esp_err_t code with any errors
 */
esp_err_t modbus_init(void);

void modbus_shutdown(void);
void modbus_start(void);


#ifdef CONFIG_MB_TEST_MODE
/**
 * @brief Starts a thread that hammers the modbus. Used only for testing. The WIFI and MQTT functionality
 * is disabled.
 */
void modbus_test_start(void);
#endif
