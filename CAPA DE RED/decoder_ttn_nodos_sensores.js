function decodeUplink(input) {
  var bytes = input.bytes;
  var decoded = {};
  var i = 0;

  // Flags de envío
  var flags = bytes[i++];
  decoded.flags = {
    gps:      !!(flags & 0x01),
    beacons:  !!(flags & 0x02),
    ranging:  !!(flags & 0x04),
    ambiente: !!(flags & 0x08),
    angulo:   !!(flags & 0x10),
  };

  // Batería — siempre presente
  decoded.bateria_mv = readUInt16LE(bytes, i); i += 2;

  // Ambiente
  if (decoded.flags.ambiente) {
    decoded.presion     = readInt16LE(bytes, i);  i += 2;
    decoded.temperatura = readInt8(bytes, i);     i += 1;
    decoded.ts_ambiente = readUInt32LE(bytes, i); i += 4;
  }

  // Ángulo
  if (decoded.flags.angulo) {
    decoded.pitch     = readInt16LE(bytes, i) / 10; i += 2;
    decoded.roll      = readInt16LE(bytes, i) / 10; i += 2;
    decoded.ts_angulo = readUInt32LE(bytes, i);      i += 4;
  }

  // GPS
  if (decoded.flags.gps) {
    decoded.latitud  = readInt32LE(bytes, i) / 1000000; i += 4;
    decoded.longitud = readInt32LE(bytes, i) / 1000000; i += 4;
    decoded.ts_gps   = readUInt32LE(bytes, i);           i += 4;
  }

  // Ranging
  if (decoded.flags.ranging) {
    var ranging_len = readUInt8(bytes, i); i += 1;

    // Primero vienen todas las distancias (1 byte c/u)
    var dists = [];
    for (var k = 0; k < ranging_len; k++) {
      dists.push(readUInt16LE(bytes, i));
      i += 2;
    }

    // Luego todas las direcciones (uint32 LE c/u), se combinan con su distancia
    decoded.ranging = [];
    for (var k = 0; k < ranging_len; k++) {
      decoded.ranging.push({
        addr: '0x' + ('00000000' + readUInt32LE(bytes, i).toString(16)).slice(-8),
        dist: dists[k]
      });
      i += 4;
    }

    // Luego todos los rssi (int8 c/u, uno por ancla)
    for (var k = 0; k < ranging_len; k++) {
      decoded.ranging[k].rssi = readInt8(bytes, i);
      i += 1;
    }

    decoded.ts_ranging = readUInt32LE(bytes, i); i += 4;
  }

  // Beacons
  if (decoded.flags.beacons) {
    // Lista de loggers BLE
    var logger_count = readUInt8(bytes, i++);
    decoded.ble_loggers = [];
    for (var k = 0; k < logger_count; k++) {
      decoded.ble_loggers.push({
        id:   readUInt8(bytes, i++),
        rssi: readInt8(bytes, i++)   // signed
      });
    }

    // Lista de beacons BLE
    var beacon_count = readUInt8(bytes, i++);
    decoded.ble_beacons = [];
    for (var k = 0; k < beacon_count; k++) {
      decoded.ble_beacons.push({
        id:   readUInt8(bytes, i++),
        rssi: readInt8(bytes, i++)   // signed
      });
    }

    decoded.ts_beacons = readUInt32LE(bytes, i); i += 4;
  }

  // Diagnóstico — 14 bytes fijos
  if (i + 14 <= bytes.length) {
    decoded.reset_count       = readUInt16LE(bytes, i); i += 2;  // uint16
    decoded.ttff_gps          = readUInt8(bytes, i);    i += 1;  // uint8
    decoded.flash_dir_lora    = readUInt32LE(bytes, i); i += 4;  // uint32
    decoded.flash_dir_imu     = readUInt32LE(bytes, i); i += 4;  // uint32
    decoded.actividad_eventos = readUInt16LE(bytes, i); i += 2;  // uint16
    decoded.flash_descarte    = readUInt8(bytes, i);    i += 1;  // uint8 
  }

  // MAC — 6 bytes (lectura secuencial, ya no desde el final)
  if (i + 6 <= bytes.length) {
    var macBytes = [];
    for (var k = 0; k < 6; k++) macBytes.push(bytes[i++]);
    decoded.mac = bytesToHex(macBytes.reverse());
  }

  return { data: decoded, warnings: [], errors: [] };
}

function bytesToHex(arr) {
  var s = '';
  for (var i = 0; i < arr.length; i++) {
    s += ('0' + arr[i].toString(16)).slice(-2);
  }
  return s;
}

function readInt16LE(bytes, idx) {
  var val = (bytes[idx+1] << 8) | bytes[idx];
  return (val & 0x8000) ? val - 0x10000 : val;
}

function readUInt16LE(bytes, idx) {
  return (bytes[idx + 1] << 8) | bytes[idx];
}

function readInt32LE(bytes, idx) {
  return (bytes[idx+3] << 24) | (bytes[idx+2] << 16) | (bytes[idx+1] << 8) | bytes[idx];
}

function readUInt32LE(bytes, idx) {
  return ((bytes[idx+3] << 24) >>> 0) + ((bytes[idx+2] << 16) | (bytes[idx+1] << 8) | bytes[idx]);
}

function readUInt24LE(bytes, idx) {
    return (bytes[idx+2] << 16) | (bytes[idx+1] << 8) | bytes[idx];
}

function readUInt8(bytes, idx) {
  return bytes[idx];
}

function readInt8(bytes, idx) {
  var val = bytes[idx];
  return (val & 0x80) ? val - 0x100 : val;
}