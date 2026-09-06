# Capa de Red

Esta carpeta contiene los componentes de la capa de red del sistema: los decoders de payload para The Things Stack (TTN) y la MicroApp que se ejecuta en el gateway BLE Cassia M2000 (puente BLE - MQTT).

## Contenido

| Fichero | Descripción | Memoria | Anexo |
|---|---|---|---|
| `decoder_ttn_anclas_lora.js` | Decoder de uplink para anclas LoRa en The Things Stack | Capítulo 4.3.1 | Anexo VI |
| `decoder_ttn_nodos_sensores.js` | Decoder de uplink para nodos sensores | Capítulo 4.3.1 | Anexo VI |
| `script_gw_cassia.py` | MicroApp que se ejecuta en el gateway Cassia M2000. Hace de puente BLE - MQTT para los nodos sensores (conexión, lectura/escritura de configuración, descarga de flash) y publica/consume topics MQTT que procesa CAPA DE MIDDLEWARE (rules rule_cassia_*). | Capítulo 4.3.2 | Anexo VII |

## Dependencias

El `script_gw_cassia.py` se comunica con el gateway Cassia M2000 combinando dos vías:

- **Llamadas a la API REST del gateway** — documentadas en la guía SDK de Cassia: https://github.com/CassiaNetworks/CassiaSDKGuide/wiki
- **Funciones de las librerías del Cassia MicroApp SDK** — no incluidas en este repositorio (propietarias del fabricante): http://bluetooth.tech/MicroAPP/2.2.mp.2509291554/html_en/quickstart/index.html
