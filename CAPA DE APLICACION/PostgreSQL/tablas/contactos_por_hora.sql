CREATE TABLE public.contactos_por_hora (
	id bigserial NOT NULL,
	dispositivo_id int4 NOT NULL,              -- dispositivo observador (FK dispositivos)
	visto_dispositivo_id int4 NOT NULL,        -- dispositivo detectado (FK dispositivos)
	tipo text NOT NULL,                        -- tipo del dispositivo visto: 'logger' | 'beacon'
	slot_inicio timestamptz NOT NULL,          -- inicio de la hora agregada (truncado a hora)
	n_ventanas int4 DEFAULT 0 NOT NULL,        -- numero de ventanas con contacto dentro de la hora
	primer_contacto timestamptz NOT NULL,      -- ts del primer avistamiento de la hora
	ultimo_contacto timestamptz NOT NULL,      -- ts del ultimo avistamiento de la hora
	rssi_medio float4 NULL,                    -- media del RSSI sobre las ventanas con RSSI valido
	rssi_max int2 NULL,                        -- RSSI maximo observado en la hora (dBm)
	actualizado_en timestamptz DEFAULT now() NOT NULL,  -- ultima modificacion de la fila (cada upsert)
	n_rssi_validos int4 DEFAULT 0 NOT NULL,    -- ventanas con RSSI no nulo (denominador de rssi_medio)
	n_inmediata int4 DEFAULT 0 NOT NULL,       -- ventanas clasificadas en anillo 'inmediata'
	n_cercana int4 DEFAULT 0 NOT NULL,         -- ventanas clasificadas en anillo 'cercana'
	n_lejana int4 DEFAULT 0 NOT NULL,          -- ventanas clasificadas en anillo 'lejana'
	-- Deduplicacion / clave de upsert: un unico registro por (observador, visto, tipo, hora)
	CONSTRAINT contactos_por_hora_dispositivo_id_visto_dispositivo_id_tipo_key UNIQUE (dispositivo_id, visto_dispositivo_id, tipo, slot_inicio),
	-- Clave primaria: identificador sintetico de la fila
	CONSTRAINT contactos_por_hora_pkey PRIMARY KEY (id),
	-- Restringe 'tipo' a los tipos de dispositivo visto contemplados
	CONSTRAINT contactos_por_hora_tipo_check CHECK ((tipo = ANY (ARRAY['logger'::text, 'beacon'::text]))),
	-- FK: el observador debe existir en dispositivos
	CONSTRAINT contactos_por_hora_dispositivo_id_fkey FOREIGN KEY (dispositivo_id) REFERENCES public.dispositivos(id),
	-- FK: el dispositivo visto debe existir en dispositivos
	CONSTRAINT contactos_por_hora_visto_dispositivo_id_fkey FOREIGN KEY (visto_dispositivo_id) REFERENCES public.dispositivos(id)
);
