-- DROP FUNCTION public.fn_trg_proximidad_gateway_ble();

CREATE OR REPLACE FUNCTION public.fn_trg_proximidad_gateway_ble()
 RETURNS trigger
 LANGUAGE plpgsql
AS $function$
DECLARE
    v_umbral_inmediata  SMALLINT;    -- RSSI mínimo para "inmediata"
    v_umbral_cercana    SMALLINT;    -- RSSI mínimo para "cercana"
    v_anillo            TEXT;        -- anillo resultante para este avistamiento
    v_hora_inicio       TIMESTAMPTZ; -- inicio de la hora local a la que se imputa
BEGIN
    -- Solo se agregan avistamientos de dispositivos identificados
    IF NEW.device_id IS NULL THEN
        RETURN NEW;
    END IF;

    -- Umbrales de anillo de la última config de despliegue activa
    SELECT anillo_gateway_rssi_umbral_inmediata, anillo_gateway_rssi_umbral_cercana
      INTO v_umbral_inmediata, v_umbral_cercana
      FROM config_despliegue
     WHERE activo = true
     ORDER BY fecha_creacion DESC
     LIMIT 1;

    IF v_umbral_inmediata IS NULL THEN
        RAISE EXCEPTION 'No hay config_despliegue activa: no se puede clasificar anillo RSSI';
    END IF;

    -- Clasificación por tramos de RSSI (mayor RSSI = más cerca)
    IF NEW.rssi >= v_umbral_inmediata THEN
        v_anillo := 'inmediata';
    ELSIF NEW.rssi >= v_umbral_cercana THEN
        v_anillo := 'cercana';
    ELSE
        v_anillo := 'lejana';
    END IF;

    -- Hora local (Europe/Madrid) a la que pertenece el avistamiento
    v_hora_inicio := (date_trunc('hour', NEW.ts_evento AT TIME ZONE 'Europe/Madrid')) AT TIME ZONE 'Europe/Madrid';

    -- Upsert del resumen horario: la primera vez crea la fila; a partir de ahí incrementa el contador y recalcula RSSI medio (media incremental),
    -- RSSI máx y el rango primer/último avistamiento.
    INSERT INTO proximidad_gateway_ble_hora (
        dispositivo_id, gateway_id, anillo, hora_inicio,
        n_avistamientos, rssi_medio, rssi_max,
        primer_avistamiento, ultimo_avistamiento
    )
    VALUES (
        NEW.device_id, NEW.gateway_id, v_anillo, v_hora_inicio,
        1, NEW.rssi, NEW.rssi,
        NEW.ts_evento, NEW.ts_evento
    )
    ON CONFLICT (dispositivo_id, gateway_id, anillo, hora_inicio) DO UPDATE
    SET n_avistamientos     = proximidad_gateway_ble_hora.n_avistamientos + 1,
        rssi_medio          = (proximidad_gateway_ble_hora.rssi_medio * proximidad_gateway_ble_hora.n_avistamientos + NEW.rssi)
                               / (proximidad_gateway_ble_hora.n_avistamientos + 1),
        rssi_max            = GREATEST(proximidad_gateway_ble_hora.rssi_max, NEW.rssi),
        primer_avistamiento = LEAST(proximidad_gateway_ble_hora.primer_avistamiento, NEW.ts_evento),
        ultimo_avistamiento = GREATEST(proximidad_gateway_ble_hora.ultimo_avistamiento, NEW.ts_evento);

    RETURN NEW;
END;
$function$
;
