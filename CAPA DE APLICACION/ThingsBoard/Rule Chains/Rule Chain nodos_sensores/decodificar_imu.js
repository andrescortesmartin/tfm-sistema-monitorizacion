var ACCEL_SCALE  = 4.0 / 32768.0;      // LSB a g   (fondo de escala +/-4g)
var GYRO_SCALE   = 1000.0 / 32768.0;   // LSB a dps (fondo de escala +/-1000 dps)
var HEADER_BYTES = 5;   // cabecera de bloque: header(1) + length(2) + f_muestreo(1) + tipo_actividad(1)
var TS_BYTES     = 4;   // timestamp del bloque (uint32 LE, segundos)
var SAMPLE_BYTES = 7;   // cada muestra: tag(1) + 3 ejes int16 LE
var TAG_XL       = 0x01; // muestra de acelerometro
var TAG_GY       = 0x02; // muestra de giroscopio

// Periodo en ms entre muestras segun el codigo de BDR/ODR del sensor (LSM6DSx).
var ODR_TABLE_MS = {
    0: 0,
    1: 80,    // 12.5 Hz
    2: 38.46153846, // 26 Hz
    3: 19.23076923, // 52 Hz
    4: 9.61538461,  // 104 Hz
    5: 4.8076923,  // 208 Hz
    6: 2.239808153,  // 417 Hz
    7: 1.20048019,   // 833 Hz
    8: 0.5998802, //1667 Hz
    9: 0.30003, //3333 Hz
    10: 0.1499925, //6667 Hz
    11: 153.8461538 //6.5 Hz
};

// Lee un entero de 16 bits con signo, little-endian, desde bytes[idx].
function readInt16LE(bytes, idx) {
    var val = ((bytes[idx+1] & 0xFF) << 8) | (bytes[idx] & 0xFF);
    return (val & 0x8000) ? val - 0x10000 : val;
}

// Lee un entero de 32 bits sin signo, little-endian (se usa * 0x1000000 en el byte alto para no desbordar el signo con el operador <<).
function readUInt32LE(bytes, idx) {
    return (((bytes[idx+3] & 0xFF) * 0x1000000) +
            ((bytes[idx+2] & 0xFF) << 16) +
            ((bytes[idx+1] & 0xFF) << 8) +
             (bytes[idx]   & 0xFF));
}

// Convierte una cadena hex ("a2b3...") en array de bytes.
function hexToBytes(hex) {
    var bytes = [];
    for (var i = 0; i < hex.length; i += 2) {
        bytes.push(parseInt(hex.substr(i, 2), 16));
    }
    return bytes;
}

// Traduce el codigo de frecuencia a periodo en ms; si no es valido, 80ms (12.5Hz).
function odrToMs(odr) {
    var ms = ODR_TABLE_MS[odr];
    return (ms === undefined || ms === 0) ? 80 : ms; // fallback 80ms = 12.5Hz
}

// Tamano real del bloque en bytes, redondeado al multiplo de 4 (el firmware alinea cada bloque a 4 bytes).
function calcBlockSize(length) {
    var rawSize = HEADER_BYTES + length + TS_BYTES;
    var padding = (4 - (rawSize % 4)) % 4;
    return rawSize + padding;
}

function parseIMU(hexStr) {
    var raw    = hexToBytes(hexStr);
    var points = [];
    var i = 0;

    // Recorre el buffer buscando bloques validos. Cada iteracion procesa un bloque completo; si el header no es conocido se avanza byte a byte hasta el siguiente.
    while (i + HEADER_BYTES + TS_BYTES <= raw.length) {
        var header = raw[i] & 0xFF;

        if (header !== 0xA2 && header !== 0xA3 && header !== 0xA4) {
            i++;
            continue;
        }

        var length     = ((raw[i+2] & 0xFF) << 8) | (raw[i+1] & 0xFF); // bytes de payload (uint16 LE)
        var f_muestreo = raw[i+3] & 0xFF; // nibble alto = BDR del XL, nibble bajo = BDR del GY
        var blockTotal = calcBlockSize(length);

        if (i + blockTotal > raw.length) break; // bloque incompleto

        var xlMs = odrToMs((f_muestreo >> 4) & 0x0F);
        var gyMs = odrToMs(f_muestreo & 0x0F);

        var n            = Math.floor(length / SAMPLE_BYTES);
        var payloadStart = i + HEADER_BYTES;

        // El timestamp del bloque corresponde al instante del volcado de la FIFO
        var tsSeconds = readUInt32LE(raw, payloadStart + length);
        if (tsSeconds === 0) { i += blockTotal; continue; }
        var tsBlockMs = tsSeconds * 1000;

        // Solo se envia el timestamp del final del bloque, asi que primero contamos
        // cuantas muestras hay de cada tipo y luego fechamos cada una restando su
        // periodo hacia atras desde ese instante.
        var nXL = 0, nGY = 0;
        for (var c = 0; c < n; c++) {
            var t = raw[payloadStart + c * SAMPLE_BYTES] & 0xFF;
            if (t === TAG_XL) { nXL++; }
            else if (t === TAG_GY) { nGY++; }
        }

        // Segunda pasada para decodificar y fechar cada muestra
        var iXL = 0, iGY = 0;
        for (var s = 0; s < n; s++) { // Se analiza muestra a muestra
            var b      = payloadStart + s * SAMPLE_BYTES;
            var tag    = raw[b] & 0xFF;
            var d      = b + 1;   // primer byte de datos, tras el tipo
            var values = {};
            var sampleTs;

            // Segun el TAG, es un tipo de dato u otro
            if (tag === TAG_XL) {
                // La ultima muestra XL cae en tsBlockMs; las anteriores, un periodo antes cada una.
                sampleTs = tsBlockMs - (nXL - 1 - iXL) * xlMs;
                iXL++;
                values.xl_x = Math.round(readInt16LE(raw, d)   * ACCEL_SCALE * 10000) / 10000;
                values.xl_y = Math.round(readInt16LE(raw, d+2) * ACCEL_SCALE * 10000) / 10000;
                values.xl_z = Math.round(readInt16LE(raw, d+4) * ACCEL_SCALE * 10000) / 10000;
            } else if (tag === TAG_GY) {
                sampleTs = tsBlockMs - (nGY - 1 - iGY) * gyMs;
                iGY++;
                values.gy_x = Math.round(readInt16LE(raw, d)   * GYRO_SCALE * 10000) / 10000;
                values.gy_y = Math.round(readInt16LE(raw, d+2) * GYRO_SCALE * 10000) / 10000;
                values.gy_z = Math.round(readInt16LE(raw, d+4) * GYRO_SCALE * 10000) / 10000;
            } else {
                continue; // tipo desconocido, muestra descartada
            }

            points.push({ ts: Math.round(sampleTs), values: values });
        }

        i += blockTotal;
    }

    return points;
}

// Punto de entrada de la funcion, cogemos los datos de raw_imu del mensaje
var hexStr = msg.raw_imu;

// Comprobamos que el campo exista, sea string (en el gateway los mensajes se
// mandan como string) y no este vacio. Si no cumple, se devuelve el mensaje tal cual.
if (!hexStr || typeof hexStr !== 'string' || hexStr.length === 0) {
    return { msg: msg, metadata: metadata, msgType: msgType };
}

// Se decodifica el mensaje
var points = parseIMU(hexStr);

// Si no hay muestras validas se devuelve el mensaje tal cual
if (points.length === 0) {
return { msg: msg, metadata: metadata, msgType: msgType };
}

// Devolver las muestras en el formato multi-timestamp de TB
return {
    msg: points,
    metadata: metadata,
    msgType:  msgType
};
