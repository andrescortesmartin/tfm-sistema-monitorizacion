# Ancla LoRa: heartbeat LoRaWAN + subordinado de ranging

## Descripción

Firmware para las anclas fijas del sistema de posicionamiento por ranging LoRa. Está pensado para un **Wio-WM1110 Dev Kit** (Seeed Studio) y cumple dos funciones a la vez sobre la única radio del módulo:

- **Heartbeat LoRaWAN periódico.** El nodo hace join OTAA y envía un uplink cada cierto tiempo (60 s por defecto) con su estado: contadores de ranging, RSSI/SNR del último intercambio, temperatura y humedad, y la configuración vigente.
- **Escucha continua de peticiones de ranging (RTToF).** Cuando el modem LoRaWAN deja tiempo libre, se suspende, la radio LR1110 se reconfigura en modo RTToF y se queda en recepción respondiendo a las solicitudes de medida de distancia que le llegan del nodo móvil. Antes de que el modem vuelva a necesitar la radio, se devuelve el control.

En el esquema de ranging el ancla tiene un **rol subordinado**: no inicia nada, solo responde. Los nodos sensores (manager) son quienes lanzan cada intercambio RTToF contra la dirección de cada ancla. Este firmware se limita a estar disponible para responder y a reportar por LoRaWAN cuántos intercambios ha atendido.

El detalle de funcionamiento (bucle del modem, formato de los mensajes y comandos de gestión) está en la documentación indicada en [Referencias](#referencias).

## Hardware

Placa: **Wio-WM1110 Dev Kit** (Seeed Studio).

| Componente | Modelo | Función | Interfaz |
|---|---|---|---|
| **MCU** | Nordic nRF52840 | Procesador de aplicación (Cortex-M4F) + BLE | - |
| **Transceptor** | Semtech LR1110 | Radio LoRa, ranging RTToF (y capacidad GNSS/Wi-Fi scan) | SPI |
| **Sensor T/H** | Sensirion SHT41 | Temperatura y humedad | I2C |

## Estructura del proyecto

```
ANCLAS LORA/
├── aplication/
│   └── pca10056/s140/
│       ├── config_sd/sdk_config.h        # configuración Nordic SDK
│       └── ses/WM1110_pca10056_s140.emProject
│
├── components/
│   ├── wm1110/
│   │   ├── lora/                         # main_lorawan.c (aplicación), apps_configuration.h
│   │   ├── sht41/                        # driver SHT41
│   │   ├── flash/                        # persistencia de configuración (FDS)
│   │
│   ├── lora_basics_modem/                # stack LoRaWAN de Semtech
│   ├── smtc_hal/                         # HAL Semtech sobre nRF52840
│
└── config/
```

## Compilar y flashear

1. Abrir en Segger Embedded Studio: `aplication/pca10056/s140/ses/WM1110_pca10056_s140.emProject`
2. Build and Run. 

Requisitos en el equipo: Segger Embedded Studio for ARM y el software de J-Link.

## Configurar LoRaWAN (primera vez)

Editar `components/apps/common/lorawan_key_config.h`:

- `LORAWAN_DEVICE_EUI`, `LORAWAN_JOIN_EUI`, `LORAWAN_APP_KEY`
- Credenciales desde la consola de tu servidor LoRaWAN (p. ej. The Things Network: https://console.thethingsnetwork.org/)

## Referencias

| Sección | Memoria | Anexo |
|---|---|---|
| Diseño y arquitectura del sistema | Capítulo 4.1 | Anexo IV |

## Fuentes

- [Wio-WM1110 Dev Kit — Seeed Studio Wiki](https://wiki.seeedstudio.com/Wio-WM1110_Dev_Kit/Introduction/)
- [Wio-WM1110 Dev Kit Hardware Overview](https://wiki.seeedstudio.com/Wio-WM1110_Dev_Kit_Hardware_Overview/)
