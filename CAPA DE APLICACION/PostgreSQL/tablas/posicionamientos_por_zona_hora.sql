CREATE TABLE public.posicionamientos_por_zona_hora (
	id serial4 NOT NULL,
	dispositivo_id int4 NOT NULL,          -- dispositivo posicionado (FK dispositivos)
	zona_id int4 NOT NULL,                 -- zona en la que cae la posicion (FK zonas)
	hora timestamptz NOT NULL,             -- inicio de la hora agregada (truncado a hora)
	origen_posicion text NOT NULL,         -- fuente de la fijacion (p. ej. 'gps' | 'ranging')
	n_fijaciones int4 DEFAULT 0 NOT NULL,  -- numero de fijaciones acumuladas en esa zona/hora
	-- Deduplicacion / clave de upsert: una fila por (dispositivo, zona, hora, origen)
	CONSTRAINT posicionamientos_por_zona_hor_dispositivo_id_zona_id_hora_o_key UNIQUE (dispositivo_id, zona_id, hora, origen_posicion),
	-- Clave primaria: identificador sintetico de la fila
	CONSTRAINT posicionamientos_por_zona_hora_pkey PRIMARY KEY (id),
	-- FK: el dispositivo debe existir en dispositivos
	CONSTRAINT posicionamientos_por_zona_hora_dispositivo_id_fkey FOREIGN KEY (dispositivo_id) REFERENCES public.dispositivos(id),
	-- FK: la zona debe existir en zonas
	CONSTRAINT posicionamientos_por_zona_hora_zona_id_fkey FOREIGN KEY (zona_id) REFERENCES public.zonas(id)
);
