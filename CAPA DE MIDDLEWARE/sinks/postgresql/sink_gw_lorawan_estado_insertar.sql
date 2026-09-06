INSERT INTO metricas_sistema (dispositivo_id, timestamp, tipo, datos)
VALUES (
    (SELECT id FROM dispositivos WHERE identificador = ${gateway_id}),
    to_timestamp(${msg_ts}::BIGINT),
    'gateway_lorawan',
    jsonb_build_object(
        'gw_online',                     ${gw_online}::BOOLEAN,
        'gw_last_status_received_at',    to_timestamp(${gw_last_status_received_at}::BIGINT / 1000),
        'gw_last_uplink_received_at',    to_timestamp(${gw_last_uplink_received_at}::BIGINT / 1000),
        'gw_last_downlink_received_at',  to_timestamp(${gw_last_downlink_received_at}::BIGINT / 1000),
        'gw_connected_at',               to_timestamp(${gw_connected_at}::BIGINT / 1000),
        'gw_disconnected_at',            to_timestamp(${gw_disconnected_at}::BIGINT / 1000),
        'gw_uplink_count',               ${gw_uplink_count}::BIGINT,
        'gw_downlink_count',             ${gw_downlink_count}::BIGINT,
        'gw_tx_ack_count',               ${gw_tx_ack_count}::BIGINT,
        'gw_rxin',                       ${gw_rxin}::INTEGER,
        'gw_rxok',                       ${gw_rxok}::INTEGER,
        'gw_rxfw',                       ${gw_rxfw}::INTEGER,
        'gw_txin',                       ${gw_txin}::INTEGER,
        'gw_txok',                       ${gw_txok}::INTEGER,
        'gw_ackr',                       ${gw_ackr}::INTEGER,
        'gw_rtt_median_ms',              ${gw_rtt_median_ms}::DOUBLE PRECISION,
        'gw_rtt_min_ms',                 ${gw_rtt_min_ms}::DOUBLE PRECISION,
        'gw_rtt_max_ms',                 ${gw_rtt_max_ms}::DOUBLE PRECISION,
        'gw_rtt_count',                  ${gw_rtt_count}::INTEGER,
        'gw_remote_ip',                  ${gw_remote_ip}::TEXT,
        'gw_protocol',                   ${gw_protocol}::TEXT,
        'gw_uplink_rate_pph',            ${gw_uplink_rate_pph}::INTEGER
    )
)
ON CONFLICT (dispositivo_id, timestamp, tipo) DO NOTHING;