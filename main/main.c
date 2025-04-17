#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"

#include "nvs_flash.h"
#include "esp_wifi.h"
#include "bme280.h"
#include "esp_http_client.h"

#define TAG "WIFI"

// WIFI CONFIG
#define WIFI_SSID "别蹭我网"
#define WIFI_PASSWORD "szc7777777"

// BME280 CONFIG
#define BME280_I2C_MASTER_FREQ_HZ 100000
#define BME280_I2C_MASTER_NUM I2C_NUM_0
#define BME280_SCL_IO GPIO_NUM_1
#define BME280_SDA_IO GPIO_NUM_2
static i2c_bus_handle_t bme280_i2c_bus = NULL;
static bme280_handle_t bme280 = NULL;

void wifi_event_handler(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "ESP32 S3 Connected to AP");
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                esp_wifi_connect();
                ESP_LOGI(TAG, "ESP32 S3 Connected to AP Failed! retrying ...");
                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT)
    {
        switch (event_id)
        {
            case IP_EVENT_STA_GOT_IP:
                ESP_LOGI(TAG, "ESP32 S3 Got IP");
                break;
            default:
                break;
        }
    }
}

void app_main(void)
{
    // connect to wifi
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);

    wifi_config_t wifi_config = {
        .sta.threshold.authmode = WIFI_AUTH_WPA2_PSK,
        .sta.pmf_cfg.capable = true,
        .sta.pmf_cfg.required = false,

        .sta.channel = 7,
        .sta.threshold.rssi = -65,
    };
    memset(&wifi_config.sta.password, 0, sizeof(wifi_config.sta.password));
    memcpy(wifi_config.sta.ssid, WIFI_SSID, strlen(WIFI_SSID));

    memset(&wifi_config.sta.password, 0, sizeof(wifi_config.sta.password));
    memcpy(wifi_config.sta.password, WIFI_PASSWORD, strlen(WIFI_PASSWORD));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // bme280 init
    i2c_config_t bme280_config = {
        .mode = I2C_MODE_MASTER,
        .scl_io_num = BME280_SCL_IO,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .sda_io_num = BME280_SDA_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = BME280_I2C_MASTER_FREQ_HZ
    };
    bme280_i2c_bus = i2c_bus_create(BME280_I2C_MASTER_NUM, &bme280_config);

    bme280 = bme280_create(bme280_i2c_bus, BME280_I2C_ADDRESS_DEFAULT);
    bme280_default_init(bme280);

    float temperature = 0.0, humidity = 0.0, pressure = 0.0;

    // delay 10s to wait for wifi connected
    vTaskDelay(pdMS_TO_TICKS(10000));

    while (1)
    {
        // http init
        esp_http_client_config_t http_config = {
            .url = "http://39.106.60.68:3377",
            .method = HTTP_METHOD_GET,
            .user_agent= "ESP32-S3",
            .timeout_ms = 15000,
        };

        esp_http_client_handle_t http_client = esp_http_client_init(&http_config);

        // bme280 read
        bme280_read_temperature(bme280, &temperature);
        bme280_read_humidity(bme280, &humidity);
        bme280_read_pressure(bme280, &pressure);

        printf("温度: %.2f C, 湿度: %.2f %%, 压力: %.2f hPa\n", temperature, humidity, pressure);

        char url_with_params[256];
        snprintf(url_with_params, sizeof(url_with_params), "/set?temperature=%.2f&humidity=%.2f&pressure=%.2f", temperature, humidity, pressure);

        esp_http_client_set_url(http_client, url_with_params);

        esp_err_t http_err = esp_http_client_perform(http_client);

        esp_err_t ret;
        for (int retry = 0; retry < 3; retry++) {
            ret = esp_http_client_perform(http_client);
            if (ret == ESP_OK && esp_http_client_get_status_code(http_client) == 200) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(2000));
        }

        if (http_err == ESP_OK)
        {
            int status_code = esp_http_client_get_status_code(http_client);
            printf("HTTP Status Code: %d\n", status_code);
        }
        else
        {
            printf("HTTP request failed: %s\n", esp_err_to_name(http_err));
        }

        int status_code = esp_http_client_get_status_code(http_client);
        ESP_LOGI(TAG, "Status:%d", status_code);
        esp_http_client_cleanup(http_client);

        vTaskDelay(pdMS_TO_TICKS(60000));
    };
}