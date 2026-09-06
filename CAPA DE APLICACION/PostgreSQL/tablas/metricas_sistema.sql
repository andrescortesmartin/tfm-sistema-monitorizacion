CREATE TABLE public.metricas_sistema (
	id serial4 NOT NULL,
	dispositivo_id int4 NOT NULL,         -- dispositivo que emite la metrica (FK dispositivos)
	"timestamp" timestamptz NOT NULL,     -- instante del reporte
	tipo varchar(16) NOT NULL,            -- tipo de dispositivo 
	datos jsonb NOT NULL,                 -- payload de la metrica; su forma depende de 'tipo'
	-- Deduplicacion: un unico reporte por dispositivo, instante y tipo
	CONSTRAINT metricas_sistema_dispositivo_id_timestamp_tipo_key UNIQUE (dispositivo_id, "timestamp", tipo),
	-- Clave primaria: identificador sintetico de la fila
	CONSTRAINT metricas_sistema_pkey PRIMARY KEY (id),
	-- Restringe 'tipo' a las clases de equipo contempladas
	CONSTRAINT metricas_sistema_tipo_check CHECK (((tipo)::text = ANY ((ARRAY['logger'::character varying, 'gateway_ble'::character varying, 'ancla_lora'::character varying, 'gateway_lorawan'::character varying])::text[]))),
	-- FK: el dispositivo debe existir en dispositivos
	CONSTRAINT metricas_sistema_dispositivo_id_fkey FOREIGN KEY (dispositivo_id) REFERENCES public.dispositivos(id)
);
-- Consulta por dispositivo en orden temporal descendente
CREATE INDEX metricas_sistema_dispositivo_id_timestamp_idx ON public.metricas_sistema USING btree (dispositivo_id, "timestamp" DESC);
-- Consulta por clase de equipo en orden temporal descendente
CREATE INDEX metricas_sistema_tipo_timestamp_idx ON public.metricas_sistema USING btree (tipo, "timestamp" DESC);

-- Table Triggers

-- Solo para reportes de gateway_ble: extrae de 'datos' los dispositivos vistos y los inserta en avistamientos_gateway_ble
create trigger trg_ingestar_avistamiento_gateway_ble after
insert
    on
    public.metricas_sistema for each row
    when (((new.tipo)::text = 'gateway_ble'::text)) execute function fn_ingestar_avistamiento_gateway_ble();
