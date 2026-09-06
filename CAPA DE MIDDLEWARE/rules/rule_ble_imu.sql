SELECT
  nth(2, tokens(topic, '/')) AS device_id,
  payload.ts * 1000 AS msg_ts,
  payload.session_id AS session_id,
  payload.es_primero AS es_primero,
  payload.es_ultimo AS es_ultimo,
  payload.data AS raw_data,
  'imu' AS tipo
FROM
  "/cassia/+/datos/imu"
WHERE payload.es_ultimo = false
