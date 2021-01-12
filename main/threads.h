#pragma once

#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"

/**
 * Defines the threads in the application and their associated priorities. We define them
 * here to give some clarify as to what is running in the application.
 * 
 * - The MODBUS/MQTT needs a higher priority that other applications.
 * - Higher numbers are high priorities
 */

// WIFI Monitor Thread
#define THREAD_WIFI_NAME "wifi_connected"
#define THREAD_WIFI_STACKSIZE configMINIMAL_STACK_SIZE * 4
#define THREAD_WIFI_PRIORITY 4

// MQTT Thread
#define THREAD_MQTT_NAME "mqttpublish"
#define THREAD_MQTT_STACKSIZE configMINIMAL_STACK_SIZE * 8
#define THREAD_MQTT_PRIORITY 9

// MODBUS Thread (used for testing only)
#define THREAD_MODBUS_NAME "modbus_reader"
#define THREAD_MODBUS_PRIORITY 5
#define THREAD_MODBUS_STACKSIZE configMINIMAL_STACK_SIZE * 4

// Make sure we configure MQTT with a different priority than the above
#if THREAD_MQTT_PRIORITY < 6
#error "MQTT_TASK_PRIORITY must us 6 or higher"
#endif

// MODBUS has a priority if 10 by default, make sure it never got changed.
#ifdef FMB_SERIAL_TASK_PRIO
#if FMB_SERIAL_TASK_PRIO < 10
#error "FMB_SERIAL_TASK_PRIO (MODBUS priority) must is 10 or better"
#endif
#endif

#ifdef CONFIG_FMB_PORT_TASK_PRIO
#if CONFIG_FMB_PORT_TASK_PRIO <10
#error "FMB_PORT_TASK_PRIO (MODBUS priority) must is 10 or better"
#endif
#endif
