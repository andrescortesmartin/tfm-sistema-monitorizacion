CREATE TABLE public.sectores_raw (
	id serial4 NOT NULL,
	dispositivo_id int4 NOT NULL,          -- logger de origen (FK dispositivos)
	session_id int4 NOT NULL,              -- identificador de la sesion/descarga a la que pertenece el sector
	tipo varchar(4) NOT NULL,              -- formato del contenido: 'lora' (paquete periodico) | 'imu' (volcado FIFO)
	recibido_en timestamptz DEFAULT now() NOT NULL,  -- instante de recepcion en el servidor
	"data" bytea NOT NULL,                 -- bytes crudos del sector, tal cual llegan del logger
	-- Clave primaria: identificador sintetico de la fila
	CONSTRAINT sectores_raw_pkey PRIMARY KEY (id),
	-- Restringe 'tipo' a los dos formatos de sector contemplados
	CONSTRAINT sectores_raw_tipo_check CHECK (((tipo)::text = ANY ((ARRAY['lora'::character varying, 'imu'::character varying])::text[]))),
	-- FK: el dispositivo debe existir en dispositivos
	CONSTRAINT sectores_raw_dispositivo_id_fkey FOREIGN KEY (dispositivo_id) REFERENCES public.dispositivos(id)
);
-- Acceso a los sectores de una descarga concreta (dispositivo + sesion + tipo)
CREATE INDEX sectores_raw_dispositivo_id_session_id_tipo_idx ON public.sectores_raw USING btree (dispositivo_id, session_id, tipo);

-- Table Triggers

-- Sector IMU: decodifica el volcado de la FIFO al insertarse
create trigger trigger_decodificar_imu after
insert
    on
    public.sectores_raw for each row
    when (((new.tipo)::text = 'imu'::text)) execute function decodificar_sector_imu();

-- Sector LoRa: decodifica el paquete periodico al insertarse
create trigger trigger_decodificar_periodico after
insert
    on
    public.sectores_raw for each row
    when (((new.tipo)::text = 'lora'::text)) execute function decodificar_sector_periodico();
