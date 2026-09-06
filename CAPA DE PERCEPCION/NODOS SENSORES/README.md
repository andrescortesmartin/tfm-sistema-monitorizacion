# Logger BLE/LoRa para monitorización de animales

## Descripción

Firmware del **nodo sensor**, el dispositivo móvil que se coloca en el animal. Combina escaneo BLE, sensores ambientales y de movimiento, posicionamiento GPS y ranging LoRa para situar y caracterizar al animal, envía resúmenes periódicos por LoRaWAN y guarda el histórico en flash. Toda la descarga de datos y la configuración se hacen por BLE.

En el sistema de posicionamiento por ranging LoRa el nodo sensor tiene el **rol de manager**: es quien inicia cada intercambio RTToF contra las anclas fijas y calcula la distancia a partir del tiempo de vuelo. Las anclas solo responden (ver el repositorio *ANCLAS LORA*).

El detalle de funcionamiento (arquitectura del firmware, máquinas de estado, formato de tramas y comandos BLE) está en la documentación indicada en [Referencias](#referencias).

## Funcionalidades

**Funcionamiento periódico:**
- Escaneo de BLE (captura de id, RSSI y datos de sensores)
- Lectura de sensores "lentos" (presión, temperatura, inclinación)
- Posicionamiento GPS y ranging LoRa
- Transmisión de paquete tipo heartbeat vía LoRaWAN
- Almacenamiento de datos en memoria flash

**Monitorización de movimiento:**
- Detección automática de movimiento mediante IMU de 6 ejes
- Dos modos complementarios a la detección: muestreo periódico configurable y muestreo continuo
- Almacenaje de datos en memoria flash

**Interfaz BLE:**
- Advertising continuo para conexión en cualquier momento
- Conexión para descarga de datos almacenados
- Conexión para configuración remota de parámetros y gestión del dispositivo

## Hardware

Placa personalizada teniendo como núcleo un chip WM1110 (integra LR1110 + nRF52840), con los siguientes componentes:

| Componentes          | Modelo                 | Función                           | Interfaz |
|----------------------|------------------------|-----------------------------------|----------|
| **MCU**              | nRF52840               | Procesamiento + BLE               | -        |
| **Modem LoRa**       | LR1110                 | Comunicación LoRaWAN + ranging + GNSS | SPI  |
| **IMU**              | LSM6DSOX               | Acelerómetro/giroscopio de 6 ejes | SPI      |
| **Sensor ambiental** | LPS22DF                | Presión + temperatura             | I2C      |
| **Flash externa**    | MX25R6435F             | 64 Mb de memoria                  | QSPI     |
| **Módulo GPS**       | FlyFishRC M10          | Posicionamiento                   | UART     |

### Características adicionales
- Reset magnético
- LED RGB de estado
- Control de alimentación GPS independiente

## Estructura del proyecto

```
NODOS SENSORES/
├── aplication/             # Aplicación principal
│   ├── main.c              # Punto de entrada del firmware
│   └── pca10056/           # Configuración SES + makefiles
│
│
├── components/             # Componentes y bibliotecas
│   ├── wm1110/             # Módulos específicos WM1110
│   │   ├── ble_service/    # BLE (NUS, advertising, scan)
│   │   ├── lora/           # LoRaWAN y ranging (config, main_lora, main_ranging)
│   │   ├── GPS/            # GPS externo UART
│   │   ├── Bateria/        # Monitorización batería
│   │   ├── flash/          # Flash storage (FDS)
│   │   └── main_configuracion.c
│   │
│   ├── LSM6DSOX/           # Driver IMU 6 ejes
│   ├── LPS22DF/            # Driver sensor ambiental
│   │
│   ├── flash/              # Memoria flash de datos
│   │
│   ├── smtc_hal/           # HAL Semtech (BSP nRF52840)
│   ├── lora_basics_modem/  # Stack LoRaWAN
│   │
│   ├── ble/                # Nordic BLE stack
│   ├── libraries/          # Librerías Nordic SDK
│   └── external/           # Dependencias externas
│
└── config/                 # Configuración del proyecto
    └── nrf52840/
        └── sdk_config.h    # Configuración Nordic SDK
```

## Quick Start

### Compilar y flashear

1. Abrir el proyecto en Segger Embedded Studio: `aplication/pca10056/s140/ses/WM1110_pca10056_s140.emProject` (o el workspace `aplication/WM1110.eww`)
2. Build and Run

Requisitos en el equipo: Segger Embedded Studio for ARM y el software de J-Link.

### Configurar LoRaWAN (obligatorio primera vez)

Editar `components/apps/common/lorawan_key_config.h`:
- Configurar `LORAWAN_DEVICE_EUI`, `LORAWAN_JOIN_EUI`, `LORAWAN_APP_KEY`
- Credenciales desde la consola de tu servidor LoRaWAN (p. ej. The Things Network: https://console.thethingsnetwork.org/)

## Referencias

| Sección | Memoria | Anexo |
|---|---|---|
| Desarrollo del firmware | Capítulo 4.2 | Anexo V |
