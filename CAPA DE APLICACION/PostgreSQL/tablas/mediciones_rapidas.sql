CREATE TABLE public.mediciones_rapidas (
	id serial4 NOT NULL,
	dispositivo_id int4 NOT NULL,            -- nodo que genero la muestra (FK dispositivos)
	"timestamp" timestamptz NOT NULL,        -- instante de la muestra (reconstruido a partir del ts del bloque y la ODR)
	n_muestra int2 NOT NULL,                 -- indice de la muestra dentro del bloque/volcado
	actividad int2 NOT NULL,                 -- tipo de actividad asociado
	tipo varchar(4) NOT NULL,                -- que ejes trae la fila: 'xl' | 'gy' | 'xl_gy'
	xl_x int2 NULL,                          -- acelerometro eje X (cuentas int16)
	xl_y int2 NULL,                          -- acelerometro eje Y
	xl_z int2 NULL,                          -- acelerometro eje Z
	gy_x int2 NULL,                          -- giroscopo eje X (cuentas int16)
	gy_y int2 NULL,                          -- giroscopo eje Y
	gy_z int2 NULL,                          -- giroscopo eje Z
	sector_raw_id int4 NULL,                 -- sector crudo al que pertenece la muestra (FK sectores_raw)
	din_x float8 NULL,                       -- aceleracion dinamica eje X (tras quitar la componente estatica/gravedad)
	din_y float8 NULL,                       -- aceleracion dinamica eje Y
	din_z float8 NULL,                       -- aceleracion dinamica eje Z
	numero_bloque int4 NULL,                 -- numero de bloque dentro del sector (agrupa muestras del mismo volcado)
	-- Clave primaria: identificador sintetico de la fila
	CONSTRAINT mediciones_rapidas_pkey PRIMARY KEY (id),
	-- Restringe 'tipo' a las combinaciones de ejes contempladas
	CONSTRAINT mediciones_rapidas_tipo_check CHECK (((tipo)::text = ANY ((ARRAY['xl'::character varying, 'gy'::character varying, 'xl_gy'::character varying])::text[]))),
	-- FK: el dispositivo debe existir en dispositivos
	CONSTRAINT mediciones_rapidas_dispositivo_id_fkey FOREIGN KEY (dispositivo_id) REFERENCES public.dispositivos(id),
	-- FK: si tiene sector crudo asociado, debe existir en sectores_raw
	CONSTRAINT mediciones_rapidas_sector_raw_id_fkey FOREIGN KEY (sector_raw_id) REFERENCES public.sectores_raw(id)
);
-- Acceso por bloque (recalculo de indicadores por ventana)
CREATE INDEX idx_mediciones_rapidas_bloque ON public.mediciones_rapidas USING btree (dispositivo_id, sector_raw_id, numero_bloque);
-- Deduplicacion: una sola muestra por dispositivo, instante y tipo de ejes (ingesta idempotente)
CREATE UNIQUE INDEX mediciones_rapidas_disp_ts_tipo_uk ON public.mediciones_rapidas USING btree (dispositivo_id, "timestamp", tipo);
-- Consulta tipica: serie por dispositivo en orden temporal descendente
CREATE INDEX mediciones_rapidas_dispositivo_id_timestamp_ref_idx ON public.mediciones_rapidas USING btree (dispositivo_id, "timestamp" DESC);
