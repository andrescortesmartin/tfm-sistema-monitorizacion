CREATE TABLE public.config_despliegue (
	id serial4 NOT NULL,
	nombre text NOT NULL,                        -- nombre del despliegue
	lat_origen float8 NOT NULL,                  -- latitud del origen de la malla de celdas
	lon_origen float8 NOT NULL,                  -- longitud del origen de la malla de celdas
	tamano_celda_m float8 NOT NULL,              -- lado de cada celda de la malla, en metros
	escala_acelerometro_g float8 DEFAULT 4.0 NOT NULL,   -- fondo de escala del acelerometro (+/- g) usado al decodificar
	max_n_muestras_dft int4 DEFAULT 2000 NOT NULL,       -- limite de muestras que entran en la DFT
	ventana_din_s float8 DEFAULT 2.0 NOT NULL,           -- ventana de media movil para caculo de la componente dinamica, en segundos
	filtro_rssi_gateway_esperado int2 NULL,             -- RSSI del gateway (filtro), NULL = sin filtro
	umbral_descarga_gateway_esperado int4 NULL,         -- umbral de descarga del gateway, NULL = sin definir
	activo bool DEFAULT true NOT NULL,                   -- TRUE = configuracion de despliegue en uso
	fecha_creacion timestamptz DEFAULT now() NOT NULL,
	anillo_gateway_rssi_umbral_inmediata int2 DEFAULT '-50'::integer NOT NULL,  -- >= este RSSI (gateway-dispositivo) implica anillo 'inmediata'
	anillo_gateway_rssi_umbral_cercana int2 DEFAULT '-70'::integer NOT NULL,    -- >= este RSSI (y < inmediata) implica anillo 'cercana'; por debajo => 'lejana'
	anillo_logger_rssi_umbral_inmediata int2 DEFAULT '-60'::integer NOT NULL,   -- idem para avistamientos entre loggers: umbral 'inmediata'
	anillo_logger_rssi_umbral_cercana int2 DEFAULT '-80'::integer NOT NULL,     -- idem loggers: umbral 'cercana'
	CONSTRAINT config_despliegue_pkey PRIMARY KEY (id)   -- clave primaria: identificador sintetico
);
