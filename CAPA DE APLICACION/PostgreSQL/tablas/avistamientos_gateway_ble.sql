CREATE TABLE public.avistamientos_gateway_ble (
	id bigserial NOT NULL,
	metrica_origen_id int4 NOT NULL,            -- traza de que heartbeat genero la fila
	gateway_id text NOT NULL,                   -- identificador del gateway BLE que detecta (texto, no FK)
	device_mac text NOT NULL,                   -- MAC del dispositivo detectado, siempre presente
	device_id int4 NULL,                        -- FK a dispositivos; NULL si la MAC no esta dada de alta
	rssi int2 NOT NULL,                         -- potencia recibida en dBm
	ts_evento timestamptz NOT NULL,             -- instante de la deteccion (reloj del gateway)
	ts_insercion timestamptz DEFAULT now() NOT NULL, -- instante de insercion en la BD
	CONSTRAINT avistamientos_gateway_ble_pkey PRIMARY KEY (id),
	CONSTRAINT avistamientos_gateway_ble_device_id_fkey FOREIGN KEY (device_id) REFERENCES public.dispositivos(id),
	CONSTRAINT avistamientos_gateway_ble_metrica_origen_id_fkey FOREIGN KEY (metrica_origen_id) REFERENCES public.metricas_sistema(id)
);
-- Consultas por dispositivo / por gateway en una ventana de tiempo
CREATE INDEX idx_avistamiento_device_ts ON public.avistamientos_gateway_ble USING btree (device_id, ts_evento);
CREATE INDEX idx_avistamiento_gateway_ts ON public.avistamientos_gateway_ble USING btree (gateway_id, ts_evento);
CREATE INDEX idx_avistamiento_metrica_origen ON public.avistamientos_gateway_ble USING btree (metrica_origen_id);
-- Deduplicacion: un gateway no registra la misma MAC dos veces en el mismo instante (ingesta idempotente)
CREATE UNIQUE INDEX uq_avistamiento_gw_device_evento ON public.avistamientos_gateway_ble USING btree (gateway_id, device_mac, ts_evento);

-- Table Triggers

-- Tras cada insert, actualiza la agregacion horaria de proximidad
create trigger trg_proximidad_gateway_ble after
insert
    on
    public.avistamientos_gateway_ble for each row execute function fn_trg_proximidad_gateway_ble();
