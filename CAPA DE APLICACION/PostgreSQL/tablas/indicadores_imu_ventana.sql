CREATE TABLE public.indicadores_imu_ventana (
	id serial4 NOT NULL,
	dispositivo_id int4 NOT NULL,               -- dispositivo de origen (FK dispositivos)
	sector_raw_id int4 NOT NULL,                -- sector crudo al que pertenece el bloque (FK sectores_raw)
	numero_bloque int4 NOT NULL,               -- indice del bloque dentro del sector
	timestamp_inicio timestamptz NOT NULL,     -- inicio de la ventana
	timestamp_fin timestamptz NOT NULL,        -- fin de la ventana
	duracion_s float8 NULL,                     -- duracion real de la ventana, en segundos
	n_muestras int4 NULL,                       -- muestras IMU incluidas en la ventana
	actividad int2 NULL,                        -- tipo de actividad detectado
	vedba_media float8 NULL,                    -- VeDBA media de la ventana
	vedba_max float8 NULL,                      -- VeDBA maxima de la ventana
	dft_freq_dominante_x float8 NULL,           -- frecuencia del pico dominante del espectro, eje X (Hz)
	dft_energia_dominante_x float8 NULL,        -- energia en ese pico dominante, eje X
	dft_freq_dominante_y float8 NULL,           -- idem eje Y
	dft_energia_dominante_y float8 NULL,        -- idem eje Y
	dft_freq_dominante_z float8 NULL,           -- idem eje Z
	dft_energia_dominante_z float8 NULL,        -- idem eje Z
	dft_espectro jsonb NULL,                    -- espectro completo (pares frecuencia/energia) serializado
	primera_medicion_id int4 NULL,             -- FK a la primera medicion rapida de la ventana (mediciones_rapidas)
	ultima_medicion_id int4 NULL,              -- FK a la ultima medicion rapida de la ventana (mediciones_rapidas)
	creado_en timestamptz DEFAULT now() NOT NULL,
	dft_energia_total_x float8 NULL,            -- energia total del espectro, eje X
	dft_energia_total_y float8 NULL,            -- idem eje Y
	dft_energia_total_z float8 NULL,            -- idem eje Z
	dft_centroide_x float8 NULL,                -- centroide espectral (frecuencia media ponderada), eje X (Hz)
	dft_centroide_y float8 NULL,                -- idem eje Y
	dft_centroide_z float8 NULL,                -- idem eje Z
	dft_ancho_banda_x float8 NULL,              -- ancho de banda espectral (dispersion en torno al centroide), eje X (Hz)
	dft_ancho_banda_y float8 NULL,              -- idem eje Y
	dft_ancho_banda_z float8 NULL,              -- idem eje Z
	-- Deduplicacion: un unico indicador por dispositivo, sector crudo y numero de bloque
	CONSTRAINT indicadores_imu_ventana_bloque_key UNIQUE (dispositivo_id, sector_raw_id, numero_bloque),
	-- Clave primaria: identificador sintetico de la fila
	CONSTRAINT indicadores_imu_ventana_pkey PRIMARY KEY (id),
	-- FK: el dispositivo debe existir en dispositivos
	CONSTRAINT indicadores_imu_ventana_dispositivo_id_fkey FOREIGN KEY (dispositivo_id) REFERENCES public.dispositivos(id),
	-- FK: la primera medicion, si se indica, debe existir en mediciones_rapidas
	CONSTRAINT indicadores_imu_ventana_primera_medicion_id_fkey FOREIGN KEY (primera_medicion_id) REFERENCES public.mediciones_rapidas(id),
	-- FK: el sector crudo debe existir en sectores_raw
	CONSTRAINT indicadores_imu_ventana_sector_raw_id_fkey FOREIGN KEY (sector_raw_id) REFERENCES public.sectores_raw(id),
	-- FK: la ultima medicion, si se indica, debe existir en mediciones_rapidas
	CONSTRAINT indicadores_imu_ventana_ultima_medicion_id_fkey FOREIGN KEY (ultima_medicion_id) REFERENCES public.mediciones_rapidas(id)
);
