/*
    MQTT support code

    (C) 2020 Mark Buckaway - Apache License Version 2.0, January 2004
*/

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "driver/gpio.h"
#include "driver/adc.h"

#include "esp_log.h"
#include "wifi.h"
#include "mqtt_client.h"
#include "modbus.h"
#include "threads.h"
//#include "led.h"

static const char *TAG = "MQTT";

#define MQTT_CONNECTED_BIT BIT0
#define MQTT_NOWONLINE_BIT BIT1
#define DATA_LEN 256
#define STATUS_LEN 20

static char topic_string[DATA_LEN];
static esp_mqtt_client_handle_t client;

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_mqtt_event_group;

void publish(char *data)
{
    char *defaultdata = "status=ONLINE";
    if (!data)
    {
        data = defaultdata;
    }
#ifdef CONFIG_THINKSPEAK_LOG_PASSWDS_IN_LOGS
    ESP_LOGI(TAG, "Publishing: %s for %s", topic_string, data);
#else
    ESP_LOGI(TAG, "Publishing: %s", data);
#endif    
#ifdef CONFIG_THINKSPEAK_DONT_PUBLISH
#warning "MQTT publish is disabled!"
    ESP_LOGI(TAG, "Publish has been disabled");
#else
    int msg_id = esp_mqtt_client_publish(client, topic_string, data, 0, 0, 0);

    if (msg_id==-1)
    {
        ESP_LOGE(TAG, "sent status unsuccessful, msg_id=%d", msg_id);
    }
    else if (msg_id>0)
    {
        ESP_LOGI(TAG, "sent status successful but queue, msg_id=%d", msg_id);
    }
    else
    {
        ESP_LOGI(TAG, "sent status successful (sent immediately), msg_id=%d", msg_id);
    }
#endif        

}

static void go_online()
{
    char *data = calloc(1, DATA_LEN);
    ESP_LOGI(TAG, "Sending status update");
    sprintf(data, "status=ONLINE");
    publish(data);
    free(data);
}

static void mqttpublish(void *pvParameter)
{
    char *data = calloc(1, DATA_LEN);
    char *status = calloc(1, STATUS_LEN);
//    const uint32_t error_delay = (2000) / portTICK_PERIOD_MS;
#ifdef CONFIG_THINKSPEAK_DONT_PUBLISH
    const uint32_t delay = 4000 / portTICK_PERIOD_MS;
#else    
    const uint32_t delay = (CONFIG_THINKSPEAK_LOOP_DELAY_SECONDS*1000) / portTICK_PERIOD_MS;

#endif
    
    if (!data)
    {
        ESP_LOGE(TAG, "Unable alloc data memory! MQTT aborted!");
        free(data);
        free(status);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "MQTT_PUBLISH_STARTED");
    while (1)
    {

        // Sit and wait until something happens
        EventBits_t bits = xEventGroupWaitBits(s_mqtt_event_group,
                MQTT_CONNECTED_BIT | MQTT_NOWONLINE_BIT,
                pdFALSE,
                pdFALSE,
                portMAX_DELAY);

        if (bits & MQTT_NOWONLINE_BIT)
        {
            xEventGroupClearBits(s_mqtt_event_group, MQTT_NOWONLINE_BIT);
            go_online();
            continue;
        }
        if (bits & MQTT_CONNECTED_BIT)
        {
            // Read the EPsolar box here
            read_modbus();
            status = "GOOD_ESP";
            snprintf(data, DATA_LEN, "field1=%0.02f&field2=%0.02f&status=%s", 
                            get_epever_value(CID_INP_DATA_TEMPERATURE),
                            get_epever_value(CID_INP_DATA_HUMIDITY),
                            status
                            );
            publish(data);
        }
        else
        {
            ESP_LOGI(TAG,"Not connected");
        }
        vTaskDelay(delay);
    }
}

static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    char *errortype = NULL;
    char *connecterror = NULL;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            xEventGroupSetBits(s_mqtt_event_group, MQTT_NOWONLINE_BIT);
            xEventGroupSetBits(s_mqtt_event_group, MQTT_CONNECTED_BIT);
            //led_off();
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            xEventGroupClearBits(s_mqtt_event_group, MQTT_CONNECTED_BIT);
            //led2_on();
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA: TOPIC=%s - DATA=%s", event->topic, event->data);
            break;
        case MQTT_EVENT_ERROR:
            switch (event->error_handle->error_type)
            {
                case MQTT_ERROR_TYPE_ESP_TLS:
                    errortype = "ESP TLS Error";
                    break;
                case MQTT_ERROR_TYPE_CONNECTION_REFUSED:
                    errortype = "Connection Refused";
                    break;
                default:
                    errortype = "Unknown error";
                    break;
            } 
            switch (event->error_handle->connect_return_code)
            {
                case MQTT_CONNECTION_REFUSE_PROTOCOL:
                    connecterror = "Refused Protocol";
                    break;
                case MQTT_CONNECTION_REFUSE_ID_REJECTED:
                    connecterror = "Refused Id Rejected";
                    break;
                case MQTT_CONNECTION_REFUSE_BAD_USERNAME:
                    connecterror = "Refused Bad Username";
                    break;
                case MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED:
                    connecterror = "Refused Not Authorized";
                    break;
                default:
                    connecterror = "Unknown connect error";
                    break;
            } 
          
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR: %s: %s", errortype, connecterror);
            break;
        case MQTT_EVENT_BEFORE_CONNECT:
            ESP_LOGI(TAG, "MQTT_EVENT_BEFORE_CONNECT");
            break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
    return ESP_OK;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    mqtt_event_handler_cb(event_data);
}

#define MAX_ID_STRING (32)

static char *create_id_string(void)
{
    uint8_t mac[6];
    char *id_string = calloc(1, MAX_ID_STRING);
    if (id_string != NULL)
    {
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        sprintf(id_string, "mqttx_%02x%02X%02X", mac[3], mac[4], mac[5]);
    }
    return id_string;
}

void mqtt_app_start(void)
{
    snprintf(topic_string, DATA_LEN, "channels/%d/publish/%s", CONFIG_THINKSPEAK_CHANNELID, CONFIG_THINKSPEAK_CHANNEL_WRITE_KEY);

#ifdef CONFIG_THINKSPEAK_LOG_PASSWDS_IN_LOGS
    ESP_LOGI(TAG, "[APP] Using topic: %s", topic_string);
#else
    ESP_LOGI(TAG, "[APP] Display of topic string disabled");
#endif    

    s_mqtt_event_group = xEventGroupCreate();

    // We run create_id_string here because we want to know what the client id is
    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = CONFIG_BROKER_URL,
        .client_id = create_id_string(),
        .username = "thingspeak",
        .password = CONFIG_THINKSPEAK_MQTT_KEY
    };

    // Wait for the WIFI to come up
    wifi_waitforconnect();

    client = esp_mqtt_client_init(&mqtt_cfg);
#ifdef CONFIG_THINKSPEAK_LOG_PASSWDS_IN_LOGS
    ESP_LOGI(TAG, "URL: %s and Login: '%s' Pass: '%s' ClientId: '%s'", mqtt_cfg.uri, mqtt_cfg.username, mqtt_cfg.password, mqtt_cfg.client_id); 
#else
    ESP_LOGI(TAG, "URL: %s and Login: '%s' ClientId: '%s'", mqtt_cfg.uri, mqtt_cfg.username, mqtt_cfg.client_id); 
#endif    
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);
    xTaskCreate(mqttpublish, THREAD_MQTT_NAME, THREAD_MQTT_STACKSIZE, NULL, THREAD_MQTT_PRIORITY, NULL);

}
