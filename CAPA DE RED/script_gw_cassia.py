"""Script del gateway Cassia: puente BLE - MQTT para los nodos logger.

Se ejecuta dentro del contenedor del gateway Cassia (entorno MicroPython) y
cumple dos funciones:

  * Modo autónomo: escanea de forma continua las MACs conocidas, y cuando
    detecta un logger con datos pendientes (segun el timestamp de ultima
    descarga que anuncia en el adData) lo conecta, descarga la flash, la
    borra y republica los sectores por MQTT.
  * Modo manual: atiende comandos recibidos por MQTT (connect, download,
    get_config/set_config, reset, erase, sync_time, gestion de MACs y
    umbrales), encolandolos para ejecutarlos de forma serializada.

El acceso al chip BLE esta serializado en ble_worker(): el bucle de escaneo
cede el chip (worker_ocupado) mientras hay un job en curso.
"""

import asyncio
import gc
import json
import struct
import time

import cassiablue
from cassiamqtt import CassiaMQTTClient
from cassiacommon import AsyncQueue
import cassiasys

KNOWN_MAC_PREFIXES = ["44:31:48:4F*"] # Variable para almacenar MACs de escaneo
UMBRAL_DESCARGA = 300  # Umbral de descarga autonoma en segundos
UMBRAL_RSSI = -75  # dBm mínimo para descargar
CONFIG_SIZE = 39 # sizeof(logger_config_t), actualizar si se cambia la estructura en el firmware del dispositivo

# Tamaño máximo de los mensajes publicados en MQTT (en bytes)
PUBLISH_BYTES = 4096

# Credenciales de mqtt (se rellenan en el despliegue, no se versionan)
MQTT_URI       = "mqtt://BROKER_HOST:BROKER_PORT"
MQTT_USER      = "CAMBIAR_ME"
MQTT_PASS      = "CAMBIAR_ME"
MQTT_CLIENT_ID = "cassiagateway"

# Handlers de las caracteristicas del servicio NUS
HANDLE_CCCD = "19"
HANDLE_RX   = "16"

# Comandos utilizados en el dispoitivo objetivo
CMD_CONFIGURAR        = "01"
CMD_DESCARGA          = "02"
CMD_VER_CONFIGURACION = "03"
CMD_RESET             = "04"    
CMD_BORRAR            = "05"
CMD_SYNCTIME          = "06"
CMD_SECTOR_ACK        = "07"

# Segun documentacion de cassia, para activar y desactivar notificaciones (0100), para indicaciones tambien (0300)
CCCD_ON  = "0300"
CCCD_OFF = "0000"

# Estado global
connected_mac = None # Indica a que MAC se esta conectado
dispositivos_visibles = {} # mac : {rssi, ts}
gateway_mac = None # MAC del gateway
gateway_info = {} # Diccionario con informacion del gateway (modelo, version, etc)
tiempo_arranque = None # Timestamp del arranque del gateway (para calcular uptime)
pausa_autonomo = False # Flag para pausar el modo autonomo (para evitar solapes si hay un comando manual en curso)
en_cola = set() # MACs que están en la cola de comandos, para evitar encolar varias veces el mismo dispositivo
worker_ocupado = False # Flag para indicar si el worker de BLE está ocupado, para evitar solapamientos
mac_en_proceso = None  # MAC con connect pendiente en el chip si el disconnect de limpieza falló
cola = None # Cola de comandos BLE

# Funciones para cargar y guardar la configuración de known_macs en el sistema de archivos del gateway
def cargar_config():
    """Carga la configuración desde el sistema de archivos."""
    global KNOWN_MAC_PREFIXES
    global UMBRAL_DESCARGA
    global UMBRAL_RSSI
    try:
        config = cassiasys.read_user_config()
        if config:
            data = json.loads(config)
            KNOWN_MAC_PREFIXES = data['known_macs']
            UMBRAL_RSSI = data.get('umbral_rssi', UMBRAL_RSSI)
            UMBRAL_DESCARGA = data.get('umbral_descarga', UMBRAL_DESCARGA)
            print(f"[GW] Config cargada: {KNOWN_MAC_PREFIXES}")
    except Exception as e:
        print(f"[GW] Error cargando config: {e}")

def guardar_config():
    """Guarda la configuración en el sistema de archivos."""
    try:
        cassiasys.save_user_config(json.dumps({
            "known_macs": KNOWN_MAC_PREFIXES,
            "umbral_rssi": UMBRAL_RSSI,
            "umbral_descarga": UMBRAL_DESCARGA
        }))
    except Exception as e:
        print(f"[GW] Error guardando config: {e}")

# Clase para gestionar la conexión MQTT con reconexión automática y publicación de mensajes
class MQTTWrapper:
    """Wrapper para el cliente MQTT con reconexión automática.

    Attributes:
        _client: Instancia activa de CassiaMQTTClient, None si no hay conexión.
    """
    def __init__(self):
        """Inicializa el wrapper."""
        self._client = None

    async def runner(self):
        """Mantiene la conexion con el broker y despacha los comandos entrantes.

        Bucle infinito: (re)conecta, se suscribe al topic de comandos del
        gateway y delega cada mensaje en procesar_comandos(). Ante cualquier
        error espera 2 s y reintenta; _client queda a None mientras no hay
        conexion.
        """
        while True:
            try:
                async with CassiaMQTTClient(MQTT_URI, MQTT_USER, MQTT_PASS, MQTT_CLIENT_ID) as client:
                    self._client = client
                    print("[MQTT] Conectado")
                    await client.subscribe(topic(gateway_mac, "comandos"), qos=1)
                    async for msg in client:
                        print(f"[MQTT] Comando recibido: {msg}")
                        await procesar_comandos(self, msg)
            except Exception as e:
                print(f"[MQTT] Error: {e}")
            finally:
                self._client = None
                await asyncio.sleep(2)

    async def publish(self, topic, payload, qos=1, retain=False):
        """Publica un mensaje en el broker MQTT.

        Args:
            topic: Topic MQTT destino.
            payload: Contenido del mensaje como string JSON.
            qos: Nivel de calidad de servicio (0, 1 o 2).
            retain: Si True, el broker retiene el último mensaje.
        """
        if self._client:
            await self._client.publish(topic, payload, qos=qos, retain=retain)

def topic(mac, tipo):
    """Construye el topic MQTT para una MAC y un tipo de mensaje.

    Args:
        mac: MAC (del gateway o de un dispositivo) usada como namespace.
        tipo: Sufijo del topic (p.ej. "heartbeat", "respuesta", "datos/lora").

    Returns:
        Topic normalizado: /cassia/<mac_sin_dos_puntos_en_minusculas>/<tipo>.
    """
    mac_clean = mac.replace(":", "").lower()
    return f"/cassia/{mac_clean}/{tipo}"

async def stop_scan():
    """Detiene el escaneo BLE, ignorando el error si ya estaba parado."""
    try:
        await cassiablue.stop_scan()
    except RuntimeError:
        pass

async def publicar_respuesta(mqtt, cmd, mac, status, data=None):
    """Publica el resultado de un comando en el topic de respuesta del gateway.

    Args:
        mqtt: Instancia del wrapper MQTT.
        cmd: Nombre del comando ejecutado (p.ej. "connect", "download").
        mac: MAC del dispositivo objetivo, o None si no aplica.
        status: "success" o "failure".
        data: Informacion adicional opcional (se serializa a string).
    """
    payload = {
        "ts":  time.time(),
        "cmd": cmd,
        "mac": mac,
        "status": status,
    }
    if data:
        payload["data"] = data if isinstance(data, str) else str(data)
    await mqtt.publish(topic(gateway_mac, "respuesta"), json.dumps(payload))

async def heartbeat(mqtt):
    """Tarea para enviar un heartbeat del gateway al broker MQTT.
    Args:
        mqtt: Instancia del wrapper MQTT.
    """
    VENTANA_VISIBLE = 90 # Si un dispositivo no ha sido visto en los últimos 90s, se considera que ya no está visible

    while mqtt._client is None:
        await asyncio.sleep(1)

    while True:
        ahora = time.time()
        gc.collect()

        viejos = [m for m, i in dispositivos_visibles.items() if ahora - i["ts"] > 600]
        for m in viejos:
            del dispositivos_visibles[m]

        visibles = [
            {"mac": mac,
            "rssi": info["rssi"],
            "last_seen": int(info["ts"]),
            "last_download": int(info["last_download"]),
            }
            for mac, info in dispositivos_visibles.items()
            if  es_especial(mac) or ahora - info["ts"] < VENTANA_VISIBLE
        ]

        sistema = {
            "version":       gateway_info.get("version"),
            "uplink":        gateway_info.get("uplink"),
            "ip":            gateway_info.get("ip"),
            "wifi_signal":   gateway_info.get("wifi_signal"),
            "uptime_gw":     gateway_info.get("uptime_arranque", 0) + int(ahora - tiempo_arranque),
            "uptime_script": int(ahora - tiempo_arranque),
            "mem_free":      gc.mem_free(),
            "mem_alloc":     gc.mem_alloc(),
        }

        await mqtt.publish(topic(gateway_mac, "heartbeat"), json.dumps({
            "ts":            ahora,
            "sistema":       sistema,
            "visibles":      visibles,
            "umbral_descarga": UMBRAL_DESCARGA,
            "known_macs":    KNOWN_MAC_PREFIXES,
            "pausa_autonomo": pausa_autonomo,
            "filtro_rssi": UMBRAL_RSSI
        }))

        await asyncio.sleep(60)


# Funciones auxiliares
async def seguimiento_conexiones():
    """Tarea para seguir las conexiones BLE."""
    global connected_mac
    async for event in cassiablue.connection_result():
        state = event['connectionState']
        print(f"[BLE] {state}: {event['handle']}")
        connected_mac = event['handle'] if state == 'connected' else None

def parse_timestamp(ad_hex):
    """Extrae el timestamp Unix del campo Service Data (0x16) del adData."""
    try:
        ad = bytes.fromhex(ad_hex)
        # El adData es una secuencia de estructuras AD: [len][type][payload],
        # donde len cuenta type + payload. Recorremos saltando de una a otra.
        i = 0
        while i < len(ad):
            length = ad[i]
            if length == 0:
                break
            # 0x16 = "Service Data - 16-bit UUID". El payload es
            # [UUID 2B LE][timestamp u32 LE], asi que el u32 empieza en
            # i + len(1) + type(1) + UUID(2) = i + 4.
            if ad[i + 1] == 0x16:
                return struct.unpack_from('<I', ad, i + 4)[0]
            i += length + 1
    except Exception:
        pass
    return None

def crc16_nrf(data: bytes) -> int:
    """CRC16 idéntico al crc16_compute() del nRF5 SDK."""
    crc = 0xFFFF
    for byte in data:
        crc = ((crc >> 8) | (crc << 8)) & 0xFFFF
        crc ^= byte
        crc ^= (crc & 0xFF) >> 4
        crc ^= ((crc << 8) << 4) & 0xFFFF
        crc ^= (((crc & 0xFF) << 4) << 1) & 0xFFFF
    return crc

def es_especial(mac):
    """Indica si una MAC corresponde a un dispositivo especial (byte 4 = 42)."""
    return mac.split(':')[4] == '42'

def hora_str(t):
    """Formatea un timestamp Unix como HH:MM:SS (hora local del gateway).

    Nota: time.time() en el gateway tiene resolucion de 1 s, por eso las
    duraciones se miden con ticks_ms() y no restando timestamps.
    """
    lt = time.localtime(int(t))
    return "%02d:%02d:%02d" % (lt[3], lt[4], lt[5])

def imprimir_velocidad(etiqueta, t_ini, t_fin, dur_ms, nbytes):
    """Imprime duracion y velocidad de una descarga (o de una zona).

    t_ini/t_fin son timestamps Unix solo para mostrar la hora; la duracion
    viene de ticks_diff() en milisegundos.
    """
    dur = dur_ms / 1000
    vel = nbytes / dur if dur > 0 else 0.0
    print(f"[TIEMPOS] {etiqueta}: inicio={hora_str(t_ini)} fin={hora_str(t_fin)} duracion={dur:.3f} s bytes={nbytes} velocidad={vel:.1f} B/s ({vel * 8 / 1000:.2f} kbps)")

# Funciones base para gestionar las operaciones BLE
#
# Protocolo NUS (Nordic UART Service) con el logger. Todas las operaciones que
# esperan respuesta siguen el mismo patron:
#   1. gatt_write(HANDLE_CCCD, CCCD_ON) habilita indicaciones en el TX
#   2. gatt_write(HANDLE_RX, CMD_x [+ datos]) envia el comando
#   3. async for notif in cassiablue.notify_result(): ... consume el stream
#      SSE global de notificaciones GATT hasta recibir el ACK/NACK esperado o
#      agotar 'deadline' (time.time() + N s).
#   4. finally: gatt_write(HANDLE_CCCD, CCCD_OFF) deshabilita indicaciones
# El valor de cada notificacion llega como hex; se intenta decodificar a UTF-8
# para comparar con los literales ACK_* / NACK_*, y si no es texto se trata
# como datos binarios (solo en la descarga).

async def ble_conectar(mac, mqtt):
    """Conecta a un dispositivo BLE
    Args:
        mac: MAC del dispositivo BLE.
        mqtt: Instancia del wrapper MQTT.
    """
    try:
        ok, ret = await asyncio.wait_for(
            cassiablue.connect(mac, '{"type": "public", "timeout": 15000}'),
            timeout=15
        )
        print(f"[BLE] connect: {ok} {ret}")
        await publicar_respuesta(mqtt, "connect", mac, "success" if ok else "failure", ret)
    except asyncio.TimeoutError:
        print(f"[BLE] Timeout conectando: {mac}")
        await publicar_respuesta(mqtt, "connect", mac, "failure", "timeout")
        await cassiablue.disconnect(mac)
    except Exception as e:
        print(f"[BLE] Error conectando: {e}")
        await publicar_respuesta(mqtt, "connect", mac, "failure", str(e))

async def ble_desconectar(mac, mqtt):
    """Desconecta un dispositivo BLE
    Args:
        mac: MAC del dispositivo BLE.
        mqtt: Instancia del wrapper MQTT.
    """
    try:
        ok, ret = await cassiablue.disconnect(mac)
        print(f"[BLE] disconnect: {ok} {ret}")
        await publicar_respuesta(mqtt, "disconnect", mac, "success" if ok else "failure", ret)
    except Exception as e:
        print(f"[BLE] Error desconectando: {e}")
        await publicar_respuesta(mqtt, "disconnect", mac, "failure", str(e))

async def ble_set_config(mac, mqtt, config):
    """Escribe la configuración en el dispositivo BLE
    Args:
        mac: MAC del dispositivo BLE.
        mqtt: Instancia del wrapper MQTT.
        config: Configuración a escribir.
    """
    try:
        await cassiablue.gatt_write(mac, HANDLE_CCCD, CCCD_ON)
        await cassiablue.gatt_write(mac, HANDLE_RX, CMD_CONFIGURAR + config)

        deadline = time.time() + 10 
        async for notif in cassiablue.notify_result():
            try:
                decoded = bytes.fromhex(notif['value']).decode('utf-8')
            except Exception:
                decoded = None
            if decoded == 'ACK_CONFIG_UPDATE':
                print(f"[BLE] Config actualizada")
                await publicar_respuesta(mqtt, "set_config", mac, "success")
                break
            
            if decoded is not None and decoded.startswith('NACK_'):
                print(f"[BLE] Config rechazada: {decoded}")
                await publicar_respuesta(mqtt, "set_config", mac, "failure", decoded)
                break  

            if time.time() > deadline:
                print(f"[BLE] Timeout esperando ACK_CONFIG_UPDATE")
                await publicar_respuesta(mqtt, "set_config", mac, "failure", "timeout")
                break
    except Exception as e:
        print(f"[BLE] Error enviando config: {e}")
        await publicar_respuesta(mqtt, "set_config", mac, "failure", str(e))
    finally:
        await cassiablue.gatt_write(mac, HANDLE_CCCD, CCCD_OFF)

async def ble_get_config(mac, mqtt):
    """Lee la configuración del dispositivo BLE
    Args:
        mac: MAC del dispositivo BLE.
        mqtt: Instancia del wrapper MQTT.
    """
    try:
        await cassiablue.gatt_write(mac, HANDLE_CCCD, CCCD_ON)
        await cassiablue.gatt_write(mac, HANDLE_RX, CMD_VER_CONFIGURACION)

        config_data = None
        deadline = time.time() + 10
        async for notif in cassiablue.notify_result():
            value = notif['value']
            try:
                decoded = bytes.fromhex(value).decode('utf-8')
            except Exception:
                decoded = None
            if decoded == 'ACK_SEND_COMPLETE':
                print(f"[BLE] Config recibida completamente")
                await publicar_respuesta(mqtt, "get_config", mac, "success", config_data)
                break
            else:
                # Notificacion con la configuracion. Trama (hex en 'value'):
                # [prefijo 1B][logger_config_t CONFIG_SIZE B][addr_lora u32 LE][addr_imu u32 LE]
                # config_hex salta el prefijo (2 hex chars = 1 byte) y coge los
                # CONFIG_SIZE bytes de config (CONFIG_SIZE*2 hex chars). Los dos
                # u32 se leen sobre los bytes crudos a partir de offset.
                raw = bytes.fromhex(value)
                config_hex = value[2:2 + CONFIG_SIZE*2]  # CONFIG_SIZE bytes de config
                offset = 1 + CONFIG_SIZE  # byte prefijo + config
                addr_lora = struct.unpack_from('<I', raw, offset)[0]
                addr_imu  = struct.unpack_from('<I', raw, offset + 4)[0]
                config_data = {
                    "config":    config_hex,
                    "addr_lora": hex(addr_lora),
                    "addr_imu":  hex(addr_imu),
                }
            if time.time() > deadline:
                print(f"[BLE] Timeout esperando ACK_SEND_COMPLETE")
                await publicar_respuesta(mqtt, "get_config", mac, "failure", "timeout")
                break
    except Exception as e:
        print(f"[BLE] Error leyendo config: {e}")
        await publicar_respuesta(mqtt, "get_config", mac, "failure", str(e))
    finally:
        await cassiablue.gatt_write(mac, HANDLE_CCCD, CCCD_OFF)

async def ble_sync_time(mac, mqtt):
    """Sincroniza el timestamp Unix al dispositivo BLE
    Args:
        mac: MAC del dispositivo BLE.
        mqtt: Instancia del wrapper MQTT.
    """
    try:
        await cassiablue.gatt_write(mac, HANDLE_CCCD, CCCD_ON)
        ts = int(time.time())
        ts_hex = struct.pack('<I', ts).hex()
        await cassiablue.gatt_write(mac, HANDLE_RX, CMD_SYNCTIME + ts_hex)
        deadline = time.time() + 10
        async for notif in cassiablue.notify_result():
            value = notif['value']
            try:
                decoded = bytes.fromhex(value).decode('utf-8')
            except Exception:
                decoded = None
            if decoded == "ACK_TIME_SYNC":
                print(f"[BLE] Timestamp sincronizado")
                await publicar_respuesta(mqtt, "sync_time", mac, "success")
                break
            if time.time() > deadline:
                print(f"[BLE] Timeout esperando ACK_TIME_SYNC")
                await publicar_respuesta(mqtt, "sync_time", mac, "failure", "timeout")
                break
    except Exception as e:
        print(f"[BLE] Error sincronizando tiempo: {e}")
        await publicar_respuesta(mqtt, "sync_time", mac, "failure", str(e)) 
    finally:
        await cassiablue.gatt_write(mac, HANDLE_CCCD, CCCD_OFF)

async def ble_reset(mac, mqtt):
    """Reinicia el dispositivo BLE
    Args:
        mac: MAC del dispositivo BLE.
        mqtt: Instancia del wrapper MQTT.
    """
    try:
        ok, ret = await cassiablue.gatt_write(mac, HANDLE_RX, CMD_RESET)
        print(f"[BLE] Reset: {ok} {ret}")
        await publicar_respuesta(mqtt, "reset", mac, "success" if ok else "failure", ret)
    except Exception as e:
        print(f"[BLE] Error en reset: {e}")
        await publicar_respuesta(mqtt, "reset", mac, "failure", str(e))

async def ble_erase(mac, mqtt):
    """Borra la flash del dispositivo BLE
    Args:
        mac: MAC del dispositivo BLE.
        mqtt: Instancia del wrapper MQTT.
    """
    try:
        await cassiablue.gatt_write(mac, HANDLE_CCCD, CCCD_ON)
        ok, ret = await cassiablue.gatt_write(mac, HANDLE_RX, CMD_BORRAR)
        deadline = time.time() + 250 # Segun el datasheet de la flash, el borrado de toda la flash en low power mode puede ser de hata 240 s
        print(f"[BLE] Borrado iniciado: {ok} {ret}")
        async for notif in cassiablue.notify_result():
            try:
                decoded = bytes.fromhex(notif['value']).decode('utf-8')
            except Exception:
                decoded = None
            if decoded == 'ACK_ERASE_COMPLETE':
                print(f"[BLE] Borrado completo")
                await publicar_respuesta(mqtt, "erase", mac, "success")
                break
            if time.time() > deadline:
                print(f"[BLE] Timeout esperando ACK_ERASE_COMPLETE")
                await publicar_respuesta(mqtt, "erase", mac, "failure", "timeout")
                break
    except Exception as e:
        print(f"[BLE] Error borrando: {e}")
        await publicar_respuesta(mqtt, "erase", mac, "failure", str(e))
    finally:
        await cassiablue.gatt_write(mac, HANDLE_CCCD, CCCD_OFF)

async def ble_descargar_datos(mac, mqtt):
    """Descarga los datos almacenados en la flash del dispositivo BLE.

    Envia CMD_DESCARGA y procesa el stream de notificaciones GATT: recibe por
    chunks los sectores de las zonas LoRa e IMU, valida cada sector por longitud
    y CRC16 (crc16_nrf), responde con CMD_SECTOR_ACK (00 ok / 01 error) y
    republica los sectores validos en los topics datos/<zona>. Al final de cada
    zona y de la sesion imprime duracion y velocidad.

    Args:
        mac: MAC del dispositivo BLE.
        mqtt: Instancia del wrapper MQTT.

    Returns:
        True si la sesion termino completa (ACK_DOWNLOAD_COMPLETE o ACK_EMPTY),
        False si quedo incompleta (timeout, NACK del dispositivo o excepcion).
        Solo debe borrarse la flash del dispositivo cuando devuelve True.
    """
    try:
        await cassiablue.gatt_write(mac, HANDLE_CCCD, CCCD_ON)
        await cassiablue.gatt_write(mac, HANDLE_RX, CMD_DESCARGA)

        # Secuencia de notificaciones de una descarga:
        #   ACK_START
        #   (chunks binarios del sector 0) ACK_SECTOR:<idx>:<len>:<crc>
        #   (chunks binarios del sector 1) ACK_SECTOR:<idx>:<len>:<crc>
        #   ...
        #   ACK_LORA_COMPLETE. Fin de la zona "lora", se pasa a "imu"
        #   ...
        #   ACK_DOWNLOAD_COMPLETE. Fin de la zona "imu" y de la sesion
        # Es decir, el ACK_SECTOR llega DESPUES de los datos y describe el sector
        # que se acaba de recibir (longitud y CRC16 esperados). Se acumulan los chunks en hex_sector_actual 
        # y se valida al recibir el ACK.
        hex_sector_actual = ""   # Chunks del sector en curso, aun sin validar
        n_sectores = 0
        zona = "lora"           # zona activa; cambia a "imu" tras ACK_LORA_COMPLETE
        deadline = time.time() + 30
        session_id = None
        es_primero = True

        # Medida de tiempos/velocidad de descarga.
        # Las horas (t_*) son solo para el log
        t_inicio = None       # hora en que llega ACK_START
        t_zona = None         # hora en que empieza la zona en curso
        ms_inicio = None      # ticks_ms() en ACK_START
        ms_zona = None        # ticks_ms() al empezar la zona en curso
        bytes_totales = 0     # bytes utiles (sectores validos) de toda la sesion
        bytes_zona = 0        # bytes utiles de la zona en curso

        async for notif in cassiablue.notify_result():
            try:
                value = notif['value']
            except (KeyError, TypeError):
                continue

            try:
                decoded = bytes.fromhex(value).decode('utf-8')
            except Exception:
                decoded = None

            if decoded == 'ACK_START':
                t_inicio = time.time()
                t_zona = t_inicio
                ms_inicio = time.ticks_ms()
                ms_zona = ms_inicio
                bytes_totales = 0
                bytes_zona = 0
                print(f"[BLE] Inicio de descarga")
                print(f"[TIEMPOS] ACK_START recibido a las {hora_str(t_inicio)} (ticks={ms_inicio} ms)")
                # session_id agrupa en el backend todos los sectores de esta
                # descarga; es_primero/es_ultimo marcan los extremos de cada zona
                # para que el backend sepa abrir/cerrar el fichero.
                session_id = int(t_inicio)
                es_primero = True
                deadline = time.time() + 30
                continue

            elif decoded and decoded.startswith('ACK_SECTOR:'):
                # Formato: "ACK_SECTOR:<idx>:<len>:<crc>" -> partes[2]=len, partes[3]=crc
                sector_ok = False

                if hex_sector_actual:
                    try:
                        partes = decoded.split(":")
                        expected_len = int(partes[2])
                        expected_crc = int(partes[3])
                        sector_bytes = bytes.fromhex(hex_sector_actual)
                        actual_crc = crc16_nrf(sector_bytes)

                        if len(sector_bytes) == expected_len and actual_crc == expected_crc:
                            sector_ok = True
                            n_sectores += 1
                            bytes_totales += len(sector_bytes)
                            bytes_zona += len(sector_bytes)
                            asyncio.create_task(mqtt.publish(
                                topic(mac, f"datos/{zona}"),
                                json.dumps({
                                    "ts": int(time.time()),
                                    "session_id": session_id,
                                    "es_primero": es_primero,
                                    "es_ultimo": False,
                                    "data": hex_sector_actual
                                })
                            ))
                            es_primero = False
                            print(f"[BLE] Sector recibido: {zona} sector {n_sectores} (len={expected_len}, crc_ok)")
                        else:
                            print(f"[BLE] Sector corrupto: len = {len(sector_bytes)}/{expected_len}, crc={actual_crc:#06x}/{expected_crc:#06x}")
                    except Exception as e:
                        print(f"[BLE] Error parseando ACK_SECTOR: {e}")

                # Respuesta por sector que consume el firmware del logger:
                # "00" = sector aceptado, "01" = sector no valido (longitud/CRC
                # incorrectos o parseo fallido). Se responde siempre.
                status = "00" if sector_ok else "01"
                await cassiablue.gatt_write(mac, HANDLE_RX, CMD_SECTOR_ACK + status)

                hex_sector_actual = ""  # Reiniciamos el buffer para el siguiente sector
                deadline = time.time() + 30
                continue

            elif decoded and decoded.startswith('NACK_SECTOR'):
                print(f"[BLE] Descarga abortada por el dispositivo: {decoded}")
                if t_inicio is not None:
                    imprimir_velocidad("PARCIAL (abortada)", t_inicio, time.time(), time.ticks_diff(time.ticks_ms(), ms_inicio), bytes_totales)
                await publicar_respuesta(mqtt, "download", mac, "failure", decoded)
                return False  # sesion incompleta, no se debe borrar

            elif decoded == 'ACK_LORA_COMPLETE':
                if hex_sector_actual:
                    print(f"[BLE] WARN: datos huérfanos al llegar ACK_LORA_COMPLETE, descartando")
                    hex_sector_actual = ""

                print(f"[BLE] Zona LoRa completa: {n_sectores} sectores")
                if t_zona is not None:
                    t_fin_zona = time.time()
                    ms_fin_zona = time.ticks_ms()
                    imprimir_velocidad("zona LoRa", t_zona, t_fin_zona, time.ticks_diff(ms_fin_zona, ms_zona), bytes_zona)
                    t_zona = t_fin_zona
                    ms_zona = ms_fin_zona
                bytes_zona = 0
                asyncio.create_task(mqtt.publish(
                    topic(mac, "datos/lora"),
                    json.dumps({
                        "ts": int(time.time()),
                        "session_id": session_id,
                        "es_primero": False,
                        "es_ultimo": True,
                        "data": ""
                    })
                ))
                zona = "imu"
                es_primero = True  
                n_sectores = 0
                deadline = time.time() + 30
                continue

            elif decoded == 'ACK_DOWNLOAD_COMPLETE':
                if hex_sector_actual:
                    print(f"[BLE] WARN: datos huérfanos al llegar ACK_DOWNLOAD_COMPLETE, descartando")
                    hex_sector_actual = ""
                print(f"[BLE] Zona IMU completa: {n_sectores} sectores")
                print(f"[BLE] Descarga completa")

                t_fin = time.time()
                ms_fin = time.ticks_ms()
                print(f"[TIEMPOS] ACK_DOWNLOAD_COMPLETE recibido a las {hora_str(t_fin)} (ticks={ms_fin} ms)")
                if t_zona is not None:
                    imprimir_velocidad("zona IMU", t_zona, t_fin, time.ticks_diff(ms_fin, ms_zona), bytes_zona)
                if t_inicio is not None:
                    imprimir_velocidad("TOTAL descarga", t_inicio, t_fin, time.ticks_diff(ms_fin, ms_inicio), bytes_totales)
                else:
                    print("[TIEMPOS] No se recibio ACK_START, no se puede calcular la duracion")


                asyncio.create_task(mqtt.publish(
                    topic(mac, "datos/imu"),
                    json.dumps({
                        "ts": int(time.time()),
                        "session_id": session_id,
                        "es_primero": False,
                        "es_ultimo": True,
                        "data": ""
                    })
                ))

                await publicar_respuesta(mqtt, "download", mac, "success")
                return True # sesion completa

            elif decoded == 'ACK_EMPTY':
                print(f"[BLE] Sin datos")
                await publicar_respuesta(mqtt, "download", mac, "success", "no data")
                return True # No habia nada que descargar

            else:
                # Chunk de datos binarios del sector en curso
                deadline = time.time() + 30
                hex_sector_actual += value

            if time.time() > deadline:
                print(f"[BLE] Timeout esperando descarga")
                if t_inicio is not None:
                    imprimir_velocidad("PARCIAL (timeout)", t_inicio, time.time(),
                                       time.ticks_diff(time.ticks_ms(), ms_inicio), bytes_totales)
                await publicar_respuesta(mqtt, "download", mac, "failure", "timeout")
                break

        return False # salida por timeout u otro corte, sesion incompleta

    except Exception as e:
        print(f"[BLE] Error descargando: {e}")
        await publicar_respuesta(mqtt, "download", mac, "failure", str(e))
        return False
    finally:
        await cassiablue.gatt_write(mac, HANDLE_CCCD, CCCD_OFF)

# Funcion para gestionar las tareas ble, tanto manuales como autonomas, evitando solapamientos
async def ble_worker(mqtt):
    """Consume la cola de comandos BLE de forma serializada.

    Unico punto que opera el chip BLE. Por cada job: detiene el scan, conecta
    al dispositivo si no habia sesion abierta (con reintentos ante "can not
    scan"), ejecuta el comando (manual o auto_download) con su propio timeout y
    desconecta al terminar si la conexion la abrio el. Mantiene worker_ocupado
    para que el bucle de escaneo de main() ceda el chip, y mac_en_proceso para
    el recovery si el disconnect de limpieza falla.

    Args:
        mqtt: Instancia del wrapper MQTT.
    """
    global worker_ocupado, mac_en_proceso

    while True:
        try:
            job = await cola.get()
            worker_ocupado = True
            mac = job['mac']
            cmd = job['cmd']
            print(f"[WORKER] Job recogido: {cmd} para {mac}")

            # El chip no puede escanear y operar una conexion a la vez: paramos
            # el scan y damos 2 s para que el estado del chip se estabilice.
            await stop_scan()
            await asyncio.sleep(2)

            try:
                if cmd == "connect":
                    await ble_conectar(mac, mqtt)

                elif cmd == "disconnect":
                    await ble_desconectar(mac or connected_mac, mqtt)

                elif cmd == "auto_download":
                    # Descarga autonoma: conectar - descargar - borrar flash si
                    # habia datos - desconectar. Misma logica de conexion que la
                    # rama 'else', pero encadena el ble_erase y actualiza
                    # last_download al terminar.
                    en_cola.discard(mac)
                    if not mqtt._client:
                        print(f"[GW] MQTT no conectado, no se puede descargar {mac}")
                        continue
                    ya_conectado = (connected_mac == mac)
                    try:
                        if not ya_conectado:
                            mac_en_proceso = mac
                            # Reintentos ante "can not scan": error de la API
                            # Cassia que indica que el chip aun esta ocupado con
                            # el scan anterior. Hasta 3 intentos con 3 s de espera.
                            for intento in range(3):
                                ok, ret = await asyncio.wait_for(
                                    cassiablue.connect(mac, '{"type": "public", "timeout": 15000}'),
                                    timeout=15
                                )
                                if ok:
                                    break
                                if "can not scan" in str(ret) and intento < 2:
                                    print(f"[BLE] device can not scan, reintentando en 3s... ({intento+1}/2)")
                                    await asyncio.sleep(3)
                                else:
                                    break
                            if not ok:
                                mac_en_proceso = None
                                await publicar_respuesta(mqtt, "download", mac, "failure", f"connect failed: {ret}")
                                continue
                        hay_datos = await ble_descargar_datos(mac, mqtt)
                        if hay_datos:
                            await ble_erase(mac, mqtt)
                    finally:
                        if not ya_conectado:
                            try:
                                await asyncio.wait_for(cassiablue.disconnect(mac), timeout=5)
                                mac_en_proceso = None
                            except Exception:
                                pass
                            await asyncio.sleep(1)
                        if mac in dispositivos_visibles:
                            dispositivos_visibles[mac]["last_download"] = time.time()

                else:
                    # Resto de comandos manuales dirigidos a un dispositivo.
                    # Conecta si hace falta (mismos reintentos "can not scan" que
                    # auto_download) y despacha el comando con su propio timeout.
                    ya_conectado = (connected_mac == mac)
                    print(f"[WORKER] Procesando {cmd} para {mac} (ya_conectado={ya_conectado})")
                    try:
                        if not ya_conectado:
                            mac_en_proceso = mac
                            for intento in range(3):
                                ok, ret = await asyncio.wait_for(
                                    cassiablue.connect(mac, '{"type": "public", "timeout": 12000}'),
                                    timeout=15
                                )
                                if ok:
                                    break
                                if "can not scan" in str(ret) and intento < 2:
                                    print(f"[BLE] device can not scan, reintentando en 3s... ({intento+1}/2)")
                                    await asyncio.sleep(3)
                                else:
                                    break
                            if not ok:
                                mac_en_proceso = None
                                await publicar_respuesta(mqtt, cmd, mac, "failure", ret)
                                continue
                        if cmd == "download":
                            await asyncio.wait_for(ble_descargar_datos(mac, mqtt), timeout=300)
                        elif cmd == "get_config":
                            await asyncio.wait_for(ble_get_config(mac, mqtt), timeout=15)
                        elif cmd == "set_config":
                            await asyncio.wait_for(ble_set_config(mac, mqtt, job.get('config')), timeout=15)
                        elif cmd == "reset":
                            await asyncio.wait_for(ble_reset(mac, mqtt), timeout=10)
                        elif cmd == "erase":
                            await asyncio.wait_for(ble_erase(mac, mqtt), timeout=260)
                        elif cmd == "sync_time":
                            await asyncio.wait_for(ble_sync_time(mac, mqtt), timeout=15)
                    except asyncio.TimeoutError:
                        print(f"[WORKER] Timeout en {cmd}")
                        await publicar_respuesta(mqtt, cmd, mac, "failure", "timeout")
                    except Exception as e:
                        print(f"[WORKER] Error en {cmd}: {e}")
                        await publicar_respuesta(mqtt, cmd, mac, "failure", str(e))
                    finally:
                        if not ya_conectado:
                            try: 
                                await asyncio.wait_for(cassiablue.disconnect(mac), timeout=5)
                                mac_en_proceso = None
                            except Exception as e:
                                pass
                            await asyncio.sleep(1)

            except Exception as e:
                print(f"[WORKER] Error en {cmd}: {e}")
                await publicar_respuesta(mqtt, cmd, mac, "failure", str(e))
            
            finally:
                await asyncio.sleep(1)
                worker_ocupado = False
        except Exception as e:
            print(f"[WORKER] Error inesperado: {e}")
            worker_ocupado = False

async def procesar_comandos(mqtt, msg):
    """Procesa un mensaje entrante del topic de comandos del gateway.

    Los comandos de configuracion del gateway (umbrales de descarga y RSSI,
    alta/baja de MACs conocidas, pausa del modo autonomo, reboot, reinicio de
    scan) se atienden aqui directamente y persisten con guardar_config(). Los
    comandos dirigidos a un dispositivo (connect, disconnect, download,
    get_config, set_config, reset, erase, sync_time) se encolan para ble_worker().

    Args:
        mqtt: Instancia del wrapper MQTT.
        msg: Mensaje MQTT recibido (dict, o JSON con clave 'payload').
    """
    global pausa_autonomo, UMBRAL_DESCARGA, mac_en_proceso

    try:
        data = msg if isinstance(msg, dict) else json.loads(msg)
        payload = json.loads(data['payload'])
        cmd = payload['cmd']
        mac = payload.get('mac')

        if cmd == "update_download_time":
            tiempo = int(payload['time'])
            if tiempo > 0:
                UMBRAL_DESCARGA = tiempo
                guardar_config()
                print(f"[GW] Nuevo umbral: {UMBRAL_DESCARGA}s")
            else:
                print("[GW] Error: umbral debe ser mayor que 0")

        elif cmd == "update_rssi_filter":
            global UMBRAL_RSSI
            rssi = int(payload['rssi'])
            UMBRAL_RSSI = rssi
            guardar_config()
            print(f"[GW] Nuevo umbral RSSI: {UMBRAL_RSSI} dBm")

        elif cmd == "restart_scan":
            if worker_ocupado:
                print("[GW] Worker ocupado, el scan se reiniciará al terminar")
            else:
                await stop_scan()
                print("[GW] Scan reiniciado por comando")
                if mac_en_proceso:
                    try:
                        await asyncio.wait_for(cassiablue.disconnect(mac_en_proceso), timeout=5)
                        mac_en_proceso = None
                    except Exception:
                        pass
                print("[GW] Scan reiniciado por comando")

        elif cmd == "reboot":
            print("[GW] Reiniciando gateway...")
            await stop_scan()
            ok, ret = await cassiablue.send_cmd("/cassia/reboot", method="POST")
            print(f"[GW] Reboot: {ok} {ret}")

        elif cmd == "add_mac":
            if mac not in KNOWN_MAC_PREFIXES:
                KNOWN_MAC_PREFIXES.append(mac)
                guardar_config()
                print(f"[GW] MAC agregada: {mac}")
                # El filtro del scan incluye MACs en el momento del start_scan,
                # hay que reiniciar para que la nueva MAC entre en el filtro hardware
                await stop_scan()
                print(f"[GW] Scan parado para reiniciar con nueva MAC")
            else:
                print(f"[GW] MAC ya existe: {mac}")

        elif cmd == "remove_mac":
            if mac in KNOWN_MAC_PREFIXES:
                KNOWN_MAC_PREFIXES.remove(mac)
                guardar_config()
                print(f"[GW] MAC removida: {mac}")
                # No es necesario reiniciar el scan: ignoramos en software los
                # paquetes de la MAC eliminada hasta el próximo arranque del scan
            else:
                print(f"[GW] MAC no encontrada: {mac}")        

        elif cmd == "pausa_on":
            pausa_autonomo = True
            print("[GW] Pausa autonomo ON")

        elif cmd == "pausa_off":
            pausa_autonomo = False
            print("[GW] Pausa autonomo OFF")

        elif cmd in ("connect", "disconnect", "download", "get_config", "set_config", "reset", "erase", "sync_time"):
            if not mac and cmd != "disconnect":
                print(f"[GW] {cmd} requiere mac")
                return
            job = {"mac": mac, "cmd": cmd}
            if cmd == "set_config":
                job["config"] = payload.get('config')
            cola.put_nowait(job)
            print(f"[GW] Job encolado: {cmd} para {mac}")

        else:
            print(f"[GW] Comando desconocido: {cmd}")
            await publicar_respuesta(mqtt, cmd, mac, "failure", "unknown command")

    except Exception as e:
        print(f"[GW] Error procesando comando: {e}")
        await publicar_respuesta(mqtt, cmd if 'cmd' in locals() else 'unknown', mac if 'mac' in locals() else None, "failure", str(e))

async def main():
    """Punto de entrada: inicializa el gateway y ejecuta el bucle de escaneo.

    Carga la configuracion persistente, obtiene la info del gateway (MAC,
    version, IP, senal WiFi), arranca las tareas de fondo (runner MQTT,
    heartbeat, seguimiento de conexiones, worker BLE) y entra en el bucle de
    escaneo: detecta loggers conocidos, actualiza la tabla de visibles y encola
    para descarga autonoma los que llevan mas de UMBRAL_DESCARGA sin descargar.
    Incluye recovery ante fallos repetidos de start_scan (disconnect dirigido y,
    tras 5 fallos, clear_resource).
    """
    global gateway_mac, gateway_info, tiempo_arranque, cola, en_cola, mac_en_proceso

    tiempo_arranque = time.time()

    # Cargar configuración persistente (MACs conocidas, umbrales de descarga y RSSI)
    cargar_config()

    # Obtener información del gateway (MAC, versión firmware, IP, señal WiFi)
    # para publicarla en heartbeat y usarla como identificador en los topics MQTT
    try:
        ok, ret = await cassiablue.send_cmd("/cassia/info")
        if ok:
            data = json.loads(ret)
            gateway_mac = data.get("mac")
            gateway_info = {
                "version":         data.get("version"),
                "uplink":          data.get("capwap-uplink"),
                "ip":              data["wireless"]["iface"]["ip"],
                "wifi_signal":     int(s.split()[0]) if (s := data["wireless"].get("signal", "")) else None,
                "uptime_arranque": data.get("uptime"),
            }
            print(f"[GW] MAC: {gateway_mac}")
    except Exception as e:
        print(f"[GW] Error leyendo info: {e}")
        gateway_mac = "unknown"

    # Cola de comandos BLE compartida entre procesar_comandos() y ble_worker()
    # El tamaño máximo evita acumular operaciones obsoletas si el worker va lento
    mqtt = MQTTWrapper()
    cola = AsyncQueue(max_size=16)
    en_cola = set()

    # Tarea MQTT que mantiene la conexión con el broker y despacha mensajes entrantes
    # Tarea heartbeat: publica periódicamente estado del gateway (uptime, visibles, etc.)
    asyncio.create_task(mqtt.runner())
    asyncio.create_task(heartbeat(mqtt))

    # SSE de estado de conexión. El gateway notifica por este stream cuando un
    # dispositivo BLE se conecta o desconecta.
    # seguimiento_conexiones() actualiza connected_mac para que el worker sepa 
    # si ya hay sesión abierta con un dispositivo
    ok, _ = await cassiablue.start_recv_connection_state()
    if not ok:
        print("[BLE] Error iniciando seguimiento de conexiones")
        return
    asyncio.create_task(seguimiento_conexiones())

    # SSE de notificaciones GATT es el stream por el que llegan los chunks de datos
    # durante la descarga, las confirmaciones de config y los ACKs de borrado
    ok, _ = await cassiablue.start_recv_notify()
    if not ok:
        print("[BLE] Error iniciando recepción de notificaciones")
        return

    # Worker BLE que consume la cola de comandos de forma serializada para evitar
    # solapamientos; gestiona tanto descargas autónomas como comandos manuales
    asyncio.create_task(ble_worker(mqtt))

    # Bucle principal de escaneo BLE
    # Permanece activo mientras el worker no esté ocupado; cuando detecta un
    # dispositivo conocido con datos pendientes lo encola para descarga autónoma.
    # scan_fallos rastrea timeouts consecutivos de start_scan para activar el
    # recovery con disconnect dirigido antes de reintentar
    print("[GW] Iniciando escaneo")
    scan_fallos = 0

    while True:
        # Ceder mientras el worker tiene el chip ocupado
        while worker_ocupado:
            await asyncio.sleep(0.5)

        # Pequeña pausa tras soltar el worker para que el chip estabilice el estado
        await asyncio.sleep(1)

        filter_mac = ",".join(KNOWN_MAC_PREFIXES)
        if not filter_mac:
            print("[GW] Sin MACs conocidas, esperando...")
            await asyncio.sleep(10)
            continue

        # Si start_scan falló previamente y hay un MAC con connect pendiente en el chip,
        # intentar un disconnect dirigido antes de reintentar, evita reboot del gateway.
        if scan_fallos > 0 and mac_en_proceso:
            print(f"[BLE] Liberando chip: disconnect {mac_en_proceso}...")
            try:
                await asyncio.wait_for(cassiablue.disconnect(mac_en_proceso), timeout=5)
                mac_en_proceso = None
            except Exception:
                pass
            await asyncio.sleep(2)

        try:
            ok, _ = await asyncio.wait_for(cassiablue.start_scan(f"timestamp=1&active=0&filter_mac={filter_mac}"), timeout=10)
        except (asyncio.TimeoutError, RuntimeError):
            scan_fallos += 1
            print(f"[BLE] Timeout al iniciar scan ({scan_fallos}), reintentando en 3s...")
            if scan_fallos >= 5:
                print(f"[BLE] {scan_fallos} fallos consecutivos, ejecutando clear_resource y esperando 15s...")
                cassiablue.clear_resource()
                mac_en_proceso = None
                await asyncio.sleep(15)
                await cassiablue.start_recv_notify()
                scan_fallos = 0
            else:
                await asyncio.sleep(3)
            continue

        # Comprobar si el worker cogió un job mientras start_scan estaba en vuelo;
        # si es así, ceder inmediatamente — el worker ya habrá llamado stop_scan()
        if worker_ocupado:
            await stop_scan()
            continue

        scan_fallos = 0
        if not ok:
            print("[BLE] Error al iniciar scan, reintentando en 3s...")
            await asyncio.sleep(3)
            continue

        # Obtener el iterador del stream SSE de scan; si aún no está listo, reintentar
        scan = cassiablue.scan_result()
        if scan is None:
            await asyncio.sleep(1)
            continue

        # Iterar dispositivos recibidos por el stream hasta que el worker pare el scan
        # o hasta que el gateway deje de emitir (el async for termina solo en ese caso)
        scan_start = time.time()
        async for dev in scan:
            mac = dev['bdaddr']

            # Ignorar MACs que se hayan eliminado de KNOWN_MACS en caliente
            if not any(mac.startswith(p.rstrip('*')) for p in KNOWN_MAC_PREFIXES):
                continue

            # Actualizar tabla de visibilidad usada por heartbeat y por el widget de TB
            dispositivos_visibles[mac] = {
                "rssi": dev.get('rssi', 0),
                "ts":   time.time(),
                "last_download": parse_timestamp(dev['adData']) or 0,
            }

            # Saltar si ya hay un job pendiente para este dispositivo
            if mac in en_cola:
                continue

            # Filtro de tipo: solo loggers (byte 4 de la MAC = 4C identifica el modelo)
            if mac.split(':')[4] != '4C':
                continue

            # Filtro de señal: descartar dispositivos demasiado lejos
            if dev.get('rssi', -100) < UMBRAL_RSSI:
                continue

            # El timestamp de la última descarga viene codificado en el adData del Logger
            ts = parse_timestamp(dev['adData']) or 0
            if ts == 0:
                print(f"[GW] {mac} - sin timestamp en adData, ignorando")
                continue

            # Si los datos llevan más de UMBRAL_DESCARGA segundos sin descargarse,
            # encolar descarga autónoma (incluye borrado de flash tras la descarga)
            elapsed = int(time.time() - ts)
            ultimo_intento = dispositivos_visibles.get(mac, {}).get("last_download", 0)
            if elapsed > UMBRAL_DESCARGA and time.time() - ultimo_intento > UMBRAL_DESCARGA:
                if not pausa_autonomo:
                    cola.put_nowait({"mac": mac, "cmd": "auto_download"})
                    en_cola.add(mac)
                    print(f"[GW] {mac} encolada")
                else:
                    print(f"[GW] {mac} - autonomo pausado, no se encola")

        # Si el scan duró menos de 2s algo lo interrumpió inesperadamente
        if time.time() - scan_start < 2:
            print("[BLE] Scan termino inesperadamente, esperando 5s...")
            await asyncio.sleep(5)

asyncio.run(main())