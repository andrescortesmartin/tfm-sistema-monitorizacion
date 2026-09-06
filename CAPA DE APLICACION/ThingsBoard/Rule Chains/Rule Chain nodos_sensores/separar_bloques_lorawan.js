var dp = msg.values || msg;
var points = [];

// Calcular el ts más reciente de los bloques activos
var ts_max = 0;
if (dp.ts_ambiente && dp.ts_ambiente > ts_max) ts_max = dp.ts_ambiente;
if (dp.ts_angulo   && dp.ts_angulo   > ts_max) ts_max = dp.ts_angulo;
if (dp.ts_gps      && dp.ts_gps      > ts_max) ts_max = dp.ts_gps;
if (dp.ts_ranging  && dp.ts_ranging  > ts_max) ts_max = dp.ts_ranging;
if (dp.ts_beacons  && dp.ts_beacons  > ts_max) ts_max = dp.ts_beacons;

// Bloque siempre presente: batería + métricas LoRa + diagnóstico
// Si no hay ningún módulo opcional en el envío, no hay timestamp propio del
// dispositivo del que colgar este bloque . Como aproximación,
// se usa la hora de procesamiento en ThingsBoard en lugar de descartarlo.
var ts_diag = ts_max > 0 ? ts_max * 1000 : Date.now();
if (ts_diag > 0) {
    points.push({
        ts: ts_diag,
        values: {
            bateria_mv:        dp.bateria_mv,
            rssi:              dp.rssi,
            snr:               dp.snr,
            reset_count:       dp.reset_count,
            ttff_gps:          dp.ttff_gps,
            flash_dir_lora:    dp.flash_dir_lora,
            flash_dir_imu:     dp.flash_dir_imu,
            flash_descarte:    dp.flash_descarte,
            actividad_eventos: dp.actividad_eventos
        }
    });
}

// Bloque ambiente
if (dp.ts_ambiente) {
    points.push({
        ts: dp.ts_ambiente * 1000,
        values: {
            presion:     dp.presion,
            temperatura: dp.temperatura
        }
    });
}

// Bloque ángulo
if (dp.ts_angulo) {
    points.push({
        ts: dp.ts_angulo * 1000,
        values: {
            pitch: dp.pitch,
            roll:  dp.roll
        }
    });
}

// Bloque GPS
if (dp.ts_gps) {
    points.push({
        ts: dp.ts_gps * 1000,
        values: {
            latitude:  dp.latitud,
            longitude: dp.longitud
        }
    });
}

// Bloque ranging
if (dp.ts_ranging) {
    points.push({
        ts: dp.ts_ranging * 1000,
        values: {
            ranging: dp.ranging
        }
    });
}

// Bloque beacons
if (dp.ts_beacons) {
    points.push({
        ts: dp.ts_beacons * 1000,
        values: {
            ble_loggers: JSON.stringify(dp.ble_loggers),
            ble_beacons: JSON.stringify(dp.ble_beacons)
        }
    });
}

if (points.length === 0) {
    return { msg: msg, metadata: metadata, msgType: msgType };
}

return { msg: points, metadata: metadata, msgType: msgType };