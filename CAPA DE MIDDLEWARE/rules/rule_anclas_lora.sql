SELECT
  payload.end_device_ids.device_id AS device_id,
  payload.uplink_message.rx_metadata[1].rssi AS rssi_lorawan,
  payload.uplink_message.rx_metadata[1].snr AS snr_lorawan,
  payload.uplink_message.decoded_payload.rttof_addr AS rttof_addr,
  payload.uplink_message.decoded_payload.ranging_count AS ranging_count,
  payload.uplink_message.decoded_payload.discard_count AS discard_count,
  payload.uplink_message.decoded_payload.last_rssi AS last_rssi,
  payload.uplink_message.decoded_payload.last_snr AS last_snr,
  payload.uplink_message.decoded_payload.temperatura AS temperatura,
  payload.uplink_message.decoded_payload.humedad AS humedad,
  payload.uplink_message.decoded_payload.heartbeat_interval_s AS heartbeat_interval_s,
  payload.uplink_message.decoded_payload.lora_sf AS lora_sf,
  payload.uplink_message.decoded_payload.lora_bw AS lora_bw,
  now_timestamp() * 1000 AS msg_ts
FROM
  "anclas_lora/uplink"