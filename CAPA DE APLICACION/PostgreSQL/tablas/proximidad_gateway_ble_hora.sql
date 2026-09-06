CREATE TABLE public.proximidad_gateway_ble_hora (
	id bigserial NOT NULL,
	dispositivo_id int4 NOT NULL,               -- dispositivo detectado (FK dispositivos)
	gateway_id text NOT NULL,                   -- gateway BLE que lo detecta (texto, no FK)
	anillo text NOT NULL,                       -- anillo de proximidad derivado del RSSI: 'inmediata' | 'cercana' | 'lejana'
	hora_inicio timestamptz NOT NULL,           -- inicio de la hora agregada (truncado a hora)
	n_avistamientos int4 DEFAULT 1 NOT NULL,    -- numero de avistamientos acumulados en esa hora/anillo
	rssi_medio float8 NOT NULL,                 -- RSSI medio de los avistamientos de la hora (dBm)
	rssi_max int2 NOT NULL,                     -- RSSI maximo de la hora (dBm)
	primer_avistamiento timestamptz NOT NULL,   -- ts del primer avistamiento de la hora
	ultimo_avistamiento timestamptz NOT NULL,   -- ts del ultimo avistamiento de la hora
	-- Restringe 'anillo' a los tres valores permitidos
	CONSTRAINT proximidad_gateway_ble_hora_anillo_check CHECK ((anillo = ANY (ARRAY['inmediata'::text, 'cercana'::text, 'lejana'::text]))),
	-- Deduplicacion / clave de upsert: una fila por (dispositivo, gateway, anillo, hora)
	CONSTRAINT proximidad_gateway_ble_hora_dispositivo_id_gateway_id_anill_key UNIQUE (dispositivo_id, gateway_id, anillo, hora_inicio),
	-- Clave primaria: identificador sintetico de la fila
	CONSTRAINT proximidad_gateway_ble_hora_pkey PRIMARY KEY (id),
	-- FK: el dispositivo debe existir en dispositivos
	CONSTRAINT proximidad_gateway_ble_hora_dispositivo_id_fkey FOREIGN KEY (dispositivo_id) REFERENCES public.dispositivos(id)
);

-- Consulta tipica: proximidad de un dispositivo a lo largo del tiempo
CREATE INDEX idx_proximidad_gw_dispositivo_hora ON public.proximidad_gateway_ble_hora USING btree (dispositivo_id, hora_inicio);
