// Code based on Espressif Modbus Master Example
//
// Copyright 2016-2019 Espressif Systems (Shanghai) PTE LTD
// Modifications Copyright (C) 2020 Mark Buckaway
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "string.h"
#include "esp_log.h"
#include "mbcontroller.h"
#include "modbus.h"
#include "sdkconfig.h"
#include "threads.h"
#include "homekit.h"

#define MB_PORT_NUM     (CONFIG_MB_UART_PORT_NUM)   // Number of UART port used for Modbus connection
#define MB_DEV_SPEED    (CONFIG_MB_UART_BAUD_RATE)  // The communication speed of the UART
#define MB_MAX_RETRY    10
// Note: Some pins on target chip cannot be assigned for UART communication.
// See UART documentation for selected board and target to configure pins using Kconfig.

// The number of parameters that intended to be used in the particular control process
#define MASTER_MAX_CIDS num_device_parameters

// Number of reading of parameters from slave
#define MASTER_MAX_RETRY 2

// Timeout between polls
#define POLL_TIMEOUT_MS                 (50)
#define POLL_TIMEOUT_TICS               (POLL_TIMEOUT_MS / portTICK_RATE_MS)

#define MODBUS_TAG "MODBUS"

#define MASTER_CHECK(a, ret_val, str, ...) \
    if (!(a)) { \
        ESP_LOGE(MODBUS_TAG, "%s(%u): " str, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
        return (ret_val); \
    }


#define STR(fieldname) ((const char*)( fieldname ))
// Options can be used as bit masks or parameter limits
#define OPTS(min_val, max_val, step_val) { .opt1 = min_val, .opt2 = max_val, .opt3 = step_val }
#define NO_OPTS() { .opt1 = 0, .opt2 = 0, .opt3 = 0 }
#define BINARY_OPTS(bit) { .opt1 = bit, .opt2 = 255, .opt3 = 255 }

static input_reg_params_t input_reg_params = { 0 };

// EPSolar Data (Object) Dictionary. We only use grab some of the live data. Stats
// are not useful to use as they can be processed by the cloud services.
//
// The CID field in the table must be unique.
// Modbus Slave Addr field defines slave address of the device with correspond parameter.
// Modbus Reg Type - Type of Modbus register area (Holding register, Input Register and such).
// Reg Start field defines the start Modbus register number and Reg Size defines the number of registers for the characteristic accordingly.
// The Instance Offset defines offset in the appropriate parameter structure that will be used as instance to save parameter value.
// Data Type, Data Size specify type of the characteristic and its data size.
// Parameter Options field specifies the options that can be used to process parameter value (limits or masks).
// Access Mode - can be used to implement custom options for processing of characteristic (Read/Write restrictions, factory mode values and etc).
static const mb_parameter_descriptor_t device_parameters[] = {
    // { CID, Param Name, Units, Modbus Slave Addr, Modbus Reg Type, Reg Start, Reg Size, Instance Offset, Data Type, Data Size, Parameter Options, Access Mode}
    { CID_INP_DATA_TEMPERATURE, STR("Temperature"), STR("C"), CONFIG_MB_DEVICE_ADDR, MB_PARAM_INPUT, 0x0001, 1,
         CID_INP_DATA_TEMPERATURE, PARAM_TYPE_FLOAT, PARAM_SIZE_U16, NO_OPTS(), PAR_PERMS_READ },
    { CID_INP_DATA_HUMIDITY, STR("Humidity"), STR("%"), CONFIG_MB_DEVICE_ADDR, MB_PARAM_INPUT, 0x0002, 1,
         CID_INP_DATA_HUMIDITY, PARAM_TYPE_FLOAT, PARAM_SIZE_U16, NO_OPTS(), PAR_PERMS_READ },
};

// Calculate number of parameters in the table
static const uint16_t num_device_parameters = (sizeof(device_parameters)/sizeof(device_parameters[0]));

/**
 * @brief Sensor data items are stored as int16 items multiplied by 10. This function converts
 * the item to a float.
 * @param i - integer value
 * @returns float of the value multiplied by 10
 */
float get_value(uint16_t cid)
{
    float result = 0.0;
    if (cid<CID_COUNT)
    {
        result = input_reg_params.inputs[cid];
    }
    else
    {
        ESP_LOGE(MODBUS_TAG, "Invalid CID %d in get_value", cid);
    }
    return result;
}

float get_temperature(void)
{
    return get_value(CID_INP_DATA_TEMPERATURE);
}

float get_humidity(void)
{
    return get_value(CID_INP_DATA_HUMIDITY);
}

void clearmodbus(void)
{
    memset(&input_reg_params, 0, sizeof(input_reg_params_t));
}

/**
 * @brief Reads the data from the modbus for the solar controller.
 * The minimum amount of time between calls to this function is 500ms. Errors will occur if it is called
 * too quickly. Data is retrieve with the "getter" functions.
  */
void read_modbus(void)
{
    esp_err_t err = ESP_OK;
    const mb_parameter_descriptor_t* param_descriptor = NULL;
    
    ESP_LOGI(MODBUS_TAG, "Reading modbus data...");
    
    for (uint16_t cid = 0; (err != ESP_ERR_NOT_FOUND) && cid < MASTER_MAX_CIDS; cid++) 
    {
        // Get data from parameters description table
        // and use this information to fill the characteristics description table
        // and having all required fields in just one table
        err = mbc_master_get_cid_info(cid, &param_descriptor);
        if ((err != ESP_ERR_NOT_FOUND) && (param_descriptor != NULL))
        {
            int32_t value = 0;
            uint8_t type = 0;
            err = ESP_ERR_TIMEOUT;
            for (int retry = 0; (retry < MB_MAX_RETRY) && (err != ESP_OK); retry++) {
                err = mbc_master_get_parameter(cid, (char*)param_descriptor->param_key, 
                                                                (uint8_t*)&value, &type);
                if (err != ESP_OK)
                {
                    ESP_LOGE(MODBUS_TAG, "Characteristic #%d (%s) read fail, err = 0x%x (%s). Retrying %d of %d ...",
                                    param_descriptor->cid,
                                    (char*)param_descriptor->param_key,
                                    (int)err,
                                    (char*)esp_err_to_name(err),
                                    retry,
                                    MB_MAX_RETRY);
                    vTaskDelay(POLL_TIMEOUT_TICS); // timeout between polls
                }
            }
            // If we get here on failure, we just move on and hope it works
            if (err != ESP_OK) continue;

            if (param_descriptor->mb_param_type == MB_PARAM_INPUT)
            {
                // All the EPever input params are 16 bit ints, but they represent a float with two decimal places. So, we divide by 100 and store them
                // for all values that the table calls a float. Everything is converted without the divide by 100
                float value_f = 0.0;
                if (param_descriptor->param_offset>CID_COUNT)
                {
                    ESP_LOGE(MODBUS_TAG, "Offset of CID %d (%s) exceeds limit of %d - ignored", cid, (char*)param_descriptor->param_key, CID_COUNT);
                }
                else
                {
                    if (param_descriptor->param_type == PARAM_TYPE_FLOAT)
                    {
                        if (value>0)
                        {
                            value_f = (float)(value)/10.0;
                        }
                        input_reg_params.inputs[param_descriptor->param_offset] = value_f;
                    } else if ((param_descriptor->param_type == PARAM_TYPE_U16) || (param_descriptor->param_type == PARAM_TYPE_U8))
                    {
                        if (value>0)
                        {
                            value_f = (float)(value);
                        }
                        input_reg_params.inputs[param_descriptor->param_offset] = value_f;
                    }
                    else
                    {
                        ESP_LOGE(MODBUS_TAG, "Unknown type for CID %d: %s ", cid, (char*)param_descriptor->param_key);
                    }
                    
                    ESP_LOGI(MODBUS_TAG, "Characteristic #%d %s (%s) value = %0.02f (0x%x) read successful.",
                                    param_descriptor->cid,
                                    (char*)param_descriptor->param_key,
                                    (char*)param_descriptor->param_units,
                                    value_f,
                                    value
                                    );
                }
            }
            else
            {
                ESP_LOGE(MODBUS_TAG, "Holding Regs not currently supported");
            }
            vTaskDelay(POLL_TIMEOUT_TICS); // timeout between polls
        }
    }
    temperature_update(get_temperature());
    humidity_update(get_humidity());
}

/**
 * @brief Modbus master initialization routine sets up the GPIO for UART communications
 * and starts up the modbus master library. This routine must be called before any communications
 * with modbus slaves.
 * @returns esp_err_t code with any errors
 */
esp_err_t modbus_init(void)
{
    ESP_LOGI(MODBUS_TAG, "Setting up Modbus master stack...");
    // Initialize and start Modbus controller
    mb_communication_info_t comm = {
            .port = MB_PORT_NUM,
#if CONFIG_MB_COMM_MODE_ASCII
            .mode = MB_MODE_ASCII,
#elif CONFIG_MB_COMM_MODE_RTU
            .mode = MB_MODE_RTU,
#endif
            .baudrate = MB_DEV_SPEED,
            .parity = MB_PARITY_NONE
    };
    void* master_handler = NULL;

    esp_err_t err = mbc_master_init(MB_PORT_SERIAL_MASTER, &master_handler);
    MASTER_CHECK((master_handler != NULL), ESP_ERR_INVALID_STATE,
                                "mb controller initialization fail.");
    MASTER_CHECK((err == ESP_OK), ESP_ERR_INVALID_STATE,
                            "mb controller initialization fail, returns(0x%x).",
                            (uint32_t)err);
    err = mbc_master_setup((void*)&comm);
    MASTER_CHECK((err == ESP_OK), ESP_ERR_INVALID_STATE,
                            "mb controller setup fail, returns(0x%x).",
                            (uint32_t)err);

    // Set UART pin numbers
    err = uart_set_pin(MB_PORT_NUM, CONFIG_MB_UART_TXD, CONFIG_MB_UART_RXD,
                              CONFIG_MB_UART_RTS, UART_PIN_NO_CHANGE);

    err = mbc_master_start();
    MASTER_CHECK((err == ESP_OK), ESP_ERR_INVALID_STATE,
                            "mb controller start fail, returns(0x%x).",
                            (uint32_t)err);

    MASTER_CHECK((err == ESP_OK), ESP_ERR_INVALID_STATE,
            "mb serial set pin failure, uart_set_pin() returned (0x%x).", (uint32_t)err);
    // Set driver mode to Half Duplex
    err = uart_set_mode(MB_PORT_NUM, UART_MODE_RS485_HALF_DUPLEX);
    MASTER_CHECK((err == ESP_OK), ESP_ERR_INVALID_STATE,
            "mb serial set mode failure, uart_set_mode() returned (0x%x).", (uint32_t)err);

    vTaskDelay(5);
    err = mbc_master_set_descriptor(&device_parameters[0], num_device_parameters);
    MASTER_CHECK((err == ESP_OK), ESP_ERR_INVALID_STATE,
                                "mb controller set descriptor fail, returns(0x%x).",
                                (uint32_t)err);
    ESP_LOGI(MODBUS_TAG, "Modbus master stack initialized...");
    return err;
}

void modbus_shutdown(void)
{
    ESP_LOGW(MODBUS_TAG, "Modbus shutdown called");
    ESP_ERROR_CHECK(mbc_master_destroy());
}

// If THinkspeak is disabled we need a loop to read the modbus device

#ifndef CONFIG_THINKSPEAK_ENABLE

static void modbus_reader(void *pvParameter)
{
    // Read the modbus on a loop, because Homekit doesn't like to wait
    while (1)
    {
        read_modbus();
        vTaskDelay(CONFIG_MB_THREAD_TIMEOUT * 1000 / portTICK_PERIOD_MS);
    }
}

void modbus_start(void)
{
    ESP_LOGI(MODBUS_TAG, "MODBUS Main Loop Start");
    xTaskCreate(modbus_reader, THREAD_MODBUS_NAME, THREAD_MODBUS_STACKSIZE, NULL, THREAD_MODBUS_PRIORITY, NULL);
}

#endif