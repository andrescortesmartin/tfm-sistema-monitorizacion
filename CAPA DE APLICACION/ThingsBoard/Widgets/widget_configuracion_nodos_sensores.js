var mac_logger = data[0]['entityName']; // MAC del logger activo en el widget (entityName = MAC sin separadores, ej. "AABBCCDDEEFF")
var http = ctx.http; // Cliente HTTP de ThingsBoard disponible en el contexto del widget

// Convierte una MAC sin separadores (AABBCCDDEEFF) a formato legible (AA:BB:CC:DD:EE:FF)
function formatearMac(mac) {
    return mac.match(/.{1,2}/g).join(':').toUpperCase();
}

// Consulta todos los gateways Cassia y devuelve el que mejor RSSI tenga hacia este logger.
// Permite enviar el comando por el gateway con mejor enlace BLE en cada momento.
async function obtenerMejorGateway() {
    // Buscar todos los dispositivos de tipo "Profile-gateway-cassia_gw" con su último telemetry "visibles"
    var resp = await http.post('/api/entitiesQuery/find?pageSize=100&page=0', {
        entityFilter: { type: "deviceType", deviceTypes: ["Profile-gateway-cassia_gw"] },
        entityFields: [{ type: "ENTITY_FIELD", key: "name" }],
        latestValues: [{ type: "TIME_SERIES", key: "visibles" }],
        pageLink: { pageSize: 100, page: 0 }
    }).toPromise();

    var gateways = resp.data;
    var mejorGw = null;
    var mejorRssi = -999;

    for (var gw of gateways) {
        var visiblesStr = gw.latest?.TIME_SERIES?.visibles?.value;
        if (!visiblesStr) continue;
        try {
            var visibles = JSON.parse(visiblesStr);
            for (var v of visibles) {
                var macNormalizada = v.mac.replace(/:/g, '').toUpperCase();
                // Quedarse con el gateway que tenga el RSSI más alto hacia este logger
                if (macNormalizada === mac_logger.toUpperCase() && v.rssi > mejorRssi) {
                    mejorRssi = v.rssi;
                    mejorGw = { id: gw.entityId.id, name: gw.latest.ENTITY_FIELD.name.value };
                }
            }
        } catch(e) {}
    }
    return mejorGw;
}

// Devuelve un botón ℹ que al hacer click muestra/oculta un popover con texto.
// uid: identificador único para el par botón+popover (evita colisiones de IDs)
// texto: HTML explicativo del contenido de la sección o campo
function makeInfoBtn(uid, texto) {
    var btnId = 'info-btn-' + uid;
    var popId = 'info-pop-' + uid;
    window['__infoToggle_' + uid] = function(e) {
        e.stopPropagation();
        var pop = document.getElementById(popId);
        document.querySelectorAll('.info-popover').forEach(function(p) {
            if (p.id !== popId) p.style.display = 'none';
        });
        pop.style.display = pop.style.display === 'block' ? 'none' : 'block';
        // Asignar innerHTML aquí, cuando el elemento ya existe en el DOM
        pop.innerHTML = texto;
    };
    if (!window.__infoClickOutRegistered) {
        window.__infoClickOutRegistered = true;
        document.addEventListener('click', function() {
            document.querySelectorAll('.info-popover').forEach(function(p) {
                p.style.display = 'none';
            });
        });
    }
    return '<span style="position:relative;display:inline-block;">'
        + '<button id="' + btnId + '" onclick="window[\'__infoToggle_' + uid + '\'](event)" '
        + 'style="margin-left:6px;width:18px;height:18px;border-radius:50%;border:1px solid #999;'
        + 'background:#f5f5f5;color:#555;font-size:11px;cursor:pointer;'
        + 'line-height:16px;padding:0;vertical-align:middle;">ℹ</button>'
        + '<div id="' + popId + '" class="info-popover" '
        + 'style="display:none;position:absolute;left:24px;top:-4px;z-index:999;'
        + 'background:white;border:1px solid #ccc;border-radius:6px;padding:10px 14px;'
        + 'width:360px;font-size:12px;line-height:1.5;color:#333;box-shadow:0 2px 8px rgba(0,0,0,0.15);'
        + 'font-weight:normal;"></div></span>';
}

// Textos por cada seccion
var INFO_FLAGS = '<b>FLAGS — Módulos activos</b><br>'
    + 'Mascara que habilita cada bloque funcional del logger:<br><br>'
    + '• <b>GPS</b>: <span style="font-weight:normal;">habilita el posicionamiento GPS</span><br>'
    + '• <b>Beacons</b>: <span style="font-weight:normal;">habilita el escaneo BLE de balizas cercanas</span><br>'
    + '• <b>Ranging</b>: <span style="font-weight:normal;">habilita la medida de distancia con LoRa RTToF (Round-Trip Time of Flight)</span><br>'
    + '• <b>Ambiente</b>: <span style="font-weight:normal;">habilita el sensor de presión/temperatura (LPS22DF)</span><br>'
    + '• <b>IMU XL</b>: <span style="font-weight:normal;">habilita el acelerómetro (LSM6DSOX). Seleccionar ODR y BDR en IMU Config</span><br>'
    + '• <b>IMU GY</b>: <span style="font-weight:normal;">habilita el giróscopo (LSM6DSOX). Seleccionar ODR y BDR en IMU Config</span><br>'
    + '• <b>Ángulo</b>: <span style="font-weight:normal;">habilita el cálculo de inclinación. Requiere ODR de acelerómetro configurado</span><br>'
    + '• <b>LoRaWAN</b>: <span style="font-weight:normal;">habilita la transmisión periódica vía LoRaWAN (LR1110)</span><br>'
    + '• <b>IMU continuo</b>: <span style="font-weight:normal;">habilita el muestreo continuo del acelerómetro/giróscopo. Requiere IMU XL o IMU GY activo. La FIFO se mantiene activa permanentemente en lugar de activarse por deteccion de movimiento</span><br>'
    + '• <b>IMU periodico</b>: <span style="font-weight:normal;">habilita el muestreo periodico del acelerómetro/giróscopo. Requiere IMU XL o IMU GY activo. Cada X tiempo configurado en el periodo, se realiza una medida burst del acelerometro</span><br>';

var INFO_FLAGS_ENVIO = '<b>FLAGS ENVÍO</b><br>'
    + '<span style="font-weight:normal;">IMPORTANTE ACTIVAR EL FLAG DE LORAWAN. Mascara independiente que controla qué bloques de datos se incluyen '
    + 'en el paquete LoRaWAN. Se envía el último dato válido medido de cada módulo seleccionado.</span>';

var INFO_PERIODOS = '<b>PERIODOS — Intervalos de muestreo y envío (segundos)</b><br><br>'
    + '• <b>Medida</b>: <span style="font-weight:normal;">resolución de la máquina de estados. El firmware duerme hasta el próximo módulo; si ninguno está activo o el resultado es 0, usa este valor como tick de fallback</span><br>'
    + '• <b>GPS</b>: <span style="font-weight:normal;">cada cuánto se activa el GPS para obtener posición</span><br>'
    + '• <b>Beacons</b>: <span style="font-weight:normal;">cada cuánto se escanean balizas BLE cercanas</span><br>'
    + '• <b>Ranging</b>: <span style="font-weight:normal;">cada cuánto se mide distancia a otros dispositivos</span><br>'
    + '• <b>Ambiente</b>: <span style="font-weight:normal;">cada cuánto se lee presión/temperatura (LPS22DF)</span><br>'
    + '• <b>Ángulo</b>: <span style="font-weight:normal;">cada cuánto se calcula la inclinación con el IMU</span><br>'
    + '• <b>Envío</b>: <span style="font-weight:normal;">cada cuánto se empaqueta y transmite la última medida válida de los módulos seleccionados</span>';

var INFO_IMU = '<b>IMU CONFIG — LSM6DSOX</b><br><br>'
    + '• <b>XL / GY ODR</b>: <span style="font-weight:normal;">frecuencia de muestreo del acelerómetro y giróscopo</span><br>'
    + '• <b>XL / GY Scale</b>: <span style="font-weight:normal;">escala del acelerómetro y giróscopo (±g / ±dps)</span><br>'
    + '• <b>Umbral Act.</b>: <span style="font-weight:normal;">umbral de detección de actividad. Duración mínima del evento = umbral / XL ODR</span><br>'
    + '• <b>Inact. Dur.</b>: <span style="font-weight:normal;">duración mínima de inactividad = umbral × 512 / XL ODR</span><br>'
    + '• <b>Power Mode</b>: <span style="font-weight:normal;">High Performance / Normal / Ultra Low Power</span><br>'
    + '• <b>FIFO XL/GY BDR</b>: <span style="font-weight:normal;">frecuencia con la que se guardan muestras en el FIFO del sensor</span><br>'
    + '• <b>FIFO Muestras</b>: <span style="font-weight:normal;">watermark del FIFO (nº muestras antes de leer). Ajustar junto con BDR para fijar la ventana de movimiento</span><br>'
    + '• <b>FIFO Mode</b>: <span style="font-weight:normal;">modo de operación del FIFO (BYPASS, FIFO, STREAM…)</span>';

var INFO_BLE = '<b>BLE & RANGING</b><br><br>'
    + '• <b>Scan dur. (s)</b>: <span style="font-weight:normal;">duración de cada ventana de escaneo BLE activo</span><br>'
    + '• <b>Duty (%)</b>: <span style="font-weight:normal;">porcentaje de tiempo que el escáner BLE está activo (50% - escanea la mitad del periodo de Beacons)</span><br>'
    + '• <b>SF</b>: <span style="font-weight:normal;">Spreading Factor LoRa. SF alto = más lento, mayor alcance y consumo</span><br>'
    + '• <b>BW</b>: <span style="font-weight:normal;">ancho de banda LoRa (125/250/500 kHz). BW bajo = más lento, mayor alcance y consumo</span><br>'
    + '• <b>Num. Anclas</b>: <span style="font-weight:normal;">número de anclas desplegadas</span><br>'
    + '• <b>Media Medidas</b>: <span style="font-weight:normal;">medidas por ancla para obtener una distancia robusta</span><br>'
    + '• <b>Anclas Objetivo</b>: <span style="font-weight:normal;">número mínimo de anclas válidas para detener el ranging</span>(3 = posicionamiento 2D, 4 = posicionamiento 3D)<br>'
    + '• <b>TX Power (dBm)</b>: <span style="font-weight:normal;">potencia de transmisión de las peticiones RTToF (rango −17..+22 dBm). Bajarla en distancias cortas evita saturar el receptor</span><br>';

var INFO_ENVIAR_CONFIG = '<b>Actualizar configuración del Logger por BLE</b><br><br>'
    + '<span style="font-weight:normal;">Cuando se pulsa el botón, se construye un paquete hexadecimal en función '
    + 'de los valores de la tabla, que es enviado al logger mediante BLE para actualizar su configuración.</span>';
    
var INFO_LEER_CONFIG = '<b>Leer configuración del Logger por BLE</b><br><br>'
    + '<span style="font-weight:normal;">Cuando se pulsa el botón, se envia un paquete hexadecimal desde el Logger '
    + 'que es decodificado, actualizando la tabla y las variables asociadas.</span>';

// Serialización de la config
// codificarConfig() lee todos los inputs del formulario y construye un ArrayBuffer
// de 39 bytes que refleja exactamente la estructura logger_config_t del firmware:
//
//   Offset  Tamaño  Campo
//   0       2 B     flags          (bitmask: GPS|Beacons|Ranging|Ambiente|IMU_XL|IMU_GY|Angulo|LoRaWAN|IMU_Continuo|IMU_Periodico)
//   2       1 B     flags_envio    (bitmask: GPS|Beacons|Ranging|Ambiente|Angulo)
//   3–18    8×2 B   periodos       (uint16 LE: medida, angulo, gps, ranging, beacons, envio, ambiente, imu_periodico)
//   19–27   9×1 B   IMU config     (ODR/scale XL, umbral, duración, ODR/scale GY, power mode, FIFO BDR×2)
//   28–29   2 B     FIFO muestras  (uint16 LE)
//   30      1 B     FIFO mode
//   31      1 B     duración escaneo BLE (s)
//   32      1 B     duty escaneo BLE (%)
//   33–34   2 B     SF/BW packed   (uint16 LE: [15:12]=SF, [11:0]=BW en kHz)
//   35      1 B     num_anclas
//   36      1 B     media_medidas
//   37      1 B     anclas_objetivo
//   38      1 B     tx_power_dbm
//
// El resultado se devuelve como hex string para enviarlo en el campo params.config del RPC.
function codificarConfig(){
    var buffer = new ArrayBuffer(39);    
    var view = new DataView(buffer);
    var o = 0;
    
    // Flags — reconstruir el byte desde los bits individuales
    var flags =
        (parseInt(document.getElementById('f_gps').value)     << 0) |
        (parseInt(document.getElementById('f_beacons').value) << 1) |
        (parseInt(document.getElementById('f_ranging').value) << 2) |
        (parseInt(document.getElementById('f_ambiente').value)<< 3) |
        (parseInt(document.getElementById('f_imu_xl').value)  << 4) |
        (parseInt(document.getElementById('f_imu_gy').value)  << 5) |
        (parseInt(document.getElementById('f_angulo').value)  << 6) |
        (parseInt(document.getElementById('f_lorawan').value) << 7) |
        (parseInt(document.getElementById('f_imu_continuo').value) << 8)|
        (parseInt(document.getElementById('f_imu_periodico').value) << 9);
    view.setUint16(o, flags,true);
    o += 2;

    // Flags envio
    var flags_envio =
        (parseInt(document.getElementById('fe_gps').value)     << 0) |
        (parseInt(document.getElementById('fe_beacons').value) << 1) |
        (parseInt(document.getElementById('fe_ranging').value) << 2) |
        (parseInt(document.getElementById('fe_ambiente').value)<< 3) |
        (parseInt(document.getElementById('fe_angulo').value)  << 4);
    view.setUint8(o++, flags_envio);

    // Periodos (uint16 LE)
    view.setUint16(o, parseInt(document.getElementById('p_medida').value),   true); o+=2;
    view.setUint16(o, parseInt(document.getElementById('p_angulo').value),   true); o+=2;
    view.setUint16(o, parseInt(document.getElementById('p_gps').value),      true); o+=2;
    view.setUint16(o, parseInt(document.getElementById('p_ranging').value),  true); o+=2;
    view.setUint16(o, parseInt(document.getElementById('p_beacons').value),  true); o+=2;
    view.setUint16(o, parseInt(document.getElementById('p_envio').value),    true); o+=2;
    view.setUint16(o, parseInt(document.getElementById('p_ambiente').value), true); o+=2;
    view.setUint16(o, parseInt(document.getElementById('p_imu_periodico').value), true); o+=2;

    // IMU (uint8)
    view.setUint8(o++, parseInt(document.getElementById('imu_odr_xl').value));
    view.setUint8(o++, parseInt(document.getElementById('imu_scale_xl').value));
    view.setUint8(o++, parseInt(document.getElementById('imu_umbral').value));
    view.setUint8(o++, parseInt(document.getElementById('imu_duracion').value));
    view.setUint8(o++, parseInt(document.getElementById('imu_odr_gy').value));
    view.setUint8(o++, parseInt(document.getElementById('imu_scale_gy').value));
    view.setUint8(o++, parseInt(document.getElementById('imu_power_mode').value));
    view.setUint8(o++, parseInt(document.getElementById('imu_fifo_xl_odr').value));
    view.setUint8(o++, parseInt(document.getElementById('imu_fifo_gy_odr').value));

    // FIFO muestras (uint16 LE)
    view.setUint16(o, parseInt(document.getElementById('imu_fifo_muestras').value), true); o+=2;

    // FIFO mode (uint8)
    view.setUint8(o++, parseInt(document.getElementById('imu_fifo_mode').value));

    // BLE
    view.setUint8(o++, parseInt(document.getElementById('duracion_escaneo').value));
    view.setUint8(o++, parseInt(document.getElementById('duty_escaneo').value));

    // SF/BW (uint16 LE)
    var sf = parseInt(document.getElementById('sf').value);
    var bw = parseInt(document.getElementById('bw').value);
    view.setUint16(o, ((sf & 0x0F) << 12) | (bw & 0xFFF), true); o+=2;
    
    view.setUint8(o++, parseInt(document.getElementById('num_anclas').value));
    view.setUint8(o++, parseInt(document.getElementById('media_medidas').value));
    view.setUint8(o++, parseInt(document.getElementById('anclas_objetivo').value));
    view.setInt8(o++, parseInt(document.getElementById('tx_power_dbm').value));

    // Convertir a hex string
    var bytes = new Uint8Array(buffer);
    return Array.from(bytes).map(function(b) {
        return b.toString(16).padStart(2, '0');
    }).join('');
}

// Deserializa un hex string de 39 bytes (respuesta de get_config) al mismo objeto
// que maneja el formulario. Orden de campos idéntico al struct logger_config_t.
function decodificarConfig(hexStr) {
    var bytes = [];
    for (var i = 0; i < hexStr.length; i += 2) bytes.push(parseInt(hexStr.substr(i, 2), 16));
    var view = new DataView(new Uint8Array(bytes).buffer);
    var o = 0;
    return {
        flags:                    view.getUint16(o,true),
        flags_envio:              view.getUint8(o+=2),
        periodo_medida:           view.getUint16(o+=1, true),
        periodo_angulo:           view.getUint16(o+=2, true),
        periodo_gps:              view.getUint16(o+=2, true),
        periodo_ranging:          view.getUint16(o+=2, true),
        periodo_beacons:          view.getUint16(o+=2, true),
        periodo_envio:            view.getUint16(o+=2, true),
        periodo_ambiente:         view.getUint16(o+=2, true),
        periodo_imu_periodico:    view.getUint16(o+=2, true),
        imu_odr_xl:               view.getUint8(o+=2),
        imu_scale_xl:             view.getUint8(++o),
        imu_umbral_actividad:     view.getUint8(++o),
        imu_duracion_inactividad: view.getUint8(++o),
        imu_odr_gy:               view.getUint8(++o),
        imu_scale_gy:             view.getUint8(++o),
        imu_power_mode:           view.getUint8(++o),
        imu_fifo_xl_odr:          view.getUint8(++o),
        imu_fifo_gy_odr:          view.getUint8(++o),
        imu_fifo_muestras:        view.getUint16(++o, true),
        imu_fifo_mode:            view.getUint8(o+=2),
        duracion_escaneo:         view.getUint8(++o),
        duty_escaneo:             view.getUint8(++o),
        sf_bw:                    view.getUint16(++o, true),
        num_anclas:               view.getUint8(o+=2),         
        media_medidas:            view.getUint8(++o),          
        anclas_objetivo:          view.getUint8(++o),      
        tx_power_dbm:             view.getInt8(++o),
    };
}

// Genera un <select> con las opciones dadas, preseleccionando el valor val
// opciones: array de [valor, etiqueta]
function makeSelect(id, opciones, val) {
    var s = '<select id="' + id + '" style="width:120px;padding:2px;">';
    opciones.forEach(function(o) {
        s += '<option value="' + o[0] + '"' + (o[0] === val ? ' selected' : '') + '>' + o[1] + '</option>';
    });
    return s + '</select>';
}

// Genera un <input type="number"> con el valor inicial val
function makeInput(id, val) {
    return '<input id="' + id + '" type="number" value="' + val + '" style="width:70px;padding:2px;"/>';
}

// Atajo para flags booleanos: select con solo opciones 0 y 1
function makeFlag(id, val) {
    return makeSelect(id, [[0,'0'],[1,'1']], val);
}

// Tablas de opciones para los selects del IMU y LoRa (valores = códigos de registro del LSM6DSOX / SX126x)
var ODR_XL    = [[0,'0 Hz'],[1,'12.5 Hz'],[2,'26 Hz'],[3,'52 Hz'],[4,'104 Hz'],[5,'208 Hz'],[6,'417 Hz'],[7,'833 Hz'],[8,'1.66 kHz'],[9,'3.33 kHz'],[10,'6.66 kHz'],[11,'1.6 Hz']];
var ODR_GY    = [[0,'0 Hz'],[1,'12.5 Hz'],[2,'26 Hz'],[3,'52 Hz'],[4,'104 Hz'],[5,'208 Hz'],[6,'417 Hz'],[7,'833 Hz'],[8,'1.66 kHz'],[9,'3.33 kHz'],[10,'6.66 kHz']];
var SCALE_XL  = [[0,'±2 g'],[1,'±16 g'],[2,'±4 g'],[3,'±8 g']];
var SCALE_GY  = [[0,'±250 dps'],[1,'±125 dps'],[2,'±500 dps'],[4,'±1000 dps'],[6,'±2000 dps']];
var POWER_MODE= [[0,'High Performance'],[1,'Normal / Low Power'],[2,'Ultra Low Power']];
var FIFO_ODR  = [[0,'Not Batched'],[1,'12.5 Hz'],[2,'26 Hz'],[3,'52 Hz'],[4,'104 Hz'],[5,'208 Hz'],[6,'417 Hz'],[7,'833 Hz'],[8,'1.66 kHz'],[9,'3.33 kHz'],[10,'6.66 kHz']];
var FIFO_MODE = [[0,'BYPASS_MODE'],[1,'FIFO_MODE'],[3,'STREAM_TO_FIFO'],[4,'BYPASS_TO_STREAM'],[6,'STREAM_MODE'],[7,'BYPASS_TO_FIFO']];
var SF_OPTS   = [[7,'SF7'],[8,'SF8'],[9,'SF9'],[10,'SF10'],[11,'SF11'],[12,'SF12']];
var BW_OPTS   = [[125,'125 kHz'],[250,'250 kHz'],[500,'500 kHz']];

// Construye el HTML de la tabla de inputs a partir de un objeto config decodificado.
// Si c es null (carga inicial) todos los campos quedan vacíos/en cero hasta que
// cargarAtributos() los rellene con los atributos SERVER_SCOPE del logger.
function construirTabla(c) {
    // Extraer SF y BW del campo empaquetado sf_bw, o usar defaults si no hay config
    var sf = c ? (c.sf_bw >> 12) & 0x0F : 7;
    var bw = c ? c.sf_bw & 0xFFF : 125;

    var html = '<table style="border-collapse:collapse;font-size:12px;width:100%;">';
    
    html += '<tr><th colspan="2" style="text-align:left;padding:4px 8px;background:#f0f0f0;">FLAGS ' + makeInfoBtn('flags', INFO_FLAGS) + '</th>';
    html += '<th colspan="2" style="text-align:left;padding:4px 8px;background:#f0f0f0;">FLAGS ENVIO ' + makeInfoBtn('flags_envio', INFO_FLAGS_ENVIO) + '</th></tr>';
    html += '<tr><td style="padding:2px 8px;">GPS (1/0)</td><td>'     + makeFlag('f_gps',     c ? (c.flags>>0)&1 : 0) + '</td><td style="padding:2px 8px;">GPS (1/0)</td><td>'     + makeFlag('fe_gps',     c ? (c.flags_envio>>0)&1 : 0) + '</td></tr>';
    html += '<tr><td style="padding:2px 8px;">Beacons (1/0)</td><td>' + makeFlag('f_beacons', c ? (c.flags>>1)&1 : 0) + '</td><td style="padding:2px 8px;">Beacons (1/0)</td><td>' + makeFlag('fe_beacons', c ? (c.flags_envio>>1)&1 : 0) + '</td></tr>';
    html += '<tr><td style="padding:2px 8px;">Ranging (1/0)</td><td>' + makeFlag('f_ranging', c ? (c.flags>>2)&1 : 0) + '</td><td style="padding:2px 8px;">Ranging (1/0)</td><td>' + makeFlag('fe_ranging', c ? (c.flags_envio>>2)&1 : 0) + '</td></tr>';
    html += '<tr><td style="padding:2px 8px;">Ambiente (1/0)</td><td>'+ makeFlag('f_ambiente',c ? (c.flags>>3)&1 : 0) + '</td><td style="padding:2px 8px;">Ambiente (1/0)</td><td>'+ makeFlag('fe_ambiente',c ? (c.flags_envio>>3)&1 : 0) + '</td></tr>';
    html += '<tr><td style="padding:2px 8px;">IMU XL (1/0)</td><td>'  + makeFlag('f_imu_xl', c ? (c.flags>>4)&1 : 0) + '</td><td style="padding:2px 8px;">Angulo (1/0)</td><td>'  + makeFlag('fe_angulo',  c ? (c.flags_envio>>4)&1 : 0) + '</td></tr>';
    html += '<tr><td style="padding:2px 8px;">IMU GY (1/0)</td><td>'  + makeFlag('f_imu_gy', c ? (c.flags>>5)&1 : 0) + '</td><td></td><td></td></tr>';
    html += '<tr><td style="padding:2px 8px;">Angulo (1/0)</td><td>'  + makeFlag('f_angulo',  c ? (c.flags>>6)&1 : 0) + '</td><td></td><td></td></tr>';
    html += '<tr><td style="padding:2px 8px;">LoRaWAN (1/0)</td><td>' + makeFlag('f_lorawan', c ? (c.flags>>7)&1 : 0) + '</td><td></td><td></td></tr>';
    html += '<tr><td style="padding:2px 8px;">IMU Continuo (1/0)</td><td>' + makeFlag('f_imu_continuo', c ? (c.flags>>8)&1 : 0) + '</td><td></td><td></td></tr>';
    html += '<tr><td style="padding:2px 8px;">IMU Periodico (1/0)</td><td>' + makeFlag('f_imu_periodico', c ? (c.flags>>9)&1 : 0) + '</td><td></td><td></td></tr>';

    html += '<tr><th colspan="4" style="text-align:left;padding:4px 8px;background:#f0f0f0;">PERIODOS (s) ' + makeInfoBtn('periodos', INFO_PERIODOS) + '</th></tr>';
    html += '<tr><td style="padding:2px 8px;">Medida</td><td>'  + makeInput('p_medida',  c ? c.periodo_medida  : '') + '</td><td style="padding:2px 8px;">Envio</td><td>'   + makeInput('p_envio',   c ? c.periodo_envio   : '') + '</td></tr>';
    html += '<tr><td style="padding:2px 8px;">GPS</td><td>'     + makeInput('p_gps',     c ? c.periodo_gps     : '') + '</td><td style="padding:2px 8px;">Ambiente</td><td>'+ makeInput('p_ambiente', c ? c.periodo_ambiente : '') + '</td></tr>';
    html += '<tr><td style="padding:2px 8px;">Beacons</td><td>' + makeInput('p_beacons', c ? c.periodo_beacons : '') + '</td><td style="padding:2px 8px;">Angulo</td><td>' + makeInput('p_angulo',   c ? c.periodo_angulo   : '') + '</td></tr>';
    
    html += '<tr><td style="padding:2px 8px;">Ranging</td><td>' + makeInput('p_ranging', c ? c.periodo_ranging : '') + '</td><td style="padding:2px 8px;">IMU Periodico</td><td>' + makeInput('p_imu_periodico', c ? c.periodo_imu_periodico : '') + '</td></tr>';

    html += '<tr><th colspan="4" style="text-align:left;padding:4px 8px;background:#f0f0f0;">IMU CONFIG ' + makeInfoBtn('imu', INFO_IMU) + '</th></tr>';
    html += '<tr><td style="padding:2px 8px;">XL ODR</td><td>'       + makeSelect('imu_odr_xl',       ODR_XL,     c ? c.imu_odr_xl              : 0) + '</td><td style="padding:2px 8px;">GY ODR</td><td>'       + makeSelect('imu_odr_gy',       ODR_GY,     c ? c.imu_odr_gy              : 0) + '</td></tr>';
    html += '<tr><td style="padding:2px 8px;">XL Scale</td><td>'     + makeSelect('imu_scale_xl',     SCALE_XL,   c ? c.imu_scale_xl            : 0) + '</td><td style="padding:2px 8px;">GY Scale</td><td>'     + makeSelect('imu_scale_gy',     SCALE_GY,   c ? c.imu_scale_gy            : 0) + '</td></tr>';
    html += '<tr><td style="padding:2px 8px;">Umbral Act.</td><td>'  + makeInput('imu_umbral',                    c ? c.imu_umbral_actividad    : '') + '</td><td style="padding:2px 8px;">Power Mode</td><td>'  + makeSelect('imu_power_mode',   POWER_MODE, c ? c.imu_power_mode          : 0) + '</td></tr>';
    html += '<tr><td style="padding:2px 8px;">Inact. Dur.</td><td>'  + makeInput('imu_duracion',                  c ? c.imu_duracion_inactividad: '') + '</td><td style="padding:2px 8px;">FIFO XL BDR</td><td>' + makeSelect('imu_fifo_xl_odr',  FIFO_ODR,   c ? c.imu_fifo_xl_odr         : 0) + '</td></tr>';
    html += '<tr><td style="padding:2px 8px;">FIFO Muestras</td><td>'+ makeInput('imu_fifo_muestras',              c ? c.imu_fifo_muestras       : '') + '</td><td style="padding:2px 8px;">FIFO GY BDR</td><td>' + makeSelect('imu_fifo_gy_odr',  FIFO_ODR,   c ? c.imu_fifo_gy_odr         : 0) + '</td></tr>';
    html += '<tr><td style="padding:2px 8px;">FIFO Mode</td><td>'    + makeSelect('imu_fifo_mode',     FIFO_MODE,  c ? c.imu_fifo_mode           : 0) + '</td><td></td><td></td></tr>';

    html += '<tr><th colspan="4" style="text-align:left;padding:4px 8px;background:#f0f0f0;">BLE & RANGING ' + makeInfoBtn('ble', INFO_BLE) + '</th></tr>';
    html += '<tr><td style="padding:2px 8px;">Scan dur. (s)</td><td>'+ makeInput('duracion_escaneo', c ? c.duracion_escaneo : '') + '</td><td style="padding:2px 8px;">SF</td><td>' + makeSelect('sf', SF_OPTS, sf) + '</td></tr>';
    html += '<tr><td style="padding:2px 8px;">Duty (%)</td><td>'     + makeInput('duty_escaneo',     c ? c.duty_escaneo    : '') + '</td><td style="padding:2px 8px;">BW (kHz)</td><td>' + makeSelect('bw', BW_OPTS, bw) + '</td></tr>';
    
    html += '<tr><td style="padding:2px 8px;">Num. Anclas</td><td>'+ makeInput('num_anclas',       c ? c.num_anclas      : '') + '</td><td style="padding:2px 8px;">Media Medidas</td><td>'+ makeInput('media_medidas',    c ? c.media_medidas   : '') + '</td></tr>';
    html += '<tr><td style="padding:2px 8px;">Anclas Objetivo</td><td>' + makeInput('anclas_objetivo', c ? c.anclas_objetivo : '') 
    + '</td><td style="padding:2px 8px;">TX Power (dBm)</td><td>' + makeInput('tx_power_dbm', c ? c.tx_power_dbm : '') + '</td></tr>';
    html += '</table>';
    return html;
}

// Reemplaza el contenido del div tabla-config con la tabla generada a partir de config
function rellenarFormulario(config) {
    document.getElementById('tabla-config').innerHTML = construirTabla(config);
}

// Lee todos los inputs del formulario y devuelve un objeto con los mismos campos
// que logger_config_t. Se usa tanto para previsualizar como para set_config y
// para guardar los atributos SERVER_SCOPE tras un envío exitoso.
function leerFormulario() {
    var flags =
        (parseInt(document.getElementById('f_gps').value)     << 0) |
        (parseInt(document.getElementById('f_beacons').value) << 1) |
        (parseInt(document.getElementById('f_ranging').value) << 2) |
        (parseInt(document.getElementById('f_ambiente').value)<< 3) |
        (parseInt(document.getElementById('f_imu_xl').value)  << 4) |
        (parseInt(document.getElementById('f_imu_gy').value)  << 5) |
        (parseInt(document.getElementById('f_angulo').value)  << 6) |
        (parseInt(document.getElementById('f_lorawan').value) << 7) |
        (parseInt(document.getElementById('f_imu_continuo').value) << 8)|
        (parseInt(document.getElementById('f_imu_periodico').value) << 9);

    var flags_envio =
        (parseInt(document.getElementById('fe_gps').value)     << 0) |
        (parseInt(document.getElementById('fe_beacons').value) << 1) |
        (parseInt(document.getElementById('fe_ranging').value) << 2) |
        (parseInt(document.getElementById('fe_ambiente').value)<< 3) |
        (parseInt(document.getElementById('fe_angulo').value)  << 4);

    var sf = parseInt(document.getElementById('sf').value);
    var bw = parseInt(document.getElementById('bw').value);

    return {
        flags:                    flags,
        flags_envio:              flags_envio,
        periodo_medida:           parseInt(document.getElementById('p_medida').value),
        periodo_angulo:           parseInt(document.getElementById('p_angulo').value),
        periodo_gps:              parseInt(document.getElementById('p_gps').value),
        periodo_ranging:          parseInt(document.getElementById('p_ranging').value),
        periodo_beacons:          parseInt(document.getElementById('p_beacons').value),
        periodo_envio:            parseInt(document.getElementById('p_envio').value),
        periodo_ambiente:         parseInt(document.getElementById('p_ambiente').value),
        periodo_imu_periodico:    parseInt(document.getElementById('p_imu_periodico').value), 
        imu_odr_xl:               parseInt(document.getElementById('imu_odr_xl').value),
        imu_scale_xl:             parseInt(document.getElementById('imu_scale_xl').value),
        imu_umbral_actividad:     parseInt(document.getElementById('imu_umbral').value),
        imu_duracion_inactividad: parseInt(document.getElementById('imu_duracion').value),
        imu_odr_gy:               parseInt(document.getElementById('imu_odr_gy').value),
        imu_scale_gy:             parseInt(document.getElementById('imu_scale_gy').value),
        imu_power_mode:           parseInt(document.getElementById('imu_power_mode').value),
        imu_fifo_xl_odr:          parseInt(document.getElementById('imu_fifo_xl_odr').value),
        imu_fifo_gy_odr:          parseInt(document.getElementById('imu_fifo_gy_odr').value),
        imu_fifo_muestras:        parseInt(document.getElementById('imu_fifo_muestras').value),
        imu_fifo_mode:            parseInt(document.getElementById('imu_fifo_mode').value),
        duracion_escaneo:         parseInt(document.getElementById('duracion_escaneo').value),
        duty_escaneo:             parseInt(document.getElementById('duty_escaneo').value),
        sf:                       sf,
        bw:                       bw,
        sf_bw:                    ((sf & 0x0F) << 12) | (bw & 0xFFF),
        num_anclas:       parseInt(document.getElementById('num_anclas').value),
        media_medidas:    parseInt(document.getElementById('media_medidas').value),
        anclas_objetivo:  parseInt(document.getElementById('anclas_objetivo').value),
        tx_power_dbm:     parseInt(document.getElementById('tx_power_dbm').value),
    };
}

// Polling de DOM: espera a que el elemento con id exista antes de ejecutar callback.
// Necesario porque el HTML del widget se inyecta de forma asíncrona y los ids
// pueden no estar disponibles en el momento de la llamada inicial.
function esperarElemento(id, callback) {
    var el = document.getElementById(id);
    if (el) {
        callback();
    } else {
        setTimeout(function() { esperarElemento(id, callback); }, 100);
    }
}

// Lee los atributos SERVER_SCOPE del logger y pre-rellena el formulario con ellos.
// Estos atributos se guardan cada vez que se hace get_config o set_config con éxito,
// de modo que al abrir el widget siempre muestra la última config conocida del logger.
async function cargarAtributos() {
    var loggerEntityId = data[0]['entityId'];
    try {
        var resp = await http.get(
            '/api/plugins/telemetry/DEVICE/' + loggerEntityId + '/values/attributes/SERVER_SCOPE'
        ).toPromise();

        if (resp && resp.length > 0) {
            var attrs = {};
            resp.forEach(function(a) { attrs[a.key] = a.value; });

            var config = {
                flags:                    attrs.flags          || 0,
                flags_envio:              attrs.flags_envio    || 0,
                periodo_medida:           attrs.periodo_medida           || 0,
                periodo_angulo:           attrs.periodo_angulo           || 0,
                periodo_gps:              attrs.periodo_gps              || 0,
                periodo_ranging:          attrs.periodo_ranging          || 0,
                periodo_beacons:          attrs.periodo_beacons          || 0,
                periodo_envio:            attrs.periodo_envio            || 0,
                periodo_ambiente:         attrs.periodo_ambiente         || 0,
                periodo_imu_periodico:    attrs.periodo_imu_periodico    || 0,
                imu_odr_xl:               attrs.imu_odr_xl               || 0,
                imu_scale_xl:             attrs.imu_scale_xl             || 0,
                imu_umbral_actividad:     attrs.imu_umbral_actividad     || 0,
                imu_duracion_inactividad: attrs.imu_duracion_inactividad || 0,
                imu_odr_gy:               attrs.imu_odr_gy               || 0,
                imu_scale_gy:             attrs.imu_scale_gy             || 0,
                imu_power_mode:           attrs.imu_power_mode           || 0,
                imu_fifo_xl_odr:          attrs.imu_fifo_xl_odr          || 0,
                imu_fifo_gy_odr:          attrs.imu_fifo_gy_odr          || 0,
                imu_fifo_muestras:        attrs.imu_fifo_muestras        || 0,
                imu_fifo_mode:            attrs.imu_fifo_mode            || 0,
                duracion_escaneo:         attrs.duracion_escaneo         || 0,
                duty_escaneo:             attrs.duty_escaneo             || 0,
                sf_bw: (((attrs.sf || 7) & 0x0F) << 12) | ((attrs.bw || 125) & 0xFFF),
                num_anclas:       attrs.num_anclas      || 0,
                media_medidas:    attrs.media_medidas   || 0,
                anclas_objetivo:  attrs.anclas_objetivo || 0,
                tx_power_dbm:     attrs.tx_power_dbm !== undefined ? attrs.tx_power_dbm : 0,
            };

            rellenarFormulario(config);
        }
    } catch(e) {
        console.log('Error cargando atributos:', e);
    }
}


// Ejecuta un comando RPC sobre el logger a través del mejor gateway disponible
// y espera confirmación leyendo el telemetry "last_response" del gateway.
//
// Flujo: widget - TB API REST - EMQX - gateway (MicroPython)
//   TB publica el RPC en EMQX bajo el topic: v1/devices/me/rpc/request/{id}
//   EMQX enruta el mensaje al gateway, que está suscrito a ese topic,
//   recibe el JSON {method, params} y encola el comando en la ColaBLE para ejecutarlo por BLE.
//   La confirmación llega de forma asíncrona vía telemetry "last_response" (ver paso 3).
//
// cmd: 'get_config' | 'set_config' (con payload config) | otros comandos genéricos
async function ejecutarComando(cmd) {
    var btn = document.getElementById('btn-' + cmd);
    var resultado = document.getElementById('resultado');
    var gw = null;

    btn.disabled = true;
    resultado.textContent = '';

    try {
        if (cmd === 'set_config') {
            var imuContinuo  = parseInt(document.getElementById('f_imu_continuo').value);
            var imuPeriodico = parseInt(document.getElementById('f_imu_periodico').value);
            if (imuContinuo && imuPeriodico) {
                resultado.style.color = '#ef4444';
                resultado.textContent = '✗ IMU Continuo e IMU Periodico son excluyentes';
                return;
            }
        }
        
         console.log('[set_config] paquete generado:', codificarConfig());
        
        // Primero se localiza el gateway con mejor señal
        btn.textContent = 'Buscando gateway...';
        gw = await obtenerMejorGateway();
        if (!gw) {
            resultado.style.color = '#ef4444';
            resultado.textContent = '✗ Ningún gateway ve al logger';
            return;
        }

        document.getElementById('gw-usado').textContent = gw.name;
        
        // Se construiye params y enviar RPC oneway
        btn.textContent = 'Enviando comando...';
        var params = { cmd: cmd, mac: formatearMac(mac_logger) };
        if (cmd === 'set_config') {
            params.config = codificarConfig();
        }
        await http.post('/api/rpc/oneway/' + gw.id, {
            method: cmd,
            params: JSON.stringify(params),
            timeout: 5000
        }).toPromise();

        // Polling de "last_response" hasta recibir confirmación
        // Menor número de intentos que en el widget de comandos (40 vs 200)
        // porque get/set_config es más rápido que una descarga completa de flash.
        btn.textContent = 'Esperando respuesta...';
        var intentos = 0;
        while (intentos < 40) {
            await new Promise(resolve => setTimeout(resolve, 1500));
            var resp = await http.get(
                '/api/plugins/telemetry/DEVICE/' + gw.id + '/values/timeseries?keys=last_response'
            ).toPromise();
            
            var raw = resp?.last_response?.[0]?.value;
            if (raw) {
                var r = JSON.parse(raw);
                var macNormalizada = r.mac ? r.mac.replace(/:/g, '').toUpperCase() : '';
                // Verificar que la respuesta pertenece a este comando y a este logger
                if (r.cmd === cmd && macNormalizada === mac_logger.toUpperCase()) {
                    if (cmd === 'get_config' && r.status === 'success') {
                        var config = decodificarConfig(r.data.config);
                        config.sf  = (config.sf_bw >> 12) & 0x0F;
                        config.bw  = config.sf_bw & 0xFFF;
                        rellenarFormulario(config);
                        delete config.sf_bw;
                        
                        // Persistir la config leída como atributos SERVER_SCOPE del logger
                        // para que cargarAtributos() la muestre la próxima vez que se abra el widget
                        var loggerEntityId = data[0]['entityId'];
                        await http.post(
                            '/api/plugins/telemetry/DEVICE/' + loggerEntityId + '/SERVER_SCOPE',
                            config
                        ).toPromise();

                        resultado.style.color = '#4caf50';
                        resultado.textContent = '✓ Configuración leída';
                    } else if (cmd === 'set_config' && r.status === 'success'){
                        // Confirmar la config enviada guardándola también en SERVER_SCOPE
                        // para mantener TB sincronizado con el estado real del logger
                        var attrs = leerFormulario();
                        delete attrs.sf_bw;
                        var loggerEntityId = data[0]['entityId'];
                        
                        await http.post('/api/plugins/telemetry/DEVICE/' + loggerEntityId + '/SERVER_SCOPE', attrs).toPromise();
                        
                        resultado.style.color = '#4caf50';
                        resultado.textContent = '✓ Configuración actualizada';
                    
                    } else {
                        resultado.style.color = r.status === 'success' ? '#4caf50' : '#ef4444';
                        resultado.textContent = r.status === 'success' ? '✓ ' + cmd + ' completado' : '✗ Error: ' + r.data;
                    }
                    return;
                }
            }
            intentos++;
        }

        resultado.style.color = '#ef4444';
        resultado.textContent = '✗ Timeout esperando respuesta';

    } catch(e) {
        resultado.style.color = '#ef4444';
        resultado.textContent = '✗ Error: ' + e.message;
    } finally {
        btn.disabled = false;
        btn.textContent = btn.dataset.label;
    }
}

// Exponer al scope global para que los onclick del HTML generado puedan llamarla
window.ejecutarComando = ejecutarComando;

// ── HTML ──────────────────────────────────────────────────────────────────────

var html = '<div style="font-family:sans-serif;padding:12px;">';
html += '<div style="margin-bottom:10px;font-size:12px;color:#666;">Logger: <b>' + mac_logger + '</b> · Gateway: <b><span id="gw-usado">—</span></b></div>';
html += '<button id="btn-get_config" data-label="Leer Config" onclick="window.ejecutarComando(\'get_config\')" style="padding:8px 16px;background:#3b82f6;color:white;border:none;border-radius:6px;cursor:pointer;margin-bottom:12px;">Leer Config</button>'+ makeInfoBtn('btn-get_config', INFO_LEER_CONFIG) + '</th>';
html += '<button id="btn-set_config" data-label="Enviar Config" onclick="window.ejecutarComando(\'set_config\')" style="padding:8px 16px;background:#10b981;color:white;border:none;border-radius:6px;cursor:pointer;margin-bottom:12px;margin-left:8px;">Enviar Config</button>'+ makeInfoBtn('btn-set_config', INFO_ENVIAR_CONFIG) + '</th>';
html += '<div id="resultado" style="margin-bottom:12px;font-size:13px;min-height:20px;"></div>';
html += '<div id="tabla-config">' + construirTabla(null) + '</div>';
html += '</div>';

// setTimeout(0) garantiza que el DOM del widget esté completamente insertado
// antes de que esperarElemento() empiece a buscar 'tabla-config'
setTimeout(function() { esperarElemento('tabla-config', cargarAtributos); }, 0);
return html;