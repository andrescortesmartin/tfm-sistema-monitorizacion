CREATE TABLE public.dispositivos (
	id serial4 NOT NULL,
	identificador text NOT NULL,                 -- identificador unico del dispositivo (MAC / DevEUI segun tipo)
	nombre varchar(64) NULL,                     -- nombre legible opcional
	descripcion text NULL,                       -- notas libres
	creado_en timestamptz DEFAULT now() NOT NULL,
	tipo varchar(16) DEFAULT 'logger'::character varying NOT NULL,  -- clase de dispositivo (ver CHECK)
	lat float8 NULL,                             -- latitud fija (dispositivos estaticos: anclas, gateways)
	lon float8 NULL,                             -- longitud fija (dispositivos estaticos: anclas, gateways)
	-- 'identificador' no se repite: sirve para resolver las FK desde la ingesta
	CONSTRAINT dispositivos_mac_key UNIQUE (identificador),
	-- Clave primaria: identificador sintetico usado en todas las FK
	CONSTRAINT dispositivos_pkey PRIMARY KEY (id),
	-- Restringe 'tipo' a las clases de dispositivo contempladas
	CONSTRAINT dispositivos_tipo_check CHECK (((tipo)::text = ANY (ARRAY['logger'::text, 'gateway_ble'::text, 'ancla_lora'::text, 'gateway_lorawan'::text, 'nanobeacon'::text])))
);
