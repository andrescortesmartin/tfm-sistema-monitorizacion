var d = data[0];
var entityId = d['entityId']; // id del dispositivo destino de los RPC
var http = ctx.http;

// Estado del modo autonomo: si el atributo no viene o es false, se considera activo
var pausaAutonomo = d['pausa_autonomo'];
var modoActivo = pausaAutonomo === undefined || pausaAutonomo === false || pausaAutonomo === 'false';

// Envia un comando al gateway.
//   method: comando
//   valueId: id del <input> del que leer el valor (o null si el comando no lleva parametro)
//   isNumber: true para parsear el valor como entero
window.sendRPC = function(method, valueId, isNumber) {
    var btn = document.getElementById('btn-' + (valueId || method));
    var params;

    if (valueId) {
        var input = document.getElementById(valueId);
        var value = isNumber ? parseInt(input.value) : input.value;

        // La clave del parametro dentro del payload depende del comando
        var key;
        if (method === 'update_download_time') {
            key = 'time';
        } else if (method === 'update_rssi_filter') {
            key = 'rssi';
        } else {
            key = 'mac';
        }

        // Payload que espera el firmware: { cmd: <method>, <key>: <value> }
        var payload = {cmd: method};
        payload[key] = value;
        params = JSON.stringify(payload);
    } else {
        params = JSON.stringify({cmd: method});
    }

    // Feedback visual: se deshabilita el boton mientras se envia
    if (btn) {
        btn.disabled = true;
        btn.textContent = 'Enviando...';
    }

    // RPC one-way: no se espera respuesta del dispositivo, solo la confirmacion de encolado
    http.post('/api/rpc/oneway/' + entityId, {
        method: method,
        params: params,
        timeout: 5000
    }).subscribe(
        function(r) {
            // Envio aceptado: marca el boton en verde y lo restaura a los 2 s
            if (btn) {
                btn.textContent = '✓ Enviado';
                btn.style.background = '#4caf50';
                setTimeout(function() {
                    var isRed = method === 'reboot' || method === 'restart_scan';
                    btn.textContent = method === 'reboot' ? 'Reboot' : method === 'restart_scan' ? 'Reiniciar' : 'Enviar';
                    btn.style.background = isRed ? '#ef4444' : '#3b82f6';
                    btn.disabled = false;
                }, 2000);
            }
        },
        function(e) {
            // Fallo en el envio: marca el boton en rojo y lo restaura a los 2 s
            if (btn) {
                btn.textContent = '✗ Error';
                btn.style.background = '#ef4444';
                setTimeout(function() {
                    var isRed = method === 'reboot' || method === 'restart_scan';
                    btn.textContent = method === 'reboot' ? 'Reboot' : method === 'restart_scan' ? 'Reiniciar' : 'Enviar';
                    btn.style.background = isRed ? '#ef4444' : '#3b82f6';
                    btn.disabled = false;
                }, 2000);
            }
        }
    );
};

//  Construccion del HTML del panel
var html = '<div style="padding:1rem; font-family:Roboto, sans-serif; display:flex; flex-direction:column; gap:12px;">';

// Cabecera
html += '<div style="display:flex; justify-content:space-between; align-items:center; border-bottom:0.5px solid rgba(0,0,0,0.1); padding-bottom:10px;">';
html += '<span style="font-size:15px; font-weight:500;">Configuración Gateway</span>';
html += '</div>';

// Genera una fila: etiqueta + input opcional + boton que llama a sendRPC()
function cmdRow(labelStr, inputHtml, btnId, btnOnclick, btnLabel, btnRed) {
    var btnBg = btnRed ? '#ef4444' : '#3b82f6';
    var row = '<div style="background:rgba(0,0,0,0.04); border-radius:8px; padding:10px 12px; display:flex; align-items:center; gap:10px;">';
    row += '<span style="font-size:11px; opacity:0.5; text-transform:uppercase; letter-spacing:0.4px; width:150px; flex-shrink:0;">' + labelStr + '</span>';
    if (inputHtml) row += inputHtml;
    row += '<button id="' + btnId + '" onclick="' + btnOnclick + '" style="padding:5px 14px; background:' + btnBg + '; color:white; border:none; border-radius:6px; font-size:12px; cursor:pointer; white-space:nowrap;">' + btnLabel + '</button>';
    row += '</div>';
    return row;
}

var inputStyle = 'style="flex:1; padding:5px 8px; border:1px solid rgba(0,0,0,0.15); border-radius:6px; font-size:13px; outline:none; background:white;"';

html += cmdRow(
    'Periodo descarga (s)',
    '<input type="number" id="interval" min="60" max="3600" placeholder="300" ' + inputStyle + '/>',
    'btn-interval', 'sendRPC(\'update_download_time\', \'interval\', true)', 'Enviar', false
);

html += cmdRow(
    'Añadir filtro MAC',
    '<input type="text" id="add_mac" placeholder="aa:bb:cc:dd:ee:ff" ' + inputStyle + '/>',
    'btn-add_mac', 'sendRPC(\'add_mac\', \'add_mac\', false)', 'Enviar', false
);

html += cmdRow(
    'Eliminar filtro MAC',
    '<input type="text" id="remove_mac" placeholder="aa:bb:cc:dd:ee:ff" ' + inputStyle + '/>',
    'btn-remove_mac', 'sendRPC(\'remove_mac\', \'remove_mac\', false)', 'Enviar', false
);

html += cmdRow(
    'Filtro RSSI',
    '<input type="number" id="update_rssi_filter" placeholder="-75" ' + inputStyle + '/>',
    'btn-update_rssi_filter', 'sendRPC(\'update_rssi_filter\', \'update_rssi_filter\', true)', 'Enviar', false
);

// Modo autónomo (checkbox): al cambiar envia pausa_off/pausa_on y actualiza la etiqueta
html += '<div style="background:rgba(0,0,0,0.04); border-radius:8px; padding:10px 12px; display:flex; align-items:center; gap:10px;">';
html += '<span style="font-size:11px; opacity:0.5; text-transform:uppercase; letter-spacing:0.4px; width:150px; flex-shrink:0;">Modo autónomo</span>';
html += '<label style="display:flex; align-items:center; gap:8px; cursor:pointer; flex:1;">';
html += '<input type="checkbox" id="debug_switch" ' + (modoActivo ? 'checked' : '') + ' onchange="';
html += 'var cmd = this.checked ? \'pausa_off\' : \'pausa_on\';';
html += 'sendRPC(cmd, null, false);';
html += 'document.getElementById(\'debug_label\').textContent = this.checked ? \'Activado\' : \'Desactivado\';';
html += 'document.getElementById(\'debug_label\').style.color = this.checked ? \'#3b82f6\' : \'#666\';';
html += '" style="width:18px; height:18px; cursor:pointer;"/>';
html += '<span id="debug_label" style="font-size:13px; font-weight:600; color:' + (modoActivo ? '#3b82f6' : '#666') + ';">';
html += (modoActivo ? 'Activado' : 'Desactivado');
html += '</span>';
html += '</label>';
html += '</div>';

// Reiniciar scan
html += cmdRow(
    'Reiniciar scan BLE',
    null,
    'btn-restart_scan', 'sendRPC(\'restart_scan\', null, false)', 'Reiniciar', true
);

// Reboot
html += cmdRow(
    'Reiniciar gateway',
    null,
    'btn-reboot', 'sendRPC(\'reboot\', null, false)', 'Reboot', true
);

html += '</div>';
return html;