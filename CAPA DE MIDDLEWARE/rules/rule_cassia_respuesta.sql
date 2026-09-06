SELECT
  nth(2, tokens(topic, '/')) AS gateway_id,
  payload.cmd                AS cmd,
  payload.mac                AS mac,
  payload.status             AS status,
  coalesce(payload.data, '{}') AS data,
  timestamp                  AS msg_ts
FROM
  "/cassia/+/respuesta"