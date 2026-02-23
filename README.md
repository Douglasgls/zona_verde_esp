# Zona Verde ESP32-CAM

Projeto embarcado (ESP-IDF) para monitoramento de vaga com **ESP32-CAM + sensor ultrassônico HC-SR04**.
O firmware mede a distância periodicamente, detecta transição de estado da vaga (**LIVRE/OCUPADO**) e envia uma foto para uma API HTTP. Também aceita comando via MQTT para captura manual.

> Este README foi reestruturado com base no comportamento implementado em `main/main.c`.

---

## Visão geral da solução

O dispositivo executa os seguintes blocos:

- **Wi-Fi (modo STA):** conecta na rede e mantém reconexão automática.
- **MQTT:** assina um tópico por dispositivo e recebe comando `picture` para foto manual.
- **Câmera (esp32-camera):** captura JPEG com ajustes para melhorar OCR (contraste/nitidez/saturação).
- **Ultrassônico:** mede distância com timeout para evitar travamento de leitura.
- **HTTP multipart/form-data:** envia `id`, `status` e `file` (`capture.jpg`) para validação/registro.

---

## Fluxo funcional

1. Inicializa NVS, GPIO do HC-SR04, câmera, Wi-Fi e MQTT.
2. Cria a `task_principal` (loop a cada ~5s).
3. No loop:
   - Se chegar MQTT com payload `picture`, dispara captura com status `MANUAL`.
   - Lê distância do HC-SR04.
   - Faz histerese por confirmação (`leituras_minimas = 3`) para evitar falso positivo.
   - Quando o estado muda:
     - `OCUPADO` → captura e envia foto com status `OCUPADO`.
     - `LIVRE` → captura e envia foto com status `LIVRE`.

---

## Dependências

- ESP-IDF (projeto em C com FreeRTOS/Event Loop/MQTT/HTTP Client).
- Driver `esp32-camera` (componente externo no projeto).

### Instalação do `esp32-camera`

No diretório raiz do projeto:

```bash
mkdir -p components
git clone https://github.com/espressif/esp32-camera components/esp32-camera
```

No `CMakeLists.txt` raiz, garantir:

```cmake
set(EXTRA_COMPONENT_DIRS components/esp32-camera)
```

Se o componente não estiver presente, o build falha por ausência de `esp_camera.h` e implementações do sensor.

---

## Configuração (hardcoded no firmware)

Os parâmetros abaixo estão definidos em `main/main.c`:

- Wi-Fi:
  - `WIFI_SSID`
  - `WIFI_PASSWORD`
- Backend:
  - `SERVER_IP`
  - `UPLOAD_URL` (`http://<SERVER_IP>:8000/api/plate/validate`)
- MQTT:
  - `MQTT_BROKER_URI` (`mqtt://<SERVER_IP>:1883`)
  - `MQTT_TOPIC` (`camera/<ID_DEVICE>`)
- Dispositivo:
  - `ID_DEVICE`
- Detecção por distância:
  - `distancia_max_interesse_cm` (default: `25.0 cm`)

> Recomendado: mover essas credenciais/configurações para `menuconfig`, NVS ou variáveis de provisionamento.

---

## Pinagem utilizada

### HC-SR04

- `TRIGGER_PIN = GPIO15`
- `ECHO_PIN = GPIO13`

### ESP32-CAM (AI Thinker)

- `PWDN=32`, `RESET=-1`, `XCLK=0`, `SIOD=26`, `SIOC=27`
- `D7=35`, `D6=34`, `D5=39`, `D4=36`, `D3=21`, `D2=19`, `D1=18`, `D0=5`
- `VSYNC=25`, `HREF=23`, `PCLK=22`

---

## Configuração da câmera

Parâmetros relevantes aplicados no firmware:

- `pixel_format = JPEG`
- `frame_size = FRAMESIZE_SVGA (800x600)`
- `jpeg_quality = 8`
- `fb_count = 2`
- `grab_mode = CAMERA_GRAB_LATEST`
- `xclk_freq_hz = 10 MHz`

Ajustes de sensor após init:

- contraste: `+2`
- saturação: `-2`
- sharpness: `+2`

Objetivo dos ajustes: melhorar legibilidade de caracteres para OCR de placa.

---

## Protocolo de integração

### MQTT (entrada)

- **Tópico:** `camera/<ID_DEVICE>`
- **Comando aceito:** payload exato `picture`
- **Ação:** captura e upload com status `MANUAL`

### HTTP (saída)

- **Endpoint:** `POST /api/plate/validate`
- **Content-Type:** `multipart/form-data; boundary=ESP32`
- **Campos enviados:**
  - `id` (ex.: `01`)
  - `status` (`MANUAL`, `OCUPADO` ou `LIVRE`)
  - `file` (`capture.jpg`, JPEG da câmera)

Upload é considerado sucesso para status HTTP `2xx`.

---

## Lógica de ocupação da vaga

- Leitura válida de distância: `0 < distancia < 400` cm.
- Limiar de interesse: `<= 25 cm` (ocupado) e `> 25 cm` (livre).
- Transição de estado exige **3 leituras consecutivas** (`leituras_minimas = 3`).
- Intervalo entre ciclos: `5 segundos`.

Esse mecanismo reduz oscilações por ruído do sensor ultrassônico.

---

## Build e flash

```bash
idf.py fullclean
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Comando útil para identificar porta serial:

```bash
ls /dev/ttyUSB*
```

---

## Troubleshooting

### 1) Build estoura tamanho de partição

Se faltar espaço de firmware, ajuste a tabela de partição no menuconfig para perfil de app maior (ex.: *Single app, huge app*), então refaça o build.

### 2) Sem foto / frame inválido

- Verificar alimentação estável da ESP32-CAM.
- Confirmar presença de PSRAM e configuração correta da placa.
- Conferir logs de tamanho de frame (`FOTO CAPTURADA: ... bytes`).

### 3) Não conecta no backend/mqtt

- Validar `SERVER_IP`, portas `8000` (HTTP) e `1883` (MQTT).
- Confirmar rota de rede entre ESP32 e servidor.

---

## Melhorias recomendadas

- Remover credenciais hardcoded e usar provisionamento seguro.
- Adicionar TLS em HTTP/MQTT.
- Implementar retentativa/backoff de upload HTTP.
- Publicar telemetria periódica de saúde (RSSI, heap, uptime).
- Adicionar watchdog e métricas de erro por módulo.
