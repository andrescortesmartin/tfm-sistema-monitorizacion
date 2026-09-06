var mac_logger = data[0]['entityName']; // el nombre de la entidad es la MAC del logger (sin ":")
var http = ctx.http;

// "aabbcc..." a "AA:BB:CC:..."
function formatearMac(mac) {
    return mac.match(/.{1,2}/g).join(':').toUpperCase();
}

// Busca, entre todos los gateways Cassia, el que ve al logger con mejor RSSI.
// Cada gateway publica en "visibles" la lista de dispositivos BLE que detecta.
async function obtenerMejorGateway() {
    // Consulta a la API de entidades de ThingsBoard:
    var resp = await http.post('/api/entitiesQuery/find?pageSize=100&page=0', {
        entityFilter: { // Que entidades mirar: los dispositivos con el perfil "Profile-gateway-cassia_gw"
            type: "deviceType",
            deviceTypes: ["Profile-gateway-cassia_gw"]
        },
        entityFields: [ // Que campos propios de la entidad devolver (aqui solo el nombre)
            { type: "ENTITY_FIELD", key: "name" }
        ],
        latestValues: [ // Que ultimo valor de telemetria/atributo adjuntar (aqui la serie temporal "visibles")
            { type: "TIME_SERIES", key: "visibles" }
        ],
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
                // Comparamos MACs normalizadas (sin ":" y en mayusculas)
                var macNormalizada = v.mac.replace(/:/g, '').toUpperCase();
                if (macNormalizada === mac_logger.toUpperCase() && v.rssi > mejorRssi) {
                    mejorRssi = v.rssi;
                    mejorGw = { id: gw.entityId.id, name: gw.latest.ENTITY_FIELD.name.value, rssi: v.rssi };
                }
            }
        } catch(e) {}
    }
    return mejorGw;
}

// Flujo completo de un comando: elige gateway, envia RPC y espera la respuesta del nodo (que llega como nueva telemetria "last_response" del gateway).
async function ejecutarComando(cmd) {
    var btn = document.getElementById('btn-' + cmd);
    var resultado = document.getElementById('resultado');

    btn.disabled = true;
    resultado.textContent = '';

    try {
        // Localizar el gateway con mejor cobertura sobre este logger
        btn.textContent = 'Buscando gateway...';
        var gw = await obtenerMejorGateway();

        if (!gw) {
            document.getElementById('gw-nombre').textContent = 'Sin gateway';
            document.getElementById('gw-nombre').style.color = '#d94f4f';
            resultado.style.color = '#d94f4f';
            resultado.textContent = 'Ningun gateway ve al logger';
            return;
        }

        document.getElementById('gw-nombre').textContent = gw.name;
        document.getElementById('gw-nombre').style.color = '#2d9e6b';
        document.getElementById('gw-rssi').textContent = gw.rssi + ' dBm';

        // Capturar el ts de la última respuesta conocida ANTES de enviar el comando.
        // Se usa este valor (mismo reloj/origen que el que llegará después) como referencia,
        // en vez de Date.now() del navegador, que puede no estar sincronizado con el
        // reloj que genera el ts de last_response.
        var respPrevia = await http.get(
            '/api/plugins/telemetry/DEVICE/' + gw.id + '/values/timeseries?keys=last_response'
        ).toPromise();
        var tsPrevio = respPrevia?.last_response?.[0]?.ts || 0;

        // Enviar el comando por RPC one-way al gateway
        btn.textContent = 'Enviando...';
        var tsAntes = Date.now();
        await http.post('/api/rpc/oneway/' + gw.id, {
            method: cmd,
            params: JSON.stringify({ cmd: cmd, mac: formatearMac(mac_logger) }),
            timeout: 5000
        }).toPromise();

        // Consultar "last_response" hasta ver una respuesta nueva (ts > tsPrevio) que corresponda a este comando y a esta MAC. Hasta 200 intentos x 1,5 s.
        btn.textContent = 'Esperando...';

        var intentos = 0;
        while (intentos < 200) {
            await new Promise(resolve => setTimeout(resolve, 1500));
            var resp = await http.get(
                '/api/plugins/telemetry/DEVICE/' + gw.id + '/values/timeseries?keys=last_response'
            ).toPromise();

            var tsResp = resp?.last_response?.[0]?.ts;
            var raw = resp?.last_response?.[0]?.value;

            if (raw && tsResp > tsPrevio) {
                var r = JSON.parse(raw);
                var macNorm = r.mac ? r.mac.replace(/:/g, '').toUpperCase() : '';
                if (r.cmd === cmd && macNorm === mac_logger.toUpperCase()) {
                    resultado.style.color = r.status === 'success' ? '#2d9e6b' : '#d94f4f';
                    resultado.textContent = r.status === 'success' ? cmd + ' completado' : 'Error: ' + r.data;
                    return;
                }
            }
            intentos++;
        }

        resultado.style.color = '#d94f4f';
        resultado.textContent = 'Timeout esperando respuesta';

    } catch(e) {
        resultado.style.color = '#d94f4f';
        resultado.textContent = 'Error: ' + e.message;
    } finally {
        // Rehabilitar el boton y restaurar su etiqueta original (guardada en data-label)
        btn.disabled = false;
        btn.dataset.label && (btn.textContent = btn.dataset.label);
    }
}

window.ejecutarComando = ejecutarComando;

// HTML del panel: cabecera, gateway activo, botones normales y botones destructivos
return '<div style="padding:1rem; display:flex; flex-direction:column; gap:12px; font-family:Roboto, sans-serif;">' +

    '<span style="font-size:15px; font-weight:500; border-bottom:0.5px solid rgba(0,0,0,0.1); padding-bottom:10px;">Comandos</span>' +

    '<div style="background:rgba(0,0,0,0.04); border-radius:8px; padding:12px; display:flex; justify-content:space-between; align-items:center;">' +
        '<div>' +
            '<div style="font-size:11px; opacity:0.5; text-transform:uppercase; letter-spacing:0.05em; margin-bottom:4px;">Gateway activo</div>' +
            '<div style="font-size:15px; font-weight:500; color:#333;" id="gw-nombre">—</div>' +
        '</div>' +
        '<div style="font-size:12px; color:#888;" id="gw-rssi"></div>' +
    '</div>' +

    '<div style="display:grid; grid-template-columns:1fr 1fr; gap:8px;">' +
        '<button id="btn-download" data-label="Descargar" onclick="window.ejecutarComando(\'download\')" style="padding:10px; background:#1a73e8; color:#fff; border:none; border-radius:8px; font-size:13px; font-weight:500; cursor:pointer;">Descargar</button>' +
        '<button id="btn-sync_time" data-label="Sincronizar Hora" onclick="window.ejecutarComando(\'sync_time\')" style="padding:10px; background:transparent; color:#1a73e8; border:1.5px solid #1a73e8; border-radius:8px; font-size:13px; font-weight:500; cursor:pointer;">Sincronizar Hora</button>' +
    '</div>' +

    '<div style="border-top:1px dashed rgba(217,79,79,0.3); padding-top:10px; display:grid; grid-template-columns:1fr 1fr; gap:8px;">' +
        '<button id="btn-erase" data-label="Borrar Flash" onclick="window.ejecutarComando(\'erase\')" style="padding:10px; background:transparent; color:#d94f4f; border:1.5px solid rgba(217,79,79,0.4); border-radius:8px; font-size:13px; font-weight:500; cursor:pointer; opacity:0.8;">Borrar Flash</button>' +
        '<button id="btn-reset" data-label="Reset" onclick="window.ejecutarComando(\'reset\')" style="padding:10px; background:transparent; color:#d94f4f; border:1.5px solid rgba(217,79,79,0.4); border-radius:8px; font-size:13px; font-weight:500; cursor:pointer; opacity:0.8;">Reset</button>' +
    '</div>' +

    '<div id="resultado" style="font-size:12px; min-height:18px; text-align:center;"></div>' +

'</div>';