INSERT INTO sectores_raw (dispositivo_id, session_id, tipo, data)
VALUES (
  (SELECT id FROM dispositivos WHERE identificador = ${device_id}),
  ${session_id},
  ${tipo},
  decode(${raw_data}, 'hex')
)