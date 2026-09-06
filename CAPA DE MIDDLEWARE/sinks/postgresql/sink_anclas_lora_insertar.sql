INSERT INTO metricas_sistema (dispositivo_id, timestamp, tipo, datos)
VALUES (
    (SELECT id FROM dispositivos WHERE identificador = ${rttof_addr}::TEXT), to_timestamp(${msg_ts}::BIGINT/ 1000), 'ancla_lora', jsonb_build_object(
        'rttof_addr',           ${rttof_addr}::TEXT,
        'ranging_count',        ${ranging_count}::INTEGER,
        'discard_count',        ${discard_count}::INTEGER,
        'last_rssi',            ${last_rssi}::FLOAT,
        'last_snr',             ${last_snr}::FLOAT,
        'temperatura',          ${temperatura}::INTEGER,
        'humedad',              ${humedad}::INTEGER,
        'heartbeat_interval_s', ${heartbeat_interval_s}::INTEGER,
        'lora_sf',              ${lora_sf}::INTEGER,
        'lora_bw',              ${lora_bw}::INTEGER,
        'rssi_lorawan',         ${rssi_lorawan}::FLOAT,
        'snr_lorawan',          ${snr_lorawan}::FLOAT
    )
)
ON CONFLICT (dispositivo_id, timestamp, tipo) DO NOTHING;