# Capa de Aplicación — ThingsBoard

Esta carpeta contiene el código con lógica propia de ThingsBoard: los scripts de  las rule chains (transformaciones y filtros) y los widgets con lógica (comandos RPC y configuración remota de dispositivos vía BLE).

El funcionamiento detallado de cada rule chain (flujo de nodos, ramas condicionales) y de cada widget (interacción, protocolo de comandos) está documentado en el Anexo IX.

## Rule Chains

### `gw_lorawan`

| Fichero | Nodo / función |
|---|---|
| `parsear_datos.js` | Transformación: parsea el mensaje de estado del gateway (JSON de TTN), normaliza timestamps, calcula si está online (umbral 5 min sin actividad), extrae contadores/métricas de recepción (rxin, rxok, rxfw...) y RTT, y calcula la tasa de uplinks (paquetes/hora) comparando con el conteo previo guardado en atributos de servidor |
| `preparar_attr_gw.js` | Guarda como atributos SERVER_SCOPE el contador de uplinks actual y el timestamp de sondeo, para que `parsear_datos.js` pueda calcular la tasa de uplinks por diferencia en el siguiente ciclo |

> **Nota:** esta rule chain incluye además un nodo nativo de llamada REST a la API de The Things Stack, configurado con una API key. Ese nodo no se representa como fichero en esta carpeta por no ser un script propio, y la key no se incluye en el repositorio por tratarse de una credencial. Ver Anexo IX para el detalle del nodo (endpoint, propósito).

### `nodos_sensores`

| Fichero | Nodo / función |
|---|---|
| `comprobar_bloques_lorawan.js` | Filtro: comprueba presencia de bloques LoRaWAN en el payload |
| `comprobar_raw_imu.js` | Filtro: comprueba presencia de datos IMU crudos |
| `comprobar_raw_lora.js` | Filtro: comprueba presencia de datos LoRa crudos |
| `decodificar_imu.js` | Transformación: decodifica los datos IMU crudos |
| `decodificar_raw_lora.js` | Transformación: decodifica los datos LoRa crudos |
| `separar_bloques_lorawan.js` | Transformación: separa los bloques LoRaWAN del payload |

## Widgets

| Fichero | Widget | Tipo | Campo de configuración |
|---|---|---|---|
| `widget_comandos_gateway_ble.js` | Comandos del gateway BLE (Cassia) | Markdown/HTML Card | Apariencia - Función de valor Markdown/HTML |
| `widget_comandos_nodos_sensores.js` | Comandos del logger (nodo sensor) | Markdown/HTML Card | Apariencia - Función de valor Markdown/HTML |
| `widget_configuracion_nodos_sensores.js` | Configuración de nodo sensor (lectura/escritura de `logger_config_t` vía BLE) | Markdown/HTML Card | Apariencia - Función de valor Markdown/HTML |

> **Nota sobre `widget_configuracion_nodos_sensores.js`:** el layout de offsets de 39 bytes que codifica/decodifica debe mantenerse sincronizado con el struct `logger_config_t` del firmware. Ver Anexo IX, sección firmware.