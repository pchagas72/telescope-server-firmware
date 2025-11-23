#include <stdio.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_log.h>
#include <string.h>

#include "esp_err.h"
#include "esp_event_base.h"
#include "portmacro.h"

#include "mqtt_client.h"
#include <esp_crt_bundle.h>
#include <pthread.h>
#include "sdkconfig.h"

#include "led_controls.h"
#include "server_controls.h"

#define MQTT_URL_SCHEME "mqtts"

Server_State state;
pthread_mutex_t led_mutex = PTHREAD_MUTEX_INITIALIZER;

char mqtt_receive_topic[64];
char mqtt_response_topic[64];
char mqtt_broker_address[128];

static void mqtt_event_handler(void *handler_args,
        esp_event_base_t base,
        int32_t event,
        void *event_data);

void app_main(void)
{
    char *taskName = pcTaskGetName(NULL);
    ESP_LOGI(taskName, "Task starting up\n");

    // Read topics and server IP from the config
    snprintf(mqtt_receive_topic, sizeof(mqtt_receive_topic), "servers/%s/command", CONFIG_SERVER_NAME);
    snprintf(mqtt_response_topic, sizeof(mqtt_response_topic), "servers/%s/response", CONFIG_SERVER_NAME);
    snprintf(mqtt_broker_address, sizeof(mqtt_broker_address), "%s://%s:%d",
            MQTT_URL_SCHEME,
            CONFIG_MQTT_IP,
            CONFIG_MQTT_PORT);

    // Start internet connection
    server_connect_internet();

    // Gather info about internet connection
    server_check_connection_internet(&state, &led_mutex);

    esp_mqtt_client_config_t mqtt_cfg = {
            .broker.address.uri = mqtt_broker_address,
            .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
            .credentials.username = CONFIG_MQTT_USER,      // Add Username
            .credentials.authentication.password = CONFIG_MQTT_PASSWORD, // Add Password
    };
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);
    blink_led(3, 500, &led_mutex);

    // Any side processes can be executed here
    while (1)
    {
        if (!state.connected_to_mqtt || !state.connected_to_internet) {
            blink_led(5, 50, &led_mutex);
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

// I like to treat this as the main function
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI("MQTT", "Connected to Broker!");
        state.connected_to_mqtt = true;
        blink_led(3, 100, &led_mutex);

        esp_mqtt_client_subscribe(client, "servers/ALL/command", CONFIG_MQTT_QOS);
        ESP_LOGI("MQTT", "Connected to %s", "servers/ALL/command");

        esp_mqtt_client_subscribe(client, mqtt_receive_topic, CONFIG_MQTT_QOS);
        ESP_LOGI("MQTT", "Connected to %s", mqtt_response_topic);
        break;

    case MQTT_EVENT_DATA:
        if (strncmp(event->data, "sv.ping", event->data_len) == 0) {
            blink_led(1, 25, &led_mutex);
            ESP_LOGI("MQTT", "Received ping from base\n");
            char response_payload[64];
            snprintf(response_payload, sizeof(response_payload), "BRAVO: received ping packet.");
            esp_mqtt_client_publish(client, mqtt_response_topic, response_payload, 0, 1, 0);
            blink_led(1, 25, &led_mutex);
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
