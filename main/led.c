/*
    Addressable LED support

    (C) 2020 Mark Buckaway - Apache License Version 2.0, January 2004
*/

#include "esp_log.h"
#include "driver/gpio.h"
#include "led.h"

static const char *TAG = "LED";

void led_off(void)
{
    ESP_LOGI(TAG, "LED OFF");
    gpio_set_level(CONFIG_LED1_GPIO, 0);
    gpio_set_level(CONFIG_LED2_GPIO, 0);
}

void led1_on(void)
{
    ESP_LOGI(TAG, "LED ONE ON");
    gpio_set_level(CONFIG_LED1_GPIO, 1);
    gpio_set_level(CONFIG_LED2_GPIO, 0);
}

void led2_on(void)
{
    ESP_LOGI(TAG, "LED TWO ON");
    gpio_set_level(CONFIG_LED1_GPIO, 0);
    gpio_set_level(CONFIG_LED2_GPIO, 1);
}

void led_both(void)
{
    ESP_LOGI(TAG, "LED BOTH ON");
    gpio_set_level(CONFIG_LED1_GPIO, 1);
    gpio_set_level(CONFIG_LED2_GPIO, 1);
}

#define GPIO_OUTPUT_PIN_SEL  ((1ULL<<CONFIG_LED1_GPIO) | (1ULL<<CONFIG_LED2_GPIO))

void configure_led(void)
{
    ESP_LOGI(TAG, "Configuring LEDS on GPIO %d and %d", CONFIG_LED1_GPIO, CONFIG_LED2_GPIO);
    gpio_config_t io_conf_leds = {
        .intr_type = GPIO_PIN_INTR_DISABLE,
        .pin_bit_mask = GPIO_OUTPUT_PIN_SEL,
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = 0,
        .pull_up_en = 0
    };
    gpio_config(&io_conf_leds);

}
