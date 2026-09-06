# Diseño y despliegue de un sistema IoT multicapa para bio-logging y monitorización avanzada de fauna

Sistema IoT de bio-logging para el seguimiento de fauna silvestre: firmware de los dispositivos de campo, decodificación y transporte de los datos, y persistencia/visualización en la capa de aplicación. Este repositorio acompaña la memoria del Trabajo Fin de Máster (TFM) y sus anexos. Cada carpeta referencia el capítulo/anexo correspondiente en su propio README.

## Arquitectura

El sistema se organiza en capas, cada una en su propia carpeta:

| Capa | Contiene | Ver |
|---|---|---|
| Percepción | Firmware de anclas LoRa, nodos sensores y balizas móviles | [`CAPA DE PERCEPCION/`](CAPA%20DE%20PERCEPCION/) |
| Red | Decoders TTN y MicroApp del gateway BLE Cassia | [`CAPA DE RED/`](CAPA%20DE%20RED/) |
| Middleware | Reglas y sinks del broker EMQX | [`CAPA DE MIDDLEWARE/`](CAPA%20DE%20MIDDLEWARE/) |
| Aplicación | Persistencia en PostgreSQL y lógica en ThingsBoard | [`CAPA DE APLICACION/`](CAPA%20DE%20APLICACION/) |

Además:

| Carpeta | Contiene |
|---|---|
| `PRUEBAS DEL SISTEMA/` | Evidencia experimental: CSV de resultados y notebooks de análisis (descarga BLE, clasificación de comportamiento, posicionamiento) |
| `PROCESADO Y ENTRENAMIENTO RN/` | Entrenamiento del modelo de clasificación de comportamiento (PoC sobre dataset público, ver su propio README) |

## Flujo de datos (resumen)

```
Dispositivos (percepción) - decoders/gateway (red) - reglas y sinks EMQX (middleware) - PostgreSQL + ThingsBoard (aplicación)
```

## Autor

Andrés Cortés Martín — Trabajo Fin de Máster, Máster en Ingeniería Electrónica, Universidad de Zaragoza (2025–2026)