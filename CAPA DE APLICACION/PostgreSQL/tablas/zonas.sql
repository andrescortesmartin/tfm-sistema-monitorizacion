CREATE TABLE public.zonas (
	id serial4 NOT NULL,
	config_despliegue_id int4 NOT NULL,   -- despliegue al que pertenece la malla (FK config_despliegue)
	zona_col int4 NOT NULL,               -- indice de columna de la celda dentro de la malla
	zona_fila int4 NOT NULL,              -- indice de fila de la celda dentro de la malla
	lat_min float8 NOT NULL,              -- limite sur de la celda
	lat_max float8 NOT NULL,              -- limite norte de la celda
	lon_min float8 NOT NULL,              -- limite oeste de la celda
	lon_max float8 NOT NULL,              -- limite este de la celda
	nombre text NULL,                     -- nombre legible opcional de la zona
	fecha_creacion timestamptz DEFAULT now() NOT NULL,
	-- Deduplicacion: una sola celda por (despliegue, columna, fila)
	CONSTRAINT zonas_config_grid_id_zona_col_zona_fila_key UNIQUE (config_despliegue_id, zona_col, zona_fila),
	-- Clave primaria: identificador sintetico de la fila
	CONSTRAINT zonas_pkey PRIMARY KEY (id),
	-- FK: el despliegue debe existir en config_despliegue
	CONSTRAINT zonas_config_despliegue_id_fkey FOREIGN KEY (config_despliegue_id) REFERENCES public.config_despliegue(id)
);
