var data = msg;
var out = {};

var now = Date.now();
var UMBRAL_ONLINE_MS = 5 * 60 * 1000; // margen para considerar el gateway "online"

// Convierte una fecha de TTN a milisegundos epoch. Devuelve null si no es valida.
function parseTtnDate(isoString) {
    if (!isoString) return null;
    if (isoString.indexOf('0001-01-01') === 0) return null; // valor cero de protobuf
    var truncated = isoString.replace(/(\.\d{3})\d+Z$/, '$1Z'); // recorta nanosegundos a milisegundos
    var ms = Date.parse(truncated);
    return isNaN(ms) ? null : ms;
}

// Asigna obj[key] = value solo si value no es null ni undefined.
// Asi evitamos escribir claves vacias en la telemetria cuando el dato no viene.
function setIfPresent(obj, key, value) {
    if (value !== null && value !== undefined) {
        obj[key] = value;
    }
}

// Ultimo status recibido del gateway
var lastStatusMs = parseTtnDate(data.last_status_received_at);
setIfPresent(out, 'gw_last_status_received_at', lastStatusMs);

// El gateway se considera online si hubo status o uplink dentro del umbral
var lastUplinkMs = parseTtnDate(data.last_uplink_received_at);
var lastKnownMs = Math.max(lastStatusMs || 0, lastUplinkMs || 0);
out.gw_online = lastKnownMs > 0 && (now - lastKnownMs) < UMBRAL_ONLINE_MS;

// Marcas de tiempo de actividad reciente del gateway
setIfPresent(out, 'gw_last_uplink_received_at', parseTtnDate(data.last_uplink_received_at));
setIfPresent(out, 'gw_last_downlink_received_at', parseTtnDate(data.last_downlink_received_at));
setIfPresent(out, 'gw_connected_at', parseTtnDate(data.connected_at));
setIfPresent(out, 'gw_disconnected_at', parseTtnDate(data.disconnected_at));

// Contadores acumulados (vienen como string en la API)
out.gw_uplink_count = parseInt(data.uplink_count || "0");
out.gw_downlink_count = parseInt(data.downlink_count || "0");
out.gw_tx_ack_count = parseInt(data.tx_acknowledgment_count || "0");

// Metricas del ultimo status reportado por el gateway (paquetes rx/tx, ratio de ack)
if (data.last_status && data.last_status.metrics) {
    var m = data.last_status.metrics;
    out.gw_rxin = m.rxin || 0;
    out.gw_rxok = m.rxok || 0;
    out.gw_rxfw = m.rxfw || 0;
    out.gw_txin = m.txin || 0;
    out.gw_txok = m.txok || 0;
    out.gw_ackr = m.ackr || 0;
}

// Tiempos de ida y vuelta hacia el gateway (la API los da en segundos, pasamos a ms)
if (data.round_trip_times) {
    var rtt = data.round_trip_times;
    setIfPresent(out, 'gw_rtt_median_ms', rtt.median ? parseFloat(rtt.median) * 1000 : null);
    setIfPresent(out, 'gw_rtt_min_ms', rtt.min ? parseFloat(rtt.min) * 1000 : null);
    setIfPresent(out, 'gw_rtt_max_ms', rtt.max ? parseFloat(rtt.max) * 1000 : null);
    out.gw_rtt_count = rtt.count || 0;
}

// Datos de red del gateway
setIfPresent(out, 'gw_remote_ip', data.gateway_remote_address ? data.gateway_remote_address.ip : null);
setIfPresent(out, 'gw_protocol', data.protocol);

// Tasa de uplinks (paquetes/hora), usando el valor previo guardado como atributo ---
var currentCount = out.gw_uplink_count;
var prevCount = metadata.ss_gw_prev_uplink_count ? parseInt(metadata.ss_gw_prev_uplink_count) : null;
var prevTs = metadata.ss_gw_prev_poll_ts ? parseInt(metadata.ss_gw_prev_poll_ts) : null;

// Necesitamos una muestra previa para poder calcular la variacion
if (prevCount !== null && prevTs !== null && now > prevTs) {
    var deltaCount = currentCount - prevCount;
    var deltaHours = (now - prevTs) / (1000 * 60 * 60);
    if (deltaCount >= 0 && deltaHours > 0) {
        out.gw_uplink_rate_pph = Math.round(deltaCount / deltaHours);
    }
    // deltaCount < 0 => el contador se reinicio (reboot del GS), no calculamos tasa ese ciclo
}

return {msg: out, metadata: metadata, msgType: msgType};
