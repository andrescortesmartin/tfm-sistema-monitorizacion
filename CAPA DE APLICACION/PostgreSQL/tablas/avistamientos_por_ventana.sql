CREATE TABLE public.avistamientos_por_ventana (
	id bigserial NOT NULL,
	dispositivo_id int4 NOT NULL,          -- dispositivo observador (FK dispositivos)
	visto_dispositivo_id int4 NOT NULL,    -- dispositivo detectado (FK dispositivos)
	tipo text NOT NULL,                    -- tipo del dispositivo visto: 'logger' | 'beacon'
	ts_ventana timestamptz NOT NULL,       -- inicio de la ventana temporal a la que se agrega el avistamiento
	obs int2 NULL,                         -- numero de observaciones en la ventana
	rssi int2 NULL,                        -- RSSI representativo del avistamiento en la ventana (dBm)
	origen text NOT NULL,                  -- fuente del dato: 'ble' | 'lorawan'
	medicion_ble_id int4 NULL,             -- FK a la medicion BLE de origen (si origen='ble')
	medicion_lorawan_id int4 NULL,         -- FK a la medicion LoRaWAN de origen (si origen='lorawan')
	creado_en timestamptz DEFAULT now() NOT NULL,
	anillo text NULL,                      -- anillo de proximidad derivado del RSSI: 'inmediata' | 'cercana' | 'lejana'
	-- Restringe 'anillo' a los tres valores permitidos (NULL sigue siendo valido)
	CONSTRAINT avistamientos_por_ventana_anillo_check CHECK ((anillo = ANY (ARRAY['inmediata'::text, 'cercana'::text, 'lejana'::text]))),
	-- Deduplicacion: un unico registro por (observador, visto, tipo, ventana). Evita filas repetidas si el mismo avistamiento se procesa mas de una vez
	CONSTRAINT avistamientos_por_ventana_dispositivo_id_visto_dispositivo__key UNIQUE (dispositivo_id, visto_dispositivo_id, tipo, ts_ventana),
	-- Restringe 'origen' a las dos fuentes de datos posibles
	CONSTRAINT avistamientos_por_ventana_origen_check CHECK ((origen = ANY (ARRAY['ble'::text, 'lorawan'::text]))),
	-- Clave primaria: identificador sintetico de la fila
	CONSTRAINT avistamientos_por_ventana_pkey PRIMARY KEY (id),
	-- Restringe 'tipo' a los tipos de dispositivo visto contemplados
	CONSTRAINT avistamientos_por_ventana_tipo_check CHECK ((tipo = ANY (ARRAY['logger'::text, 'beacon'::text]))),
	-- FK: el observador debe existir en dispositivos
	CONSTRAINT avistamientos_por_ventana_dispositivo_id_fkey FOREIGN KEY (dispositivo_id) REFERENCES public.dispositivos(id),
	-- FK: si hay medicion BLE de origen, debe existir en mediciones_lentas_ble
	CONSTRAINT avistamientos_por_ventana_medicion_ble_id_fkey FOREIGN KEY (medicion_ble_id) REFERENCES public.mediciones_lentas_ble(id),
	-- FK: si hay medicion LoRaWAN de origen, debe existir en mediciones_lentas_lorawan
	CONSTRAINT avistamientos_por_ventana_medicion_lorawan_id_fkey FOREIGN KEY (medicion_lorawan_id) REFERENCES public.mediciones_lentas_lorawan(id),
	-- FK: el dispositivo visto debe existir en dispositivos
	CONSTRAINT avistamientos_por_ventana_visto_dispositivo_id_fkey FOREIGN KEY (visto_dispositivo_id) REFERENCES public.dispositivos(id)
);

-- Table Triggers

-- Tras cada insert, actualiza la agregacion horaria de contactos
create trigger trg_contactos_por_hora after
insert
    on
    public.avistamientos_por_ventana for each row execute function procesar_contactos_por_hora();
