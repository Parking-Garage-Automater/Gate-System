#include <inttypes.h> 
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "driver/mcpwm.h"
#include "mqtt_client.h"

/* WiFi configuration */
#define WIFI_SSID "Ze"
#define WIFI_PASS "987654321"
#define WIFI_MAXIMUM_RETRY 10

/* MQTT configuration */
#define MQTT_BROKER_ADDRESS "138.199.217.16"
#define MQTT_BROKER_PORT 1883
#define MQTT_USERNAME "parkers"
#define MQTT_PASSWORD "parkers"
#define MQTT_TOPIC_ENTRY "parking/gate/entry"
#define MQTT_TOPIC_EXIT "parking/gate/exit"

/* Servo configuration */
#define SERVO_MIN_PULSEWIDTH 600     /* Minimum pulse width in microseconds */
#define SERVO_MAX_PULSEWIDTH 2400    /* Maximum pulse width in microseconds */
#define SERVO_MAX_DEGREE 180         /* Maximum angle in degrees */
#define SERVO_ENTRY_GPIO 5           /* GPIO for entry gate servo */
#define SERVO_EXIT_GPIO 18           /* GPIO for exit gate servo */

/* Gate configuration */
#define GATE_OPEN_ANGLE1 90             /* Angle when entry gate is open */
#define GATE_CLOSED_ANGLE_1 180         /* Angle for entry servo motor when gate is open */
#define GATE_OPEN_ANGLE2 90            /* Angle when exit gate is open */
#define GATE_CLOSED_ANGLE_2 180          /* Angle for exit servo motor when gate is closed */
#define GATE_OPEN_TIME_MS 5000          /* Time to keep gate open in milliseconds */

static const char *TAG = "GATE_SYSTEM";
static EventGroupHandle_t wifi_event_group;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;
static esp_mqtt_client_handle_t mqtt_client = NULL;

/* Function to print memory stats */
void print_memory_stats(char *event) {
    uint32_t free_heap = esp_get_free_heap_size();
    uint32_t min_free_heap = esp_get_minimum_free_heap_size();
    
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_DEFAULT);

    printf("MEMLOG,%lu,%s,%lu,%lu,%lu,%lu,%lu\n", 
        (unsigned long)esp_log_timestamp(),
        event,
        (unsigned long)free_heap, 
        (unsigned long)min_free_heap, 
        (unsigned long)info.total_allocated_bytes,
        (unsigned long)info.total_free_bytes,
        (unsigned long)info.largest_free_block);

    ESP_LOGI(TAG, "--- MEMORY STATS for event: %s ---", event);
    ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
    ESP_LOGI(TAG, "Minimum free heap ever: %lu bytes", (unsigned long)esp_get_minimum_free_heap_size());
    ESP_LOGI(TAG, "Total allocated: %lu bytes", (unsigned long)info.total_allocated_bytes);
    ESP_LOGI(TAG, "Total free: %lu bytes", (unsigned long)info.total_free_bytes);
    ESP_LOGI(TAG, "Largest free block: %lu bytes", (unsigned long)info.largest_free_block);
}

/* Function to set servo angle */
static void set_servo_angle(mcpwm_unit_t unit, mcpwm_timer_t timer, uint32_t gpio_num, uint32_t angle)
{
    uint32_t pulse_width_us = (SERVO_MIN_PULSEWIDTH + (((SERVO_MAX_PULSEWIDTH - SERVO_MIN_PULSEWIDTH) * angle) / SERVO_MAX_DEGREE));
    mcpwm_set_duty_in_us(unit, timer, MCPWM_OPR_A, pulse_width_us);
}

/* Function to close entry gate after delay */
static void close_entry_gate_task(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(GATE_OPEN_TIME_MS));
    ESP_LOGI(TAG, "[ACTION] Closing ENTRY gate...");
    set_servo_angle(MCPWM_UNIT_0, MCPWM_TIMER_0, SERVO_ENTRY_GPIO, GATE_CLOSED_ANGLE_1);
    print_memory_stats("After close entry gate");
    vTaskDelete(NULL);
}

/* Function to open entry gate */
static void open_entry_gate(void)
{
    print_memory_stats("Before open entry gate");
    
    ESP_LOGI(TAG, "[ACTION] Opening ENTRY gate...");
    set_servo_angle(MCPWM_UNIT_0, MCPWM_TIMER_0, SERVO_ENTRY_GPIO, GATE_OPEN_ANGLE1);
    
    xTaskCreate(
        (TaskFunction_t)&close_entry_gate_task,
        "close_entry_gate_task",
        2048,
        NULL,
        5,
        NULL
    );
}

/* Function to close exit gate after delay */
static void close_exit_gate_task(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(GATE_OPEN_TIME_MS));
    ESP_LOGI(TAG, "[ACTION] Closing EXIT gate...");
    set_servo_angle(MCPWM_UNIT_0, MCPWM_TIMER_1, SERVO_EXIT_GPIO, GATE_CLOSED_ANGLE_2);
    print_memory_stats("After close exit gate");
    vTaskDelete(NULL);
}

/* Function to open exit gate */
static void open_exit_gate(void)
{
    print_memory_stats("After open exit gate");
    
    ESP_LOGI(TAG, "[ACTION] Opening EXIT gate...");
    set_servo_angle(MCPWM_UNIT_0, MCPWM_TIMER_1, SERVO_EXIT_GPIO, GATE_OPEN_ANGLE2);

    xTaskCreate(
        (TaskFunction_t)&close_exit_gate_task,
        "close_exit_gate_task",
        2048,
        NULL,
        5,
        NULL
    );
}

/* MQTT event handler */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "--- MQTT connected ---");
            esp_mqtt_client_subscribe(mqtt_client, MQTT_TOPIC_ENTRY, 0);
            esp_mqtt_client_subscribe(mqtt_client, MQTT_TOPIC_EXIT, 0);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "--- MQTT disconnected ---");
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "--- MQTT subscribed to topic ---");
            break;

        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "--- MQTT unsubscribed from topic ---");
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "--- MQTT data received ---");

            ESP_LOGI(TAG, "Topic: %.*s", event->topic_len, event->topic);
            ESP_LOGI(TAG, "Data: %.*s", event->data_len, event->data);

            if (strncmp(event->topic, MQTT_TOPIC_ENTRY, event->topic_len) == 0) {
                /* Check message content - expecting "open" */
                if (strncmp(event->data, "open", event->data_len) == 0) {
                    open_entry_gate();
                }
            } else if (strncmp(event->topic, MQTT_TOPIC_EXIT, event->topic_len) == 0) {
                /* Check message content - expecting "open" */
                if (strncmp(event->data, "open", event->data_len) == 0) {
                    open_exit_gate();
                }
            }
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "--- MQTT error ---");
            break;

        default:
            ESP_LOGI(TAG, "--- Other MQTT event %d---", event->event_id);
            break;
    }
}

/* Function to initialise WiFi event handler */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
    int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "[RETRY] Connecting to WiFi...");
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "[ERROR] Failed to connect to WiFi.");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "[INFO] Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// Initialize WiFi
static void wifi_init(void)
{
    print_memory_stats("Before WiFi init");

    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "[INIT] WiFi initialization completed!");
    
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                            pdFALSE,
                                            pdFALSE,
                                            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "[INFO] Connected to WiFi SSID: %s", WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "[ERROR] Failed to connect to WiFi SSID: %s", WIFI_SSID);
    } else {
        ESP_LOGE(TAG, "[WARN] Unexpected event.");
    }

    print_memory_stats("After WiFi init");
}

/* Function to initialize servos */
static void servo_init(void)
{
    print_memory_stats("Before Servo init");

    ESP_LOGI(TAG, "[INIT] Initializing servo motors...");

    mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0A, SERVO_ENTRY_GPIO);
    mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM1A, SERVO_EXIT_GPIO);

    mcpwm_config_t pwm_config = {
        .frequency = 50,    /* Set 50Hz frequency = 20ms period */
        .cmpr_a = 0,        /* Set initial duty cycle to 0% */ 
        .cmpr_b = 0,
        .duty_mode = MCPWM_DUTY_MODE_0,
        .counter_mode = MCPWM_UP_COUNTER,
    };

    mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_0, &pwm_config);
    mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_1, &pwm_config);

    /* Move servos to closed position */
    set_servo_angle(MCPWM_UNIT_0, MCPWM_TIMER_0, SERVO_ENTRY_GPIO, GATE_CLOSED_ANGLE_1);
    set_servo_angle(MCPWM_UNIT_0, MCPWM_TIMER_1, SERVO_EXIT_GPIO, GATE_CLOSED_ANGLE_2);

    print_memory_stats("After Servo init");
}

/* Function to initialize MQTT */
static void mqtt_init(void)
{
    print_memory_stats("Before MQTT init");

    ESP_LOGI(TAG, "[INIT] Initializing MQTT client...");

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.hostname = MQTT_BROKER_ADDRESS,
        .broker.address.port = MQTT_BROKER_PORT,
        .broker.address.transport = MQTT_TRANSPORT_OVER_TCP,
        .credentials.username = MQTT_USERNAME,
        .credentials.authentication.password = MQTT_PASSWORD,
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);

    print_memory_stats("After MQTT init");
}

void app_main(void)
{
    ESP_LOGI(TAG, "[INIT] Starting gate system...");

    set_servo_angle(MCPWM_UNIT_0, MCPWM_TIMER_0, SERVO_ENTRY_GPIO, 0);
    set_servo_angle(MCPWM_UNIT_0, MCPWM_TIMER_0, SERVO_EXIT_GPIO, 0);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init();

    servo_init();

    mqtt_init();

    ESP_LOGI(TAG, "[INFO] Gate system READY.");
}