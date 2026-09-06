function decodeUplink(input) {
  var bytes = input.bytes;
  var decoded = {};

  if (bytes.length < 17) {
    return { data: {}, warnings: [], errors: ["Payload demasiado corto: " + bytes.length + " bytes"] };
  }

  // RTTOF Address — uint32 BE
  decoded.rttof_addr = '0x' + ('00000000' + (
    ((bytes[0] << 24) >>> 0) | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3]
  ).toString(16)).slice(-8);

  // Ranging count — uint24 BE
  decoded.ranging_count = (bytes[4] << 16) | (bytes[5] << 8) | bytes[6];

  // Discard count — uint16 BE
  decoded.discard_count = (bytes[7] << 8) | bytes[8];

  // RSSI y SNR — int8 (signed)
  decoded.last_rssi = (bytes[9]  & 0x80) ? bytes[9]  - 0x100 : bytes[9];
  decoded.last_snr  = (bytes[10] & 0x80) ? bytes[10] - 0x100 : bytes[10];

  // Temperatura — int8 (signed, truncado desde float)
  decoded.temperatura = (bytes[11] & 0x80) ? bytes[11] - 0x100 : bytes[11];

  // Humedad — uint8 (0 si era negativa)
  decoded.humedad = bytes[12];
  
  decoded.heartbeat_interval_s = (bytes[13] << 8) | bytes[14];

  // LoRa SF y BW — uint8
  decoded.lora_sf = bytes[15];
  decoded.lora_bw = bytes[16];

  return { data: decoded, warnings: [], errors: [] };
}