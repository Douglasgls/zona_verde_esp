# ESP32-CAM – Integração do Driver esp32-camera no ESP-IDF

Este projeto utiliza o driver esp32-camera, necessário para capturar fotos usando módulos como ESP32-CAM (AI Thinker) e outras placas compatíveis com sensores OV2640 / OV3660 / OV5640.

A biblioteca não faz parte do ESP-IDF 5.x, por isso deve ser adicionada manualmente ao projeto ou instalada como dependência externa.

Este README explica as opções, como funciona a estrutura e por que o build depende da pasta components/esp32-camera.


## Instalando a Biblioteca
Antes de realizar o build do projeto necessita-se instalar a biblioteca esp32-camera no projeto.


o driver não é registrado como componente do ESP-IDF.

1. Adicione a pasta components ao projeto

2. Clone o repositorio https://github.com/espressif/esp32-camera

3. Inclua o driver no seu projeto utilizando o CMake

```cmake
set(EXTRA_COMPONENT_DIRS components/esp32-camera)
```

AVISO: Se essa pasta não existir → o build falha, porque:
o arquivo esp_camera.h não é encontrado;
as implementações internas de OV2640/OV3660 não são encontradas;

4. Copie o driver para a pasta components/esp32-camera

Agora pode compilar o projeto.

```bash
idf.py fullclean
idf.py build
```
