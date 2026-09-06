INSERT INTO metricas_sistema (dispositivo_id, timestamp, tipo, datos)
VALUES (
    (SELECT id FROM dispositivos WHERE identificador = ${device_id}),
    to_timestamp(${msg_ts}::BIGINT / 1000),
    'gateway_ble',
    jsonb_build_object(
        'mem_free',        ${mem_free}::INTEGER,
        'mem_alloc',       ${mem_alloc}::INTEGER,
        'uptime_gw',       ${uptime_gw}::INTEGER,
        'uptime_script',   ${uptime_script}::INTEGER,
        'ip',              ${ip}::TEXT,
        'wifi_signal',     ${wifi_signal}::INTEGER,
        'uplink',          ${uplink}::TEXT,
        'version',         ${version}::TEXT,
        'visibles',        ${visibles}::JSONB,
        'known_macs',      ${known_macs}::JSONB,
        'umbral_descarga', ${umbral_descarga}::INTEGER,
        'pausa_autonomo',  ${pausa_autonomo}::BOOLEAN,
        'filtro_rssi',     ${filtro_rssi}::INTEGER
    )
)
ON CONFLICT (dispositivo_id, timestamp, tipo) DO NOTHING;