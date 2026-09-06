CREATE TABLE public.mediciones_lentas_lorawan (
	id serial4 NOT NULL,
	dispositivo_id int4 NOT NULL,             -- nodo sensor que genero la medida (FK dispositivos)
	"timestamp" timestamptz NOT NULL,         -- instante de la medida (reloj del nodo)
	bateria_mv int4 NULL,                     -- tension de bateria en mV
	presion int2 NULL,                        -- presion (hPa)
	temperatura int2 NULL,                    -- temperatura (grados centigrados)
	pitch float8 NULL,                        -- inclinacion pitch (grados)
	roll float8 NULL,                         -- inclinacion roll (grados)
	gps_lat float8 NULL,                      -- latitud GPS (si el modulo GPS estaba activo)
	gps_lon float8 NULL,                      -- longitud GPS
	ranging jsonb NULL,                       -- resultado de ranging, paquete compacto
	loggers jsonb NULL,                       -- loggers BLE vistos, paquete compacto
	beacons jsonb NULL,                       -- beacons BLE vistos, paquete compacto
	-- Deduplicacion: una sola medida por dispositivo e instante (ingesta idempotente)
	CONSTRAINT mediciones_lentas_lorawan_dispositivo_id_timestamp_key UNIQUE (dispositivo_id, "timestamp"),
	-- Clave primaria: identificador sintetico de la fila
	CONSTRAINT mediciones_lentas_lorawan_pkey PRIMARY KEY (id),
	-- FK: el dispositivo debe existir en dispositivos
	CONSTRAINT mediciones_lentas_lorawan_dispositivo_id_fkey FOREIGN KEY (dispositivo_id) REFERENCES public.dispositivos(id)
);

-- Table Triggers

-- Al llegar loggers/beacons, deriva los avistamientos por ventana (misma funcion que en la via BLE)
create trigger trg_avistamientos_lorawan after
insert
    or
update
    of loggers,
    beacons on
    public.mediciones_lentas_lorawan for each row
    when (((new.loggers is not null)
        or (new.beacons is not null))) execute function procesar_avistamientos_ble();

-- Al llegar/actualizarse la posicion GPS, actualiza la agregacion de posicionamiento
create trigger trg_posicionamiento_gps_lorawan after
insert
    or
update
    of gps_lat,
    gps_lon on
    public.mediciones_lentas_lorawan for each row execute function fn_trg_posicionamiento_gps();
