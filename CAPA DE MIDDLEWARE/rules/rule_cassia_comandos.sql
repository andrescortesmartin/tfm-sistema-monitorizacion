SELECT
  payload.device as device,
  payload.data.params as params
FROM
  "$bridges/mqtt:source_cassia_comandos"