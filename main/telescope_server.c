#include <stdio.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_log.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_event_base.h"
#include "esp_wifi_types_generic.h"
#include "portmacro.h"

#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"

#include "protocol_examples_common.h"
#include "esp_wifi.h"

#include "mqtt_client.h"
#include <pthread.h>

#define BLINK_LED 2
#define MQTT_TOPIC "home/testing"
#define MQTT_QOS 1

typedef struct server_state{
    bool connected_to_internet;
    bool connected_to_mqtt;
} Server_State;

Server_State state;
pthread_mutex_t led_mutex = PTHREAD_MUTEX_INITIALIZER;

static void blink_led(int n, int delay, pthread_mutex_t *led_mutex);
static void mqtt_event_handler(void *handler_args,
        esp_event_base_t base,
        int32_t event,
        void *event_data);

void app_main(void)
{
    char *taskName = pcTaskGetName(NULL);
    ESP_LOGI(taskName, "Task starting up\n");


    // Start internet connection
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());

    // Gather info about internet connection
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK){
        ESP_LOGI("WiFi", "Connected to %s\n", ap_info.ssid);
        state.connected_to_internet = true;
        blink_led(3, 100, &led_mutex);
    }

    esp_mqtt_client_config_t mqtt_cfg = {
            .broker.address.uri = "mqtt://192.168.1.17:1883",
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);

    blink_led(3, 500, &led_mutex);
    while (1)
    {
        if (!state.connected_to_mqtt || !state.connected_to_internet) {
            blink_led(5, 50, &led_mutex);
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

// TODO: ADD MUTEX TO CONTROL THIS FUNCTION
void blink_led(int n, int delay, pthread_mutex_t *led_mutex)
{
    pthread_mutex_lock(led_mutex);
    gpio_reset_pin(BLINK_LED);
    gpio_set_direction(BLINK_LED, GPIO_MODE_OUTPUT);

    for (int i = 0; i < n; i++) {
        gpio_set_level(BLINK_LED, 1);
        vTaskDelay(delay / portTICK_PERIOD_MS);
        gpio_set_level(BLINK_LED, 0);
        vTaskDelay(delay / portTICK_PERIOD_MS);
    }

    vTaskDelay(500 / portTICK_PERIOD_MS);
    pthread_mutex_unlock(led_mutex);
}

// This is a default function that will always be called
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI("MQTT", "Connected to Broker!");
        state.connected_to_mqtt = true;
        blink_led(3, 100, &led_mutex);

        esp_mqtt_client_subscribe(client, MQTT_TOPIC, MQTT_QOS);
        ESP_LOGI("MQTT", "Connected to %s", MQTT_TOPIC);

        esp_mqtt_client_subscribe(client, "servers/ALL/command", MQTT_QOS);
        ESP_LOGI("MQTT", "Connected to %s", "servers/ALL/command");

        esp_mqtt_client_subscribe(client, "servers/BRAVO/command", MQTT_QOS);
        ESP_LOGI("MQTT", "Connected to %s", "servers/BRAVO/command");
        break;

    case MQTT_EVENT_DATA:
        if (strncmp(event->data, "sv.ping", event->data_len) == 0) {
            blink_led(1, 100, &led_mutex);
            ESP_LOGI("MQTT", "Received ping from base\n"); // TODO: ADD BASE_ID TO MSG
            char response_payload[64];
            snprintf(response_payload, sizeof(response_payload), "BRAVO: received ping packet.");
            esp_mqtt_client_publish(client, "home/testing", response_payload, 0, 1, 0);
            blink_led(1, 100, &led_mutex);
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGI("MQTT", "Error occurred");
        state.connected_to_mqtt = false;
        break;
        
    default:
        break;
    }
}
