/* Flash multiple partitions example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <sys/param.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp32_port.h"
#include "esp_loader.h"
#include "example_common.h"
#include "ssd1306.h"
#include "font8x8_basic.h"
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_ota_ops.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <otadrive_esp.h>
#include "esp_wifi.h"

#define EXAMPLE_NETIF_DESC_STA "WiFi-IDF"

// ===== WiFi Credentials
#define WIFI_SSID "roshan_ssid"
#define WIFI_PASS "password"

// ===== OTAdrive configurations
#define OTADRIVE_APIKEY "2022bdac-96ea-4de9-859c-3b0156cb7e11"
#define APP_VERSION "v@1"

static esp_netif_t *s_example_sta_netif = NULL;
static SemaphoreHandle_t s_semph_get_ip_addrs = NULL;
static int s_retry_num = 0;

esp_err_t example_wifi_connect(void);

static const char *TAG = "serial_flasher";
static const char *OTA_TAG = "otadrive";

#define tag "SSD1306"
SSD1306_t dev;

#define HIGHER_BAUDRATE 230400
// ========================== OTA section
static void otadrive_event_handler(void *arg, esp_event_base_t event_base,
                                   int32_t event_id, void *event_data)
{
    if (event_base == ESP_HTTPS_OTA_EVENT)
    {
        switch (event_id)
        {
        case ESP_HTTPS_OTA_START:
            ESP_LOGI(OTA_TAG, "OTA started");
            break;
        case ESP_HTTPS_OTA_CONNECTED:
            ESP_LOGI(OTA_TAG, "Connected to server");
            break;
        case ESP_HTTPS_OTA_GET_IMG_DESC:
            ESP_LOGI(OTA_TAG, "Reading Image Description");
            break;
        case ESP_HTTPS_OTA_VERIFY_CHIP_ID:
            ESP_LOGI(OTA_TAG, "Verifying chip id of new image: %d", *(esp_chip_id_t *)event_data);
            break;
        case ESP_HTTPS_OTA_DECRYPT_CB:
            ESP_LOGI(OTA_TAG, "Callback to decrypt function");
            break;
        case ESP_HTTPS_OTA_WRITE_FLASH:
            ESP_LOGD(OTA_TAG, "Writing to flash: %d written", *(int *)event_data);
            break;
        case ESP_HTTPS_OTA_UPDATE_BOOT_PARTITION:
            ESP_LOGI(OTA_TAG, "Boot partition updated. Next Partition: %d", *(esp_partition_subtype_t *)event_data);
            break;
        case ESP_HTTPS_OTA_FINISH:
            ESP_LOGI(OTA_TAG, "OTA finish");
            break;
        case ESP_HTTPS_OTA_ABORT:
            ESP_LOGI(OTA_TAG, "OTA abort");
            break;
        }
    }
}

void otadrive_thread(void *pvParameter)
{
    while (1)
    {
        if (otadrive_timeTick(120))
        {
            otadrive_result r = otadrive_updateFirmwareInfo();
            ESP_LOGI(OTA_TAG, "Check new OTA result %d,%lu", r.code, r.size);
            if (r.available)
            {
                ssd1306_display_text(&dev, 4, " --UPDATING--", 14, false);
                ESP_LOGI(OTA_TAG, "Lets download new firmware %s,%luBytes. Current firmware is %s",
                         r.version, r.size, otadrive_currentversion());

                r = otadrive_updateFirmware(false);
                if (r.code == OTADRIVE_Success)
                {
                    // do something to prepare device for reboot
                    esp_restart();
                }

                ESP_LOGI(OTA_TAG, "OTA Download Result %d", r.code);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{

    int center, top, bottom;
    char lineChar[20];

#if CONFIG_I2C_INTERFACE
    ESP_LOGI(tag, "INTERFACE is i2c");
    ESP_LOGI(tag, "CONFIG_SDA_GPIO=%d", CONFIG_SDA_GPIO);
    ESP_LOGI(tag, "CONFIG_SCL_GPIO=%d", CONFIG_SCL_GPIO);
    ESP_LOGI(tag, "CONFIG_RESET_GPIO=%d", CONFIG_RESET_GPIO);
    i2c_master_init(&dev, CONFIG_SDA_GPIO, CONFIG_SCL_GPIO, CONFIG_RESET_GPIO);
#endif // CONFIG_I2C_INTERFACE

#if CONFIG_SPI_INTERFACE
    ESP_LOGI(tag, "INTERFACE is SPI");
    ESP_LOGI(tag, "CONFIG_MOSI_GPIO=%d", CONFIG_MOSI_GPIO);
    ESP_LOGI(tag, "CONFIG_SCLK_GPIO=%d", CONFIG_SCLK_GPIO);
    ESP_LOGI(tag, "CONFIG_CS_GPIO=%d", CONFIG_CS_GPIO);
    ESP_LOGI(tag, "CONFIG_DC_GPIO=%d", CONFIG_DC_GPIO);
    ESP_LOGI(tag, "CONFIG_RESET_GPIO=%d", CONFIG_RESET_GPIO);
    spi_master_init(&dev, CONFIG_MOSI_GPIO, CONFIG_SCLK_GPIO, CONFIG_CS_GPIO, CONFIG_DC_GPIO, CONFIG_RESET_GPIO);
#endif // CONFIG_SPI_INTERFACE

#if CONFIG_FLIP
    dev._flip = true;
    ESP_LOGW(tag, "Flip upside down");
#endif

#if CONFIG_SSD1306_128x64
    ESP_LOGI(tag, "Panel is 128x64");
    ssd1306_init(&dev, 128, 64);
#endif // CONFIG_SSD1306_128x64
#if CONFIG_SSD1306_128x32
    ESP_LOGI(tag, "Panel is 128x32");
    ssd1306_init(&dev, 128, 32);
#endif // CONFIG_SSD1306_128x32

#if CONFIG_SSD1306_128x64
    top = 2;
    center = 3;
    bottom = 8;
#endif // CONFIG_SSD1306_128x64

#if CONFIG_SSD1306_128x32
    top = 1;
    center = 1;
    bottom = 4;
#endif // CONFIG_SSD1306_128x32
    ssd1306_clear_screen(&dev, false);

    example_binaries_t bin;

    const loader_esp32_config_t config = {
        .baud_rate = 115200,
        .uart_port = UART_NUM_1,
        .uart_rx_pin = GPIO_NUM_5,
        .uart_tx_pin = GPIO_NUM_4,
        .reset_trigger_pin = GPIO_NUM_25,
        .gpio0_trigger_pin = GPIO_NUM_26,
    };

    if (loader_port_esp32_init(&config) != ESP_LOADER_SUCCESS)
    {
        ESP_LOGE(TAG, "serial initialization failed.");
        return;
    }

    if (connect_to_target(HIGHER_BAUDRATE) == ESP_LOADER_SUCCESS)
    {

        get_example_binaries(esp_loader_get_target(), &bin);
        ssd1306_display_text(&dev, 0, "    FLASHING  ", 15, false);
        ESP_LOGI(TAG, "Loading bootloader...");
        ssd1306_display_text(&dev, 2, " 1.BOOTLOADER", 14, false);
        flash_binary(bin.boot.data, bin.boot.size, bin.boot.addr);
        ssd1306_display_text(&dev, 3, " 2.PARTITION", 13, false);
        ESP_LOGI(TAG, "Loading partition table...");
        flash_binary(bin.part.data, bin.part.size, bin.part.addr);
        ssd1306_display_text(&dev, 4, " 3.FIRMWARE", 12, false);
        ESP_LOGI(TAG, "Loading app...");
        flash_binary(bin.app.data, bin.app.size, bin.app.addr);
        ssd1306_display_text(&dev, 5, " 4.SPIFFS", 10, false);
        ESP_LOGI(TAG, "Loading SPIFFS...");
        flash_binary(bin.spiffs.data, bin.spiffs.size, bin.spiffs.addr);
        ESP_LOGI(TAG, "Done!");
        ssd1306_display_text(&dev, 7, "----->DONE<----", 16, false);
    }
    else
    {
        ssd1306_display_text(&dev, 4, "---No Target---", 16, false);
        ESP_LOGI(TAG, "TARGET not found");
    }

    ESP_LOGI(OTA_TAG, "app simple_fota start");
    // Initialize NVS.

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_ERROR_CHECK(example_wifi_connect());

    // OTAdrive initialize
    esp_event_handler_register(OTADRIVE_EVENTS, ESP_EVENT_ANY_ID, &otadrive_event_handler, NULL); // Register a handler to get updates on progress
    otadrive_setInfo(OTADRIVE_APIKEY, APP_VERSION);

    xTaskCreate(&otadrive_thread, "otadrive_example_task", 1024 * 16, NULL, 5, NULL);
}

// ========================== Wifi section
bool example_is_our_netif(const char *prefix, esp_netif_t *netif)
{
    return strncmp(prefix, esp_netif_get_desc(netif), strlen(prefix) - 1) == 0;
}

static void example_handler_on_wifi_disconnect(void *arg, esp_event_base_t event_base,
                                               int32_t event_id, void *event_data)
{
    s_retry_num++;
    if (s_retry_num > 6)
    {
        ESP_LOGI(OTA_TAG, "WiFi Connect failed %d times, stop reconnect.", s_retry_num);
        /* let example_wifi_sta_do_connect() return */
        if (s_semph_get_ip_addrs)
        {
            xSemaphoreGive(s_semph_get_ip_addrs);
        }
        return;
    }
    ESP_LOGI(OTA_TAG, "Wi-Fi disconnected, trying to reconnect...");
    esp_err_t err = esp_wifi_connect();
    if (err == ESP_ERR_WIFI_NOT_STARTED)
    {
        return;
    }
    ESP_ERROR_CHECK(err);
}

static void example_handler_on_sta_got_ip(void *arg, esp_event_base_t event_base,
                                          int32_t event_id, void *event_data)
{
    s_retry_num = 0;
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    if (!example_is_our_netif(EXAMPLE_NETIF_DESC_STA, event->esp_netif))
    {
        return;
    }
    ESP_LOGI(OTA_TAG, "Got IPv4 event: Interface \"%s\" address: " IPSTR, esp_netif_get_desc(event->esp_netif), IP2STR(&event->ip_info.ip));
    if (s_semph_get_ip_addrs)
    {
        xSemaphoreGive(s_semph_get_ip_addrs);
    }
    else
    {
        ESP_LOGI(OTA_TAG, "- IPv4 address: " IPSTR ",", IP2STR(&event->ip_info.ip));
    }
}

void example_wifi_start(void)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_WIFI_STA();
    // Warning: the interface desc is used in tests to capture actual connection details (IP, gw, mask)
    esp_netif_config.if_desc = EXAMPLE_NETIF_DESC_STA;
    esp_netif_config.route_prio = 128;
    s_example_sta_netif = esp_netif_create_wifi(WIFI_IF_STA, &esp_netif_config);
    esp_wifi_set_default_wifi_sta_handlers();

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

esp_err_t example_wifi_sta_do_connect(wifi_config_t wifi_config, bool wait)
{
    if (wait)
    {
        s_semph_get_ip_addrs = xSemaphoreCreateBinary();
        if (s_semph_get_ip_addrs == NULL)
        {
            return ESP_ERR_NO_MEM;
        }
    }
    s_retry_num = 0;
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &example_handler_on_wifi_disconnect, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &example_handler_on_sta_got_ip, NULL));

    ESP_LOGI(OTA_TAG, "Connecting to %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    esp_err_t ret = esp_wifi_connect();
    if (ret != ESP_OK)
    {
        ESP_LOGE(OTA_TAG, "WiFi connect failed! ret:%x", ret);
        return ret;
    }
    if (wait)
    {
        ESP_LOGI(OTA_TAG, "Waiting for IP(s)");

        xSemaphoreTake(s_semph_get_ip_addrs, portMAX_DELAY);

        if (s_retry_num > 6)
        {
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

esp_err_t example_wifi_connect(void)
{
    ESP_LOGI(OTA_TAG, "Start wifi_connect.");
    example_wifi_start();
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    return example_wifi_sta_do_connect(wifi_config, true);
}