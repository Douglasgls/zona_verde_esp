#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include <esp_system.h>
#include <nvs_flash.h>
#include "esp_log.h"
#include "mqtt_client.h"

#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "esp_http_client.h"

#include "esp_camera.h"

static const char *TAG = "MAIN";


// Meu servidor
#define UPLOAD_URL "http://seu-servidor.com/upload"  

// wifi
#define WIFI_SSID      "DOUGLAS_VLINK"
#define WIFI_PASSWORD  "06191005c"



// camera CAMERA_MODEL_AI_THINKER 

#define CAM_PIN_PWDN    32 
#define CAM_PIN_RESET   -1 //software reset will be performed
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
#define CAM_PIN_D0       5
#define CAM_PIN_VSYNC   25
#define CAM_PIN_HREF    23
#define CAM_PIN_PCLK    22

#define CONFIG_XCLK_FREQ 20000000 
#define CONFIG_OV2640_SUPPORT 1
#define CONFIG_OV7725_SUPPORT 1
#define CONFIG_OV3660_SUPPORT 1
#define CONFIG_OV5640_SUPPORT 1


/********************* WIFI *************************/
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi desconectado, tentando reconectar...");
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "WiFi conectado! IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

void wifi_init(void)
{
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
        },
    };

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    esp_wifi_start();
}

/********************* MQTT *************************/
static esp_mqtt_client_handle_t mqtt_client;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch (event->event_id) {

        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT conectado!");

            esp_mqtt_client_subscribe(event->client, "tirarfoto/acao");

            break;
            
        // HABILITAR SOMENTE QUANDO REALIZAR O RESTO DOS TESTES
        // case MQTT_EVENT_DATA:
        //     ESP_LOGI(TAG, "MQTT DATA RECEBIDA!");
        //     printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        //     printf("DATA=%.*s\r\n", event->data_len, event->data);

        //     if (strncmp(event->data, "tirarfoto", event->data_len) == 0) {

        //         ESP_LOGI(TAG, "Comando para tirar foto recebido!");

        //         camera_fb_t* pic = take_photo();
        //         if (!pic) {
        //             ESP_LOGE(TAG, "Erro ao capturar foto!");
        //             return;
        //         }

        //         esp_err_t error_http = send_photo(pic);

        //         esp_camera_fb_return(pic);

        //         if (error_http == ESP_OK) {
        //             ESP_LOGI(TAG, "FOTO ENVIADA COM SUCESSO!");
        //         } else {
        //             ESP_LOGE(TAG, "ERRO AO ENVIAR FOTO!");
        //         }
        //     }
        //     break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT desconectado!");
            mqtt_start();
            break;

        default:
            break;
    }
}

void mqtt_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://192.168.0.158:1883",
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, mqtt_client);
    esp_mqtt_client_start(mqtt_client);
}

/********************* CAMERA *************************/
static esp_err_t init_camera(void)
{
    camera_config_t camera_config = {
        .pin_pwdn  = CAM_PIN_PWDN,
        .pin_reset = CAM_PIN_RESET,
        .pin_xclk = CAM_PIN_XCLK,
        .pin_sccb_sda = CAM_PIN_SIOD,
        .pin_sccb_scl = CAM_PIN_SIOC,

        .pin_d7 = CAM_PIN_D7,
        .pin_d6 = CAM_PIN_D6,
        .pin_d5 = CAM_PIN_D5,
        .pin_d4 = CAM_PIN_D4,
        .pin_d3 = CAM_PIN_D3,
        .pin_d2 = CAM_PIN_D2,
        .pin_d1 = CAM_PIN_D1,
        .pin_d0 = CAM_PIN_D0,
        .pin_vsync = CAM_PIN_VSYNC,
        .pin_href = CAM_PIN_HREF,
        .pin_pclk = CAM_PIN_PCLK,

        .xclk_freq_hz = CONFIG_XCLK_FREQ,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,

        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = FRAMESIZE_VGA,

        .jpeg_quality = 10,
        .fb_count = 1,
        .grab_mode = CAMERA_GRAB_WHEN_EMPTY};//CAMERA_GRAB_LATEST. Sets when buffers should be filled
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK)
    {
        return err;
    }
    return ESP_OK;
}

camera_fb_t* take_photo()
{
    ESP_LOGI(TAG, "Taking picture...");

    camera_fb_t *pic = esp_camera_fb_get();
    
    ESP_LOGI(TAG, "Picture taken! Its size was: %zu bytes", pic->len);
    
    return pic;
}

/********************* HTTP *************************/
esp_err_t send_photo(camera_fb_t* pic)
{
    esp_http_client_config_t config = {
        .url = UPLOAD_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE("HTTP", "Falha ao inicializar HTTP client");
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "image/jpeg");

    esp_err_t err = esp_http_client_open(client, pic->len);
    if (err != ESP_OK) {
        ESP_LOGE("HTTP", "Erro ao abrir conex�o HTTP: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int written = esp_http_client_write(client, (const char *)pic->buf, pic->len);
    if (written < 0) {
        ESP_LOGE("HTTP", "Erro ao enviar imagem");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    int status = esp_http_client_get_status_code(client);
    ESP_LOGI("HTTP", "Status HTTP recebido: %d", status);

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    return (status == 200 || status == 204) ? ESP_OK : ESP_FAIL;
}

void app_main()
{

    // inicia wifi e mqtt
    nvs_flash_init();
    wifi_init();
    vTaskDelay(3000 / portTICK_PERIOD_MS);
    mqtt_start();

    // inicia camera
    esp_err_t error_cam;
    error_cam = init_camera();
    if (error_cam != ESP_OK)
    {
        printf("error_cam: %s\n", esp_err_to_name(error_cam));
        return;
    }

    // tira foto 
    camera_fb_t* pic = take_photo();

    // envia foto
    esp_err_t error_http = send_photo(pic);
    
    // libera buffer     
    esp_camera_fb_return(pic);

    if (error_http != ESP_OK)
    {
        printf("error_http: %s\n", esp_err_to_name(error_http));
        return;
    }

    ESP_LOGI(TAG, "FOTO ENVIADA");
  
    vTaskDelay(5000 / portTICK_PERIOD_MS);
}