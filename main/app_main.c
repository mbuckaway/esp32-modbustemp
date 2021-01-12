//    ESP 32 EPSolar Charge Control Modbus logger
//
//    Code takes inputs from a EPSolar Tracer Charge controller via it's modbus RS485
//    port, and uploads select data items to Thingspeak via MQTT. Code currently connects to a
//    fixed WIFI network but can be easily adapted to app provisioning.
//
//    (C) 2020 Mark Buckaway - Apache License Version 2.0, January 2004
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


#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#ifdef CONFIG_THINKSPEAK_ENABLE
#include "mqtt.h"
#else
#include "modbus.h"
#endif
#include "led.h"
#include "modbus.h"
#include "sdkconfig.h"
#ifdef CONFIG_HOMEKIT_ENABLED
#include "homekit.h"
#endif


static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_INFO);
    esp_log_level_set("MQTT", ESP_LOG_INFO);
    esp_log_level_set("WIFICTRL", ESP_LOG_INFO);
    esp_log_level_set("TRANSPORT_TCP", ESP_LOG_INFO);
    esp_log_level_set("TRANSPORT_SSL", ESP_LOG_INFO);
    esp_log_level_set("TRANSPORT", ESP_LOG_INFO);
    esp_log_level_set("OUTBOX", ESP_LOG_INFO);
#if CONFIG_MB_TEST_MODE || CONFIG_THINKSPEAK_DONT_PUBLISH
#ifndef CONFIG_LOG_DEFAULT_LEVEL_DEBUG
#error "CONFIG_LOG_DEFAULT_LEVEL_DEBUG must be defined in sdkconfig"
#endif
    esp_log_level_set("MB_CONTROLLER_MASTER", ESP_LOG_DEBUG);
    esp_log_level_set("MB_PORT_TAG", ESP_LOG_DEBUG);
    esp_log_level_set("MB_SERIAL", ESP_LOG_DEBUG);
    esp_log_level_set("MB_MASTER_SERIAL", ESP_LOG_DEBUG);
#endif
#if 0
    esp_log_level_set("MB_CONTROLLER_MASTER", ESP_LOG_DEBUG);
    esp_log_level_set("MB_PORT_TAG", ESP_LOG_DEBUG);
    esp_log_level_set("MB_SERIAL", ESP_LOG_DEBUG);
    esp_log_level_set("MB_MASTER_SERIAL", ESP_LOG_DEBUG);
#endif

    ESP_ERROR_CHECK(modbus_init());

    configure_led();
    led_both();

    void (*ptr_1on)() = &led1_on;
    set_led_disconnected_callback(ptr_1on);
    void (*ptr_off)() = &led_off;
    set_led_connected_callback(ptr_off);

    wifi_setup();
    wifi_connect();

#ifdef CONFIG_HOMEKIT_ENABLED
    homekit_start();
#endif    

#ifdef CONFIG_THINKSPEAK_ENABLE
    mqtt_app_start();
#else
    modbus_start();    
#endif
}
