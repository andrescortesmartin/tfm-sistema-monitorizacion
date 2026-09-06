CREATE TABLE public.indicadores_posicion_ranging (
	id serial4 NOT NULL,
	dispositivo_id int4 NOT NULL,              -- dispositivo posicionado (FK dispositivos)
	medicion_ble_id int4 NULL,                 -- FK a la medicion BLE de origen (si origen='ble')
	medicion_lorawan_id int4 NULL,             -- FK a la medicion LoRaWAN de origen (si origen='lorawan')
	origen varchar(10) NOT NULL,               -- fuente del ranging: 'ble' | 'lorawan'
	"timestamp" timestamptz NOT NULL,          -- instante al que corresponde la posicion
	lat_estimada float8 NULL,                  -- latitud estimada (NULL si la solucion no converge)
	lon_estimada float8 NULL,                  -- longitud estimada (NULL si la solucion no converge)
	n_anclas_usadas int2 NULL,                 -- numero de anclas que entraron en el calculo
	residual_medio float4 NULL,                -- residual medio del ajuste (m): calidad de la solucion
	residual_max float4 NULL,                  -- residual maximo del ajuste (m)
	creado_en timestamptz DEFAULT now() NOT NULL,
	hdop float8 NULL,                          -- dilucion horizontal de la precision (geometria de las anclas)
	-- Coherencia origen - medicion: 'ble' exige medicion_ble_id (y lorawan NULL) y viceversa
	CONSTRAINT chk_origen_medicion_pos CHECK (((((origen)::text = 'ble'::text) AND (medicion_ble_id IS NOT NULL) AND (medicion_lorawan_id IS NULL)) OR (((origen)::text = 'lorawan'::text) AND (medicion_lorawan_id IS NOT NULL) AND (medicion_ble_id IS NULL)))),
	-- Restringe 'origen' a las dos fuentes posibles
	CONSTRAINT indicadores_posicion_ranging_origen_check CHECK (((origen)::text = ANY ((ARRAY['ble'::character varying, 'lorawan'::character varying])::text[]))),
	-- Clave primaria: identificador sintetico de la fila
	CONSTRAINT indicadores_posicion_ranging_pkey PRIMARY KEY (id),
	-- FK: el dispositivo debe existir en dispositivos
	CONSTRAINT indicadores_posicion_ranging_dispositivo_id_fkey FOREIGN KEY (dispositivo_id) REFERENCES public.dispositivos(id),
	-- FK: si viene de BLE, la medicion debe existir en mediciones_lentas_ble
	CONSTRAINT indicadores_posicion_ranging_medicion_ble_id_fkey FOREIGN KEY (medicion_ble_id) REFERENCES public.mediciones_lentas_ble(id),
	-- FK: si viene de LoRaWAN, la medicion debe existir en mediciones_lentas_lorawan
	CONSTRAINT indicadores_posicion_ranging_medicion_lorawan_id_fkey FOREIGN KEY (medicion_lorawan_id) REFERENCES public.mediciones_lentas_lorawan(id)
);
-- Consulta tipica: trayectoria de un dispositivo en orden temporal
CREATE INDEX idx_indicadores_posicion_ranging_dispositivo ON public.indicadores_posicion_ranging USING btree (dispositivo_id, "timestamp");

-- Table Triggers

-- Al insertar o al fijarse lat/lon, actualiza la agregacion de posicionamiento por zona/hora
create trigger trg_posicionamiento_ranging after
insert
    or
update
    of lat_estimada,
    lon_estimada on
    public.indicadores_posicion_ranging for each row execute function fn_trg_posicionamiento_ranging();
