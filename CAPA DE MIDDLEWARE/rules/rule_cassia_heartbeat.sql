SELECT
  nth(2, tokens(topic, '/')) AS device_id,
  payload.ts * 1000 AS msg_ts,
  payload.sistema.mem_free AS mem_free,
  payload.sistema.mem_alloc AS mem_alloc,
  payload.sistema.uptime_gw AS uptime_gw,
  payload.sistema.uptime_script AS uptime_script,
  payload.sistema.ip AS ip,
  payload.sistema.wifi_signal AS wifi_signal,
  payload.sistema.uplink AS uplink,
  payload.sistema.version AS version,
  json_encode(payload.visibles) AS visibles,
  payload.umbral_descarga AS umbral_descarga,
  json_encode(payload.known_macs) AS known_macs,
  payload.pausa_autonomo AS pausa_autonomo,
  payload.filtro_rssi AS filtro_rssi
FROM
  "/cassia/+/heartbeat"