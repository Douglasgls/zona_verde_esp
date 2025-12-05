#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "esp_camera.h"
#include "mqtt_client.h"

// ============================================================
// 1. CONFIGURAÇÕES GERAIS E CONSTANTES
// ============================================================
static const char *TAG = "ZONA_VERDE";

// --- Rede e Servidor ---
#define WIFI_SSID       "DOUGLAS_VLINK FIBRA"
#define WIFI_PASSWORD   "06191005c"
#define SERVER_IP       "192.168.0.158"
#define ID_DEVICE      "01"
#define STATUS_DEVICE_OCUPADO  "OCUPADO"

#define UPLOAD_URL      "http://" SERVER_IP ":8000/api/plate/validate"
#define MQTT_BROKER_URI "mqtt://" SERVER_IP ":1883"
#define MQTT_TOPIC      "tirarfoto/acao/" ID_DEVICE

// --- Pinos da Câmera (AI THINKER / ESP32-CAM) ---
#define CAM_PIN_PWDN    32
#define CAM_PIN_RESET   -1
#define CAM_PIN_XCLK    0
#define CAM_PIN_SIOD    26
#define CAM_PIN_SIOC    27
#define CAM_PIN_D7      35
#define CAM_PIN_D6      34
#define CAM_PIN_D5      39
#define CAM_PIN_D4      36
#define CAM_PIN_D3      21
#define CAM_PIN_D2      19
#define CAM_PIN_D1      18
#define CAM_PIN_D0      5
#define CAM_PIN_VSYNC   25
#define CAM_PIN_HREF    23
#define CAM_PIN_PCLK    22


static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
static TaskHandle_t s_main_task_handle = NULL;

// ============================================================
// 2. MÓDULO WI-FI
// ============================================================
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi caiu. Tentando reconectar...");
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Conectado! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void setup_wifi(void)
{
    s_wifi_event_group = xEventGroupCreate();
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    esp_wifi_start();

    ESP_LOGI(TAG, "Aguardando Wi-Fi...");
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
}

// ============================================================
// 3. MÓDULO MQTT
// ============================================================
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT Conectado! Inscrevendo no topico: %s", MQTT_TOPIC);
            esp_mqtt_client_subscribe(event->client, MQTT_TOPIC, 0);
            break;
            
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "Mensagem MQTT recebida");
            if (strncmp(event->data, "tirarfoto", event->data_len) == 0) {
                ESP_LOGI(TAG, "Comando RECEBIDO. Acordando tarefa principal...");
                if (s_main_task_handle != NULL) {
                    xTaskNotifyGive(s_main_task_handle);
                }
            }
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT Desconectado");
            break;
            
        default:
            break;
    }
}

void setup_mqtt(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
    };
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);
}

// ============================================================
// 4. MÓDULO CÂMERA
// ============================================================
esp_err_t setup_camera(void)
{
    camera_config_t config = {
        .pin_pwdn = CAM_PIN_PWDN, .pin_reset = CAM_PIN_RESET,
        .pin_xclk = CAM_PIN_XCLK, .pin_sccb_sda = CAM_PIN_SIOD, .pin_sccb_scl = CAM_PIN_SIOC,
        .pin_d7 = CAM_PIN_D7, .pin_d6 = CAM_PIN_D6, .pin_d5 = CAM_PIN_D5,
        .pin_d4 = CAM_PIN_D4, .pin_d3 = CAM_PIN_D3, .pin_d2 = CAM_PIN_D2,
        .pin_d1 = CAM_PIN_D1, .pin_d0 = CAM_PIN_D0,
        .pin_vsync = CAM_PIN_VSYNC, .pin_href = CAM_PIN_HREF, .pin_pclk = CAM_PIN_PCLK,
        
        .xclk_freq_hz = 20000000,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG,
        
        .frame_size = FRAMESIZE_VGA, // 640x480
        .jpeg_quality = 12,          // 0-63
        .fb_count = 2,
        .grab_mode = CAMERA_GRAB_LATEST,
    };

    return esp_camera_init(&config);
}

// ============================================================
// 5. MÓDULO HTTP (UPLOAD)
// ============================================================
esp_err_t upload_photo_http(camera_fb_t* pic)
{
    esp_http_client_config_t config = {
        .url = UPLOAD_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 30000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_header(client, "Content-Type", "multipart/form-data; boundary=ESP32");

    char head_body[512];
    snprintf(head_body, sizeof(head_body),
        "--ESP32\r\n"
        "Content-Disposition: form-data; name=\"id\"\r\n\r\n01\r\n"
        "--ESP32\r\n"
        "Content-Disposition: form-data; name=\"status\"\r\n\r\nOCUPADO\r\n"
        "--ESP32\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"capture.jpg\"\r\n"
        "Content-Type: image/jpeg\r\n\r\n"
    );
    char tail_body[] = "\r\n--ESP32--\r\n";

    size_t content_len = strlen(head_body) + pic->len + strlen(tail_body);
    
    esp_err_t err = esp_http_client_open(client, content_len);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Falha ao abrir conexão HTTP: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    esp_http_client_write(client, head_body, strlen(head_body));
    esp_http_client_write(client, (const char*)pic->buf, pic->len);
    esp_http_client_write(client, tail_body, strlen(tail_body));

    int content_length = esp_http_client_fetch_headers(client);

    if (content_length < 0) {
        ESP_LOGI(TAG, "ERRO: Servidor não respondeu corretamente ou timeout excedido");
    } else {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Upload Finalizado. Status Code: %d", status_code);   
    }

    int final_status = esp_http_client_get_status_code(client);
    
    esp_http_client_cleanup(client);

    return (final_status >= 200 && final_status < 300) ? ESP_OK : ESP_FAIL;
}

// ============================================================
// 6. MAIN (LÓGICA PRINCIPAL)
// ============================================================
void app_main()
{
    nvs_flash_init();
    s_main_task_handle = xTaskGetCurrentTaskHandle();

    setup_wifi();  
    setup_mqtt();
    
    if (setup_camera() != ESP_OK) {
        ESP_LOGE(TAG, "Erro crítico: Câmera não inicializou.");
        return;
    }

    ESP_LOGI(TAG, ">>> SISTEMA INICIADO COM SUCESSO <<<");

    while (1)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        ESP_LOGI(TAG, "--- INICIANDO PROCESSO DE VALIDAÇÃO ---");

        camera_fb_t* pic = esp_camera_fb_get();
        if (!pic) {
            ESP_LOGE(TAG, "Falha na captura da câmera!");
            continue;
        }

        vTaskDelay(5000 / portTICK_PERIOD_MS);

        ESP_LOGI(TAG, "Foto capturada: %u bytes", pic->len);

        if (upload_photo_http(pic) == ESP_OK) {
            ESP_LOGI(TAG, "Sucesso: Fluxo completo.");
        } else {
            ESP_LOGE(TAG, "Erro: Falha no envio.");
        }

        esp_camera_fb_return(pic);
        ESP_LOGI(TAG, "--- FIM DO PROCESSO. AGUARDANDO... ---");
    }
}