# Diseño y despliegue de un sistema IoT multicapa para bio-logging y monitorización avanzada de fauna

Sistema IoT de bio-logging para el seguimiento de fauna silvestre: firmware de los dispositivos de campo, decodificación y transporte de los datos, y persistencia/visualización en la capa de aplicación. Este repositorio acompaña la memoria del Trabajo Fin de Máster (TFM) y sus anexos. Cada carpeta referencia el capítulo/anexo correspondiente en su propio README.

## Diagrama del sistema

![Diagrama del sistema](Diagrama%20del%20sistema.png)

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

## Correspondencia con la memoria

| Capítulo | Anexo | Carpeta |
|---|---|---|
| Capítulo 4.1 | Anexo III | [`CAPA DE PERCEPCION/BALIZAS MOVILES/`](CAPA%20DE%20PERCEPCION/BALIZAS%20MOVILES/) |
| Capítulo 4.1 | Anexo IV | [`CAPA DE PERCEPCION/ANCLAS LORA/`](CAPA%20DE%20PERCEPCION/ANCLAS%20LORA/) |
| Capítulo 4.1 y 4.2 | Anexo V | [`CAPA DE PERCEPCION/NODOS SENSORES/`](CAPA%20DE%20PERCEPCION/NODOS%20SENSORES/) |
| Capítulo 4.3.1 | Anexo VI | [`CAPA DE RED/`](CAPA%20DE%20RED/) (decoders TTN) |
| Capítulo 4.3.2 | Anexo VII | [`CAPA DE RED/`](CAPA%20DE%20RED/) (MicroApp gateway Cassia) |
| Capítulo 4.4 | Anexo VIII | [`CAPA DE MIDDLEWARE/`](CAPA%20DE%20MIDDLEWARE/) |
| Capitulo 4.5.1 | Anexo IX | [`CAPA DE APLICACION/ThingsBoard/`](CAPA%20DE%20APLICACION/ThingsBoard/) |
| Capítulo 4.5.2 | Anexo X | [`CAPA DE APLICACION/PostgreSQL/`](CAPA%20DE%20APLICACION/PostgreSQL/) (tablas) |
| Capítulo 4.6.1 a 4.6.4 | Anexo XI y Anexo II | [`CAPA DE APLICACION/PostgreSQL/`](CAPA%20DE%20APLICACION/PostgreSQL/) (funciones) |
| Capítulo 4.6.5 | Anexo XI | [`PROCESADO Y ENTRENAMIENTO RN/`](PROCESADO%20Y%20ENTRENAMIENTO%20RN/) |
| Capítulo 4.7.2 | Anexo XII | [`PRUEBAS DEL SISTEMA/descarga_ble/`](PRUEBAS%20DEL%20SISTEMA/descarga_ble/) |
| Capítulo 4.7.3 | Anexo XII y Anexo II | [`PRUEBAS DEL SISTEMA/posicionamiento/`](PRUEBAS%20DEL%20SISTEMA/posicionamiento/) |
| Capítulo 4.7.4 | Anexo XII | [`PRUEBAS DEL SISTEMA/indicadores_clasificacion/`](PRUEBAS%20DEL%20SISTEMA/indicadores_clasificacion/) |

## Autor

Andrés Cortés Martín — Trabajo Fin de Máster, Máster en Ingeniería Electrónica, Universidad de Zaragoza (2025–2026)