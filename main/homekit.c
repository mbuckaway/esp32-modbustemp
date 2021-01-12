
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "freertos/queue.h"
#include <esp_event.h>
#include <esp_log.h>

#include <math.h>

#include <hap.h>
#include <hap_apple_servs.h>
#include <hap_apple_chars.h>

#include <iot_button.h>
#include <app_hap_setup_payload.h>
#include "wifi.h"
#include "modbus.h"

#ifdef CONFIG_HOMEKIT_ENABLED

static const char *TAG = "HAP";

/*  Required for server verification during OTA, PEM format as string  */
char server_cert[] = {};

static const uint16_t homekit_TASK_PRIORITY = 1;
static const uint16_t homekit_TASK_STACKSIZE = 4 * 1024;
static const char *homekit_TASK_NAME = "hap";

/* Reset to factory if button is pressed and held for more than 5 seconds */
 static const uint16_t RESET_TO_FACTORY_BUTTON_TIMEOUT = 5;

/* The button "Boot" will be used as the Reset button for the example */
static const uint16_t RESET_GPIO = GPIO_NUM_0;

/* Char definitions for our sensors */
static hap_char_t *temperature_char = 0;
static hap_char_t *humidity_char = 0;

/**
 * @brief The factory reset button callback handler.
 */
static void reset_to_factory_handler(void* arg)
{
    hap_reset_to_factory();
}

/**
 * The Reset button  GPIO initialisation function.
 * Same button will be used for resetting Wi-Fi network as well as for reset to factory based on
 * the time for which the button is pressed.
 */
static void reset_key_init(uint32_t key_gpio_pin)
{
    button_handle_t handle = iot_button_create(key_gpio_pin, BUTTON_ACTIVE_LOW);
    iot_button_add_on_press_cb(handle, RESET_TO_FACTORY_BUTTON_TIMEOUT, reset_to_factory_handler, NULL);
}

/* Mandatory identify routine for the accessory.
 * In a real accessory, something like LED blink should be implemented
 * got visual identification
 */
static int homekit_identify(hap_acc_t *ha)
{
    ESP_LOGI(TAG, "Accessory identified");
    return HAP_SUCCESS;
}

/*
 * An optional HomeKit Event handler which can be used to track HomeKit
 * specific events.
 */
static void homekit_hap_event_handler(void* arg, esp_event_base_t event_base, int event, void *data)
{
    switch(event) {
        case HAP_EVENT_PAIRING_STARTED :
            ESP_LOGI(TAG, "Pairing Started");
            break;
        case HAP_EVENT_PAIRING_ABORTED :
            ESP_LOGI(TAG, "Pairing Aborted");
            break;
        case HAP_EVENT_CTRL_PAIRED :
            ESP_LOGI(TAG, "Controller %s Paired. Controller count: %d",
                        (char *)data, hap_get_paired_controller_count());
            break;
        case HAP_EVENT_CTRL_UNPAIRED :
            ESP_LOGI(TAG, "Controller %s Removed. Controller count: %d",
                        (char *)data, hap_get_paired_controller_count());
            break;
        case HAP_EVENT_CTRL_CONNECTED :
            ESP_LOGI(TAG, "Controller %s Connected", (char *)data);
            break;
        case HAP_EVENT_CTRL_DISCONNECTED :
            ESP_LOGI(TAG, "Controller %s Disconnected", (char *)data);
            break;
        case HAP_EVENT_ACC_REBOOTING : {
            wifi_disable();
            wifi_disconnect();
            char *reason = (char *)data;
            ESP_LOGI(TAG, "Accessory Rebooting (Reason: %s)",  reason ? reason : "null");
            break;
        }
        default:
            /* Silently ignore unknown events */
            break;
    }
}

void temperature_update(float temperature)
{
    ESP_LOGI(TAG, "Updating temperature: %0.01f", temperature);
    hap_val_t new_val;
    new_val.f = temperature;
    hap_char_update_val(temperature_char, &new_val);
}

void humidity_update(float humidity)
{
    ESP_LOGI(TAG, "Updating humidity: %0.01f", humidity);
    hap_val_t new_val;
    new_val.f = humidity;
    hap_char_update_val(humidity_char, &new_val);
}

/* 
 * Read routines are generally not required as the value is available with th HAP core
 * when it is updated from write routines. For external triggers (like fan switched on/off
 * using physical button), accessories should explicitly call hap_char_update_val()
 * instead of waiting for a read request.
 */
static int homekit_read(hap_char_t *hc, hap_status_t *status_code, void *serv_priv, void *read_priv)
{
    if (hap_req_get_ctrl_id(read_priv))
    {
        ESP_LOGI(TAG, "HC sensor received read from %s", hap_req_get_ctrl_id(read_priv));
    }
    if (!strcmp(hap_char_get_type_uuid(hc), HAP_CHAR_UUID_CURRENT_TEMPERATURE)) 
    {
        hap_val_t new_val;
        new_val.f = get_temperature();
        hap_char_update_val(hc, &new_val);
        *status_code = HAP_STATUS_SUCCESS;
        ESP_LOGI(TAG,"homekit ait temp status updated to %0.01f", new_val.f);
    }
    // Only update the sensor info on a temperature read since they are read one after another
    if (!strcmp(hap_char_get_type_uuid(hc), HAP_CHAR_UUID_CURRENT_RELATIVE_HUMIDITY)) 
    {
        hap_val_t new_val;
        new_val.f = get_humidity();;
        hap_char_update_val(hc, &new_val);
        *status_code = HAP_STATUS_SUCCESS;
        ESP_LOGI(TAG,"humidity status updated to %0.01f%%", new_val.f);
    }
    return HAP_SUCCESS;
}

/**
 * @brief Main Thread to handle setting up the service and accessories for the GarageDoor
 */
static void homekit_thread_entry(void *p)
{
    hap_acc_t *homekitaccessory = NULL;
    hap_serv_t *tempservice = NULL;
    hap_serv_t *humidityservice = NULL;

    /* Configure HomeKit core to make the Accessory name (and thus the WAC SSID) unique,
     * instead of the default configuration wherein only the WAC SSID is made unique.
     */
    ESP_LOGI(TAG, "configuring HAP");
    hap_cfg_t hap_cfg;
    hap_get_config(&hap_cfg);
    hap_cfg.unique_param = UNIQUE_NAME;
    hap_set_config(&hap_cfg);

    ESP_LOGI(TAG, "initializing HAP");
    /* Initialize the HAP core */
    hap_init(HAP_TRANSPORT_WIFI);

    /* Initialise the mandatory parameters for Accessory which will be added as
     * the mandatory services internally
     */
    hap_acc_cfg_t cfg = {
        .name = "Esp-Temp",
        .manufacturer = "Espressif",
        .model = "EspTemp02",
        .serial_num = "001122334457",
        .fw_rev = "1.0.0",
        .hw_rev =  (char*)esp_get_idf_version(),
        .pv = "1.0.0",
        .identify_routine = homekit_identify,
        .cid = HAP_CID_SENSOR,
    };
    ESP_LOGI(TAG, "Creating Modbus temperature accessory...");
    /* Create accessory object */
    homekitaccessory = hap_acc_create(&cfg);

    /* Add a dummy Product Data */
    uint8_t product_data[] = {'E','S','P','3','2','H','A','P'};
    hap_acc_add_product_data(homekitaccessory, product_data, sizeof(product_data));

    float temperature = get_temperature();
    float humidity = get_humidity();
    ESP_LOGI(TAG, "Creating temperature service (current temp: %0.01fC)", temperature);
    /* Create the temp Service. Include the "name" since this is a user visible service  */
    tempservice = hap_serv_temperature_sensor_create(temperature);
    hap_serv_add_char(tempservice, hap_char_name_create("ESP Temperature Sensor"));
    /* Set the read callback for the service (optional) */
    hap_serv_set_read_cb(tempservice, homekit_read);
    /* Add the Garage Service to the Accessory Object */
    hap_acc_add_serv(homekitaccessory, tempservice);
    temperature_char = hap_serv_get_char_by_uuid(tempservice, HAP_CHAR_UUID_CURRENT_TEMPERATURE);

    ESP_LOGI(TAG, "Creating humidity service (current humidity: %0.01f%%)", humidity);
    /* Create the humidity Service. Include the "name" since this is a user visible service  */
    humidityservice = hap_serv_humidity_sensor_create(humidity);
    hap_serv_add_char(humidityservice, hap_char_name_create("ESP Humidity Sensor"));
    /* Set the read callback for the service (optional) */
    hap_serv_set_read_cb(humidityservice, homekit_read);
    /* Add the Garage Service to the Accessory Object */
    hap_acc_add_serv(homekitaccessory, humidityservice);
    humidity_char = hap_serv_get_char_by_uuid(humidityservice, HAP_CHAR_UUID_CURRENT_TEMPERATURE);


#if 0
    /* Create the Firmware Upgrade HomeKit Custom Service.
     * Please refer the FW Upgrade documentation under components/homekit/extras/include/hap_fw_upgrade.h
     * and the top level README for more information.
     */
    hap_fw_upgrade_config_t ota_config = {
        .server_cert_pem = server_cert,
    };
    service = hap_serv_fw_upgrade_create(&ota_config);
    /* Add the service to the Accessory Object */
    hap_acc_add_serv(accessory, service);
#endif

    /* Add the Accessory to the HomeKit Database */
    ESP_LOGI(TAG, "Adding Temperature Accessory...");
    hap_add_accessory(homekitaccessory);

    /* Register a common button for reset Wi-Fi network and reset to factory.
     */
    ESP_LOGI(TAG, "Register reset GPIO (reset button) on pin %d", RESET_GPIO);
    reset_key_init(RESET_GPIO);

    /* Register an event handler for HomeKit specific events */
    esp_event_handler_register(HAP_EVENT, ESP_EVENT_ANY_ID, &homekit_hap_event_handler, NULL);

    /* Query the controller count (just for information) */
    ESP_LOGI(TAG, "Accessory is paired with %d controllers", hap_get_paired_controller_count());


    /* For production accessories, the setup code shouldn't be programmed on to
     * the device. Instead, the setup info, derived from the setup code must
     * be used. Use the factory_nvs_gen utility to generate this data and then
     * flash it into the factory NVS partition.
     *
     * By default, the setup ID and setup info will be read from the factory_nvs
     * Flash partition and so, is not required to set here explicitly.
     *
     * However, for testing purpose, this can be overridden by using hap_set_setup_code()
     * and hap_set_setup_id() APIs, as has been done here.
     */
#ifdef CONFIG_HOMEKIT_USE_HARDCODED_SETUP_CODE
    /* Unique Setup code of the format xxx-xx-xxx. Default: 111-22-333 */
    hap_set_setup_code(CONFIG_HOMEKIT_SETUP_CODE);
    /* Unique four character Setup Id. Default: ES32 */
    hap_set_setup_id(CONFIG_HOMEKIT_SETUP_ID);
#ifdef CONFIG_APP_WIFI_USE_WAC_PROVISIONING
    app_hap_setup_payload(CONFIG_HOMEKIT_SETUP_CODE, CONFIG_HOMEKIT_SETUP_ID, true, cfg.cid);
#else
    app_hap_setup_payload(CONFIG_HOMEKIT_SETUP_CODE, CONFIG_HOMEKIT_SETUP_ID, false, cfg.cid);
#endif
#endif

    /* mfi is not supported */
    hap_enable_mfi_auth(HAP_MFI_AUTH_NONE);

    /* After all the initializations are done, start the HAP core */
    ESP_LOGI(TAG, "Starting HAP...");
#ifdef CONFIG_HAP_DEBUG    
    hap_http_debug_enable();
#endif
    hap_start();

    ESP_LOGI(TAG, "HAP initialization complete.");

   /* The task ends here. The read/write callbacks will be invoked by the HAP Framework */
    vTaskDelete(NULL);
}


void homekit_start(void)
{
    ESP_LOGI(TAG, "Creating homekit thread...");
    xTaskCreate(homekit_thread_entry, homekit_TASK_NAME, homekit_TASK_STACKSIZE, NULL, homekit_TASK_PRIORITY, NULL);
}
#endif
