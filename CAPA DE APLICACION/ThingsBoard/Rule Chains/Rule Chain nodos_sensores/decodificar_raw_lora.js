var hex = msg.raw_lora;
var bytes = [];
for (var i = 0; i < hex.length; i += 2) {
    bytes.push(parseInt(hex.substr(i, 2), 16));
}

var pos = 0;
var points = [];

function toSigned16(v) { 
    return v > 32767 ? v - 65536 : v; 
}
function toSigned8(v)  { 
    return v > 127  ? v - 256  : v; 
}

// Cada vuelta procesa un registro. Se corta al primer byte que no sea 0xA1.
while (pos < bytes.length) {
    if (bytes[pos] !== 0xA1) break;

    var longitud = bytes[pos + 1] | (bytes[pos + 2] << 8); // longitud del payload (uint16 LE)
    pos += 3;

    var flags = bytes[pos]; pos += 1;                       // bitmask de bloques presentes
    var bateria = bytes[pos] | (bytes[pos + 1] << 8); pos += 2; // siempre presente, en mV

    var values = { bateria_mv: bateria };

    // bit 3: ambiente (presion + temperatura)
    if (flags & 8) {
        values.presion = toSigned16(bytes[pos] | (bytes[pos + 1] << 8)); pos += 2;
        values.temperatura = toSigned8(bytes[pos]); pos += 1;
    }

    // bit 6: angulo (pitch + roll, en decimas de grado)
    if (flags & 64) {
        values.pitch = toSigned16(bytes[pos] | (bytes[pos + 1] << 8)) / 10.0; pos += 2;
        values.roll  = toSigned16(bytes[pos] | (bytes[pos + 1] << 8)) / 10.0; pos += 2;
    }

    // bit 0: GPS (lat/lon en millonesimas de grado)
    if (flags & 1) {
        var rawLat = (bytes[pos] | (bytes[pos+1]<<8) | (bytes[pos+2]<<16) | (bytes[pos+3]<<24));
        pos += 4;
        var rawLon = (bytes[pos] | (bytes[pos+1]<<8) | (bytes[pos+2]<<16) | (bytes[pos+3]<<24));
        pos += 4;
        values.latitude  = rawLat / 1000000.0;
        values.longitude = rawLon / 1000000.0;
    }

    // Ordena por distancia usando un array de indices (no el de distancias) para poder recuperar el rssi de la MISMA muestra que quedo en la mediana.
    function medianaConRssi(distArr, rssiArr) {
        var n = distArr.length;
        var idx = distArr.map(function (_, i) { return i; });
        idx.sort(function (a, b) { return distArr[a] - distArr[b]; });
        var mid = idx[Math.floor((n - 1) / 2)]; // para n par, la mediana "baja"
        return { dist: distArr[mid], rssi: rssiArr[mid] };
    }
    
    // bit 2: ranging compacto (solo mediana de distancia + su rssi por dispositivo)
    if (flags & 4) {
        var rangingLen = bytes[pos]; pos += 1; // nº de dispositivos con los que se ha medido
        var addrs = []; // direcciones (uint32 LE), una por dispositivo
        for (var a = 0; a < rangingLen; a++) {
            var addr = (bytes[pos+a*4] | (bytes[pos+a*4+1]<<8) | (bytes[pos+a*4+2]<<16) | (bytes[pos+a*4+3]<<24)) >>> 0;
            addrs.push(addr);
        }
        pos += rangingLen * 4;
    
        var rangingOut = [];
        for (var r = 0; r < rangingLen; r++) {
            var nMuestras = bytes[pos]; pos += 1;
    
            var distArr = [];
            for (var mI = 0; mI < nMuestras; mI++) {
                distArr.push(toSigned16(bytes[pos + mI*2] | (bytes[pos + mI*2 + 1] << 8)));
            }
            var rssiArr = [];
            for (var mI2 = 0; mI2 < nMuestras; mI2++) {
                rssiArr.push(toSigned8(bytes[pos + nMuestras*2 + mI2]));
            }
            pos += nMuestras * 3; // distArr (2B c/u) + rssiArr (1B c/u); se avanza aunque solo se guarde la mediana
    
            if (nMuestras > 0) {
                var med = medianaConRssi(distArr, rssiArr);
                rangingOut.push({ addr: addrs[r], dist: med.dist, rssi: med.rssi });
            }
        }
        values.ranging = JSON.stringify(rangingOut);
    }
    
    // bit 1: BLE loggers + beacons compacto, solo id + rssi (se descartan obs y data del beacon)
    if (flags & 2) {
        var loggerCount = bytes[pos]; pos += 1;
        var loggersOut = [];
        for (var lg = 0; lg < loggerCount; lg++) {
            pos += 1; // byte de tipo (HOWL_TYPE_LOGGER), se descarta
            var lId   = bytes[pos];
            var lRssi = toSigned8(bytes[pos + 1]);
            // obs se descarta del output, pero forma parte del bloque -> hay que saltarlo
            loggersOut.push({ id: lId, rssi: lRssi });
            pos += 3; // id + rssi + obs
        }
    
        var beaconCount = bytes[pos]; pos += 1;
        var beaconsOut = [];
        for (var bc = 0; bc < beaconCount; bc++) {
            pos += 1; // byte de tipo (HOWL_TYPE_BEACON), se descarta
            var bId   = bytes[pos];
            var bRssi = toSigned8(bytes[pos + 1]);
            var bLen  = bytes[pos + 3]; // obs (pos+2) se descarta, len esta en pos+3
            pos += 4;
            pos += bLen; // data se descarta del output, pero hay que saltarla igualmente
            beaconsOut.push({ id: bId, rssi: bRssi });
        }
    
        values.ble_loggers = JSON.stringify(loggersOut);
        values.ble_beacons = JSON.stringify(beaconsOut);
    }

    var ts = (bytes[pos] | (bytes[pos+1]<<8) | (bytes[pos+2]<<16) | (bytes[pos+3]<<24)) >>> 0; // timestamp del registro (uint32 LE, segundos)
    pos += 4;

    points.push({ ts: ts * 1000, values: values }); // TB espera ms

    var padding = (4 - ((longitud + 3) % 4)) % 4; // los registros van alineados a 4 bytes
    pos += padding;
}

// Si no se decodifico ningun registro, se deja pasar el mensaje original
if (points.length === 0) {
    return { msg: msg, metadata: metadata, msgType: msgType };
}

return { msg: points, metadata: metadata, msgType: msgType };