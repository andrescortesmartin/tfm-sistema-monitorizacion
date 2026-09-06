CREATE TABLE public.indicadores_imu_1s (
	id serial4 NOT NULL, 
	dispositivo_id int4 NOT NULL,                -- dispositivo de origen (FK dispositivos)
	"timestamp" timestamptz NOT NULL,           -- segundo agregado (inicio del intervalo de 1 s)
	n_muestras int2 NOT NULL,                    -- muestras IMU usadas en el calculo de ese segundo
	actividad int2 NULL,                         -- tipo de actividad detectado
	vedba_media float8 NULL,                     -- VeDBA media del segundo (aceleracion dinamica vectorial)
	vedba_max float8 NULL,                       -- VeDBA maxima del segundo
	pitch_medio float8 NULL,                     -- pitch medio del segundo (grados)
	roll_medio float8 NULL,                      -- roll medio del segundo (grados)
	sector_raw_id int4 NULL,                     -- FK al sector crudo de origen (sectores_raw)
	medicion_rapida_id int4 NULL,               -- FK a la medicion rapida de origen (mediciones_rapidas)
	comportamiento_predicho varchar NULL,        -- etiqueta de comportamiento inferida (modelo), NULL si no se ha clasificado
	-- Deduplicacion: un unico indicador por dispositivo y segundo
	CONSTRAINT indicadores_imu_dispositivo_id_timestamp_key UNIQUE (dispositivo_id, "timestamp"),
	-- Clave primaria: identificador sintetico de la fila
	CONSTRAINT indicadores_imu_pkey PRIMARY KEY (id),
	-- FK: el dispositivo debe existir en dispositivos
	CONSTRAINT indicadores_imu_dispositivo_id_fkey FOREIGN KEY (dispositivo_id) REFERENCES public.dispositivos(id),
	-- FK: si viene de una medicion rapida, debe existir en mediciones_rapidas
	CONSTRAINT indicadores_imu_medicion_rapida_id_fkey FOREIGN KEY (medicion_rapida_id) REFERENCES public.mediciones_rapidas(id),
	-- FK: si tiene sector crudo asociado, debe existir en sectores_raw
	CONSTRAINT indicadores_imu_sector_raw_id_fkey FOREIGN KEY (sector_raw_id) REFERENCES public.sectores_raw(id)
);
-- Consulta: series por dispositivo y (opcionalmente) por actividad, en orden temporal descendente
CREATE INDEX indicadores_imu_dispositivo_id_actividad_timestamp_idx ON public.indicadores_imu_1s USING btree (dispositivo_id, actividad, "timestamp" DESC);
CREATE INDEX indicadores_imu_dispositivo_id_timestamp_idx ON public.indicadores_imu_1s USING btree (dispositivo_id, "timestamp" DESC);

