# XY-MD02 ModBus Temperature/Humidity Monitor with Homekit/Thinkspeak Support

## Purpose

The purpose of this code is to monitor the temperture/humidity on the XY MD02 Modbus Temp/Humidity sensor and upload data to Thinkspeak. The sensor can be found on [Aliexpress](https://www.aliexpress.com/item/1005001475675808.html). This code is work in progress as the Espressif Modbus support is flakey. To work correctly, IDF v4.2 along with the Modbus patch on [Issue #6134](https://github.com/espressif/esp-idf/issues/6134) is required...and then it still make not work consistently. Timing issues occur with the modbus thread and other threads.

The code is built using the ESP-IDF and not Ardrino. Espresif provides the modbus driver based on the Freemodbus source. This code is heavily based on the Modbus Master and MQTT examples from Espressif ESP-IDF examples with some if my own code layered on top to make it do what I want.

This code is an attempt to get both Homekit and Thinkspeak support. If Thinkspeak is enabld, it sets up a thread to read the sensor. Homekit support runs passively in it's own thread and responds when requested. The temperature and humidity values are stored in the modbus code. Without Thinkspeak, the modbus code runs a thread to poll the sensor. Homekit responds better when it returns immediately.

The device solves the problem of running a temperature/humidity sensor outside while powering a ESP32 board. THe modbus sensor allows the sensor to be located outside, and the ESP32 board to be located inside. In the future, additional sensors could be added to the modbus (wind/rain/etc.).

## Hardware required

ESP32 board and a MAX485 (or similar) line driver is used as an example below but other similar chips can be used as well. The ESP32 modbus support is experimental. In order to get reliable communications, you must use the v4.2 branch and apply the patch in Issue 6134 as stated above.

The MAX485 board is powered from +3.3V from the ESP32, and wired as following:

MAX485 module <-> ESP32

* DI -> GPIO for TX
* RO -> GPIO for RX
* DE and RE are interconnected with a jumper and then connected to GPIO for RTS
* VCC to +3.3V / VIN on ESP32
* A/B on Sensor <-> MAX485

## Configuration

### Configure the application

Start the command below to setup configuration:

```bash
idf.py menuconfig
```

Configure the UART pins used for modbus communication using and table below.

The XY-MD02 requires the modbus mode be set to RTU and the default baud rate is 9600. 

The code permits almost any set of pins to be used for the UART with the restrictions noted below.

  | ESP32 Interface       | #define            | Default ESP32 Pin     | Default ESP32-S2 Pins | External RS485 Driver Pin |
  | ----------------------|--------------------|-----------------------|-----------------------|---------------------------|
  | Transmit Data (TxD)   | CONFIG_MB_UART_TXD | GPIO26                | GPIO20                | DI                        |
  | Receive Data (RxD)    | CONFIG_MB_UART_RXD | GPIO25                | GPIO19                | RO                        |
  | Request To Send (RTS) | CONFIG_MB_UART_RTS | GPIO27                | GPIO18                | ~RE/DE                    |
  | Ground                | n/a                | GND                   | GND                   | GND                       |

Note: The GPIO22 - GPIO25 can not be used with ESP32-S2 chip because they are used for flash chip connection. Please refer to UART documentation for selected target.

### WIFI and MQTT Config

Both the WIFI and MQTT sections from menuconfig must be setup. MQTT can speak to anything, but the code has been setup to connect to Thinkspeak and their channel configuration. Your API key (from your Thinkspeak profile) and your write key are required. The read key can be ignored as it is for future development. The disable publish item disable sending data to Thinkspeak, but runs threw the motions of connecting to MQTT, etc.. The last item is the timeout between successive data uploads (default is one minute).

The WIFI section has places for two SSIDs and password. The intend is one for development (home) and one for the field - this just saves having to change the SSID when deploying the board. A future release might use the bluetooth provisioning provided by the Espressif app. For the time being, it was overkill for my needs. The last item is the timeout between retries of the WIFI connection. The WIFI code monitors the connection, and should it drop for any reason, it will wait the timeout, and retry. The last option (not should) sets the number of times the WIFI will attempt a connect before the ESP32 is restarted. I've see in the field where WIFI will never reconnect and a restart is required. This does it automatically.

### Build and flash software of master device

Build the project and flash it to the board, then run monitor tool to view serial output:
```
idf.py -p PORT flash monitor
```

(To exit the serial monitor, type ``Ctrl-]``.)

See the Getting Started Guide for full steps to configure and use ESP-IDF to build projects.

## Example Output
Example log of the application:
```
I (1579) wifi:AP's beacon interval = 102400 us, DTIM period = 1
I (2519) esp_netif_handlers: sta ip: 192.168.20.156, mask: 255.255.255.0, gw: 192.168.20.1
I (2519) WIFICTRL: got ip:192.168.20.156
I (2519) LED: LED ONE ON
I (2519) MQTT: URL: mqtt://mqtt.thingspeak.com and Login: 'thingspeak' Pass: 'PASSWORD' ClientId: 'mqttx_5e6870'
I (2539) MQTT: MQTT_PUBLISH_STARTED
I (2539) MQTT: MQTT_EVENT_BEFORE_CONNECT
I (2839) MQTT: MQTT_EVENT_CONNECTED
I (2839) MQTT: Sending status update
I (2839) MQTT: Publishing: channels/1228246/publish/PASSWORD for status=ONLINE
I (6955520) MODBUS: Reading modbus data...
D (6955530) MB_PORT_COMMON: eMBMasterRTUSend: Port enter critical.
D (6955530) MB_PORT_COMMON: eMBMasterRTUSend: Port exit critical
D (6955530) MB_MASTER_SERIAL: MB_TX_buffer sent: (9) bytes.
D (6955540) MB_MASTER_SERIAL: MB_uart[2] event:
D (6955540) MB_MASTER_SERIAL: uart rx break.
D (6955690) MB_MASTER_SERIAL: MB_uart[2] event:
D (6955690) MB_MASTER_SERIAL: Data event, len: 7.
D (6955690) MB_PORT_COMMON: eMBMasterRTUReceive: Port enter critical.
D (6955690) MB_PORT_COMMON: eMBMasterRTUReceive: Port exit critical
D (6955690) MB_MASTER_SERIAL: Received data: 8(bytes in buffer)

D (6955700) MB_CONTROLLER_MASTER: mbc_serial_master_get_parameter: Good response for get cid(0) = ESP_OK
D (6955700) MB_MASTER_SERIAL: Timeout occurred, processed: 8 bytes
I (6955720) MODBUS: Characteristic #0 Temperature (C) value = 22.50 (0xe1) read successful.
D (6955790) MB_PORT_COMMON: eMBMasterRTUSend: Port enter critical.
D (6955790) MB_PORT_COMMON: eMBMasterRTUSend: Port exit critical
D (6955790) MB_MASTER_SERIAL: MB_TX_buffer sent: (9) bytes.
D (6955790) MB_MASTER_SERIAL: MB_uart[2] event:
D (6955800) MB_MASTER_SERIAL: uart rx break.
D (6955890) MB_MASTER_SERIAL: MB_uart[2] event:
D (6955890) MB_MASTER_SERIAL: Data event, len: 7.
D (6955890) MB_PORT_COMMON: eMBMasterRTUReceive: Port enter critical.
D (6955890) MB_PORT_COMMON: eMBMasterRTUReceive: Port exit critical
D (6955890) MB_MASTER_SERIAL: Received data: 8(bytes in buffer)

D (6955900) MB_CONTROLLER_MASTER: mbc_serial_master_get_parameter: Good response for get cid(1) = ESP_OK
D (6955900) MB_MASTER_SERIAL: Timeout occurred, processed: 8 bytes
I (6955920) MODBUS: Characteristic #1 Humidity (%) value = 38.40 (0x180) read successful.
I (6955990) MQTT: Publishing: field1=22.50&field2=38.40&status=GOOD_ESP
I (6955990) MQTT: sent status successful (sent immediately), msg_id=0

```
