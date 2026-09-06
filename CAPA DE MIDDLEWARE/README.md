# Middleware — EMQX

Esta carpeta contiene la configuración exportada del broker EMQX: reglas (rules), 
fuentes (sources) y destinos (sinks) que gestionan el enrutamiento de datos entre 
gateways, PostgreSQL y ThingsBoard.

## Estructura

```
rules/         Reglas de procesado, transformación y enrutado
sinks/
  mqtt/        Salidas hacia topics MQTT
  postgresql/  Salidas de inserción en PostgreSQL
```

## Correspondencia Rules - Sinks/Source

| Rule | Naturaleza | Entrada | Salida(s) | Destino |
|---|---|---|---|---|
| `rule_logger_lora` | Telemetría | Messages `logger/uplink` | `sink_logger_telemtry` (MQTT) + `sink_lorawan_insertar` (PostgreSQL) | Logger_tortugas / tabla PG |
| `rule_estado_gw_lorawan` | Estado | Messages `tb/gateway_status/+` | `sink_gw_lorawan_estado_insertar` (PostgreSQL) | Tabla PG |
| `rule_cassia_respuesta` | Telemetría | Messages `/cassia/+/respuesta` | `sink_cassia_respuesta` (MQTT) | Gateway-cassia |
| `rule_cassia_heartbeat` | Heartbeat | Messages `/cassia/+/heartbeat` | `sink_cassia_heartbeat` (MQTT) + `sink_cassia_heartbeat_insertar` (PostgreSQL) | Gateway-cassia / tabla PG |
| `rule_cassia_comandos` | Control (downlink) | `source_cassia_comandos` (MQTT Broker externo) | Republish → `/cassia/${device}/comandos` | Gateway-cassia |
| `rule_ble_lora` | Telemetría (condicional) | Messages `/cassia/+/datos/lora` | `sink_ble_lora` (MQTT) + `sink_lora_insertar` (PostgreSQL) | Gateway-cassia / tabla PG |
| `rule_ble_imu` | Telemetría (condicional) | Messages `/cassia/+/datos/imu` | `sink_ble_imu` (MQTT) + `sink_imu_insertar` (PostgreSQL) | Gateway-cassia / tabla PG |
| `rule_anclas_lora` | Telemetría | Messages `anclas_lora/uplink` | `sink_anclas_lora` (MQTT) + `sink_anclas_lora_insertar` (PostgreSQL) | Anclas_lora / tabla PG |

## Referencias

| Sección | Memoria | Anexo |
|---|---|---|
| Middleware / EMQX | Capítulo 4.4 | Anexo VIII |