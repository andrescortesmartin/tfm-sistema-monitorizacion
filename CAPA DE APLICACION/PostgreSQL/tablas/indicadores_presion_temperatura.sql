CREATE TABLE public.indicadores_presion_temperatura (
	id serial4 NOT NULL,
	dispositivo_id int4 NOT NULL,                    -- dispositivo de origen (FK dispositivos)
	medicion_ble_id int4 NULL,                       -- FK a la medicion BLE de origen (si origen='ble')
	medicion_lorawan_id int4 NULL,                   -- FK a la medicion LoRaWAN de origen (si origen='lorawan')
	origen varchar(10) NOT NULL,                     -- fuente del dato: 'ble' | 'lorawan'
	"timestamp" timestamptz NOT NULL,                -- instante de la medida
	delta_t_real interval NULL,                      -- tiempo transcurrido desde la medida anterior (base de los gradientes)
	presion_puntual float8 NULL,                     -- presion en este instante
	gradiente_presion_instantaneo float8 NULL,       -- variacion de presion respecto a la medida anterior, por unidad de tiempo
	temperatura_puntual float8 NULL,                 -- temperatura en este instante
	gradiente_temperatura_instantaneo float8 NULL,   -- variacion de temperatura respecto a la medida anterior, por unidad de tiempo
	creado_en timestamptz DEFAULT now() NOT NULL,
	-- Coherencia origen - medicion: 'ble' exige medicion_ble_id (y lorawan NULL) y viceversa
	CONSTRAINT chk_origen_medicion_presion_temp CHECK (((((origen)::text = 'ble'::text) AND (medicion_ble_id IS NOT NULL) AND (medicion_lorawan_id IS NULL)) OR (((origen)::text = 'lorawan'::text) AND (medicion_lorawan_id IS NOT NULL) AND (medicion_ble_id IS NULL)))),
	-- Restringe 'origen' a las dos fuentes posibles
	CONSTRAINT indicadores_presion_temperatura_origen_check CHECK (((origen)::text = ANY ((ARRAY['ble'::character varying, 'lorawan'::character varying])::text[]))),
	-- Clave primaria: identificador sintetico de la fila
	CONSTRAINT indicadores_presion_temperatura_pkey PRIMARY KEY (id),
	-- Deduplicacion: un unico indicador por dispositivo, instante y fuente
	CONSTRAINT uq_indicadores_presion_temp_disp_ts_origen UNIQUE (dispositivo_id, "timestamp", origen),
	-- FK: el dispositivo debe existir en dispositivos
	CONSTRAINT indicadores_presion_temperatura_dispositivo_id_fkey FOREIGN KEY (dispositivo_id) REFERENCES public.dispositivos(id),
	-- FK: si viene de BLE, la medicion debe existir en mediciones_lentas_ble
	CONSTRAINT indicadores_presion_temperatura_medicion_ble_id_fkey FOREIGN KEY (medicion_ble_id) REFERENCES public.mediciones_lentas_ble(id),
	-- FK: si viene de LoRaWAN, la medicion debe existir en mediciones_lentas_lorawan
	CONSTRAINT indicadores_presion_temperatura_medicion_lorawan_id_fkey FOREIGN KEY (medicion_lorawan_id) REFERENCES public.mediciones_lentas_lorawan(id)
);
-- Consulta tipica: serie por dispositivo en orden temporal descendente
CREATE INDEX indicadores_presion_temperatura_dispositivo_id_timestamp_idx ON public.indicadores_presion_temperatura USING btree (dispositivo_id, "timestamp" DESC);
