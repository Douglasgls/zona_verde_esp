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
#include "esp_timer.h"
#include "rom/ets_sys.h"
#include <math.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ============================================================
// 1. CONFIGURAÇÕES GERAIS E CONSTANTES
// ============================================================
static const char *TAG = "ZONA_VERDE";

// --- Rede e Servidor ---
#define WIFI_SSID       "DOUGLAS_VLINK FIBRA"
#define WIFI_PASSWORD   "06191005c"
#define SERVER_IP       "192.168.0.181"
#define ID_DEVICE      "01"
#define STATUS_DEVICE_OCUPADO  "OCUPADO"

#define UPLOAD_URL      "http://" SERVER_IP ":8000/api/plate/validate"
#define MQTT_BROKER_URI "mqtt://" SERVER_IP ":1883"
#define MQTT_TOPIC      "camera/" ID_DEVICE

// --- Pinos do HC-SR04 ---
#define TRIGGER_PIN 15
#define ECHO_PIN 13

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

float distancia_max_interesse_cm = 25.0f;

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
            if (event->data_len == strlen("picture") && strncmp(event->data, "picture", event->data_len) == 0) {
                ESP_LOGI(TAG, "Comando RECEBIDO...");
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
        
        .xclk_freq_hz = 10000000,       // REDUZIDO para 10MHz (Elimina as listras)
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG,
        
        .frame_size = FRAMESIZE_SVGA,   // 800x600
        .jpeg_quality = 8,              
        .fb_count = 2,
        .grab_mode = CAMERA_GRAB_LATEST,
    };

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) return err;

    sensor_t *s = esp_camera_sensor_get();
    s->set_contrast(s, 2);       // Aumenta contraste (letras mais pretas)
    s->set_saturation(s, -2);    // Diminui cor (OCR prefere tons de cinza)
    s->set_sharpness(s, 2);      // Melhora nitidez das bordas
    
    return ESP_OK;
}

// ============================================================
// 5. MÓDULO HTTP (UPLOAD)
// ============================================================
esp_err_t upload_photo_http(camera_fb_t* pic, const char* status_msg)
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
        "Content-Disposition: form-data; name=\"id\"\r\n\r\n%s\r\n"
        "--ESP32\r\n"
        "Content-Disposition: form-data; name=\"status\"\r\n\r\n%s\r\n" // <--- Aqui entra o status dinâmico
        "--ESP32\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"capture.jpg\"\r\n"
        "Content-Type: image/jpeg\r\n\r\n",
        ID_DEVICE,
        status_msg 
    );
    
    char tail_body[] = "\r\n--ESP32--\r\n";

    size_t content_len = strlen(head_body) + pic->len + strlen(tail_body);
    
    esp_err_t err = esp_http_client_open(client, content_len);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Falha na conexão HTTP: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    esp_http_client_write(client, head_body, strlen(head_body));
    esp_http_client_write(client, (const char*)pic->buf, pic->len);
    esp_http_client_write(client, tail_body, strlen(tail_body));

    int status_code = esp_http_client_get_status_code(client);
    
    // Log para confirmar qual status foi enviado
    ESP_LOGI(TAG, "Upload enviado com status: %s | Código HTTP: %d", status_msg, status_code);

    esp_http_client_cleanup(client);
    return (status_code >= 200 && status_code < 300) ? ESP_OK : ESP_FAIL;
}

// ============================================================
// 6. MÓDULO ULTRASSONICO
// ============================================================

void pulsarTrigger(void)
{
    gpio_set_level(TRIGGER_PIN, 0);
    esp_rom_delay_us(2);
    gpio_set_level(TRIGGER_PIN, 1);
    esp_rom_delay_us(10);
    gpio_set_level(TRIGGER_PIN, 0);
}

float calcularDistancia(void)
{
    pulsarTrigger();

    int64_t t0 = esp_timer_get_time();
    while (gpio_get_level(ECHO_PIN) == 0) {
        if ((esp_timer_get_time() - t0) > 25000) return -1.0f;
    }

    int64_t inicio = esp_timer_get_time();
    while (gpio_get_level(ECHO_PIN) == 1) {
        if ((esp_timer_get_time() - inicio) > 25000) return -1.0f;
    }

    int64_t fim = esp_timer_get_time();
    return (float)(fim - inicio) * 0.0343f / 2.0f;
}



camera_fb_t* tirarFoto(void)
{
    for(int i = 0; i < 4; i++) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb) esp_camera_fb_return(fb);
        vTaskDelay(pdMS_TO_TICKS(150));
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Falha ao capturar frame");
        return NULL;
    }

    // LOG DE DIAGNÓSTICO: Se o tamanho for baixo, a PSRAM não está ativa
    ESP_LOGW(TAG, "FOTO CAPTURADA: %zu bytes", fb->len);
    
    return fb;
}


void capturar_e_enviar(const char *status)
{
    camera_fb_t *fb = tirarFoto();
    if (!fb) {
        ESP_LOGE(TAG, "Erro ao capturar foto");
        return;
    }

    upload_photo_http(fb, status);
    esp_camera_fb_return(fb);
}


void task_principal(void *pvParameters)
{
    bool vaga_ocupada_atualmente = false;
    int leituras_confirmacao = 0;
    const int leituras_minimas = 3;

    while (1) {

        // ===== COMANDO MQTT =====
        if (ulTaskNotifyTake(pdTRUE, 0) > 0) {
            ESP_LOGW(TAG, "Comando MQTT recebido! Tirando foto manual...");
            capturar_e_enviar("MANUAL");
        }

        // ===== SENSOR ULTRASSÔNICO =====
        float distancia = calcularDistancia();

        if (distancia > 0 && distancia < 400) {

            if (distancia <= distancia_max_interesse_cm) {
                if (!vaga_ocupada_atualmente) {
                    leituras_confirmacao++;
                    if (leituras_confirmacao >= leituras_minimas) {
                        vaga_ocupada_atualmente = true;
                        leituras_confirmacao = 0;

                        ESP_LOGW(TAG, "ESTADO: OCUPADO");
                        capturar_e_enviar("OCUPADO");
                    }
                } else {
                    leituras_confirmacao = 0;
                }
            } 
            else {
                if (vaga_ocupada_atualmente) {
                    leituras_confirmacao++;
                    if (leituras_confirmacao >= leituras_minimas) {
                        vaga_ocupada_atualmente = false;
                        leituras_confirmacao = 0;

                        ESP_LOGI(TAG, "ESTADO: LIVRE");
                        capturar_e_enviar("LIVRE");
                    }
                } else {
                    leituras_confirmacao = 0;
                }
            }

            ESP_LOGI(TAG, "Distância: %.2f cm | Estado: %s",
                     distancia,
                     vaga_ocupada_atualmente ? "OCUPADO" : "LIVRE");
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
// ============================================================
// 7. MAIN (LÓGICA PRINCIPAL)
// ============================================================
void app_main(void)
{
    s_main_task_handle = NULL;

    nvs_flash_init();

    // GPIO Ultrassônico
    gpio_reset_pin(TRIGGER_PIN);
    gpio_reset_pin(ECHO_PIN);
    gpio_set_direction(TRIGGER_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(ECHO_PIN, GPIO_MODE_INPUT);

    // Inicializações
    ESP_ERROR_CHECK(setup_camera());
    vTaskDelay(pdMS_TO_TICKS(2000));

    setup_wifi();
    setup_mqtt();

    // ===== CRIA TASK PRINCIPAL =====
    xTaskCreate(
        task_principal,        // Função da task
        "task_principal",      // Nome
        8192,                  // Stack (ESP32-CAM precisa)
        NULL,                  // Parâmetros
        5,                     // Prioridade
        &s_main_task_handle    // Handle (para MQTT notificar)
    );

    ESP_LOGI(TAG, "Sistema inicializado com task principal");
}