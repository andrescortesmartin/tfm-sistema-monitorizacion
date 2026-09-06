SELECT
  payload.uplink_message.decoded_payload.mac AS device_id,
  map_put(
    'rssi', payload.uplink_message.rx_metadata[1].rssi,
    map_put(
      'snr', payload.uplink_message.rx_metadata[1].snr,
      payload.uplink_message.decoded_payload
    )
  ) AS telemetry
FROM
  "logger/uplink"
WHERE
  is_not_null(payload.uplink_message.decoded_payload.mac)