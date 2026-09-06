-- DROP FUNCTION public.procesar_avistamientos_ble();

CREATE OR REPLACE FUNCTION public.procesar_avistamientos_ble()
 RETURNS trigger
 LANGUAGE plpgsql
AS $function$
DECLARE
    v_elemento JSONB;                  -- avistamiento en curso (elemento del array JSON)
    v_visto_id INTEGER;                -- id corto del dispositivo visto (según el firmware)
    v_visto_dispositivo_id INTEGER;    -- id del dispositivo visto en la tabla dispositivos
    v_obs SMALLINT;                    -- nº de observaciones en la ventana
    v_rssi SMALLINT;                   -- RSSI del avistamiento
    v_origen TEXT;                     -- 'ble' o 'lorawan' según la tabla que dispara el trigger
BEGIN
    -- El origen depende de qué tabla lanzó el trigger
    v_origen := CASE WHEN TG_TABLE_NAME = 'mediciones_lentas_ble' THEN 'ble' ELSE 'lorawan' END;

    -- LOGGERS
    IF NEW.loggers IS NOT NULL THEN
        FOR v_elemento IN SELECT * FROM jsonb_array_elements(NEW.loggers)
        LOOP
            v_visto_id := (v_elemento->>'id')::INTEGER;
            v_rssi := (v_elemento->>'rssi')::SMALLINT;
            v_obs := (v_elemento->>'obs')::SMALLINT;

            -- Se ignora el avistamiento si el dispositivo visto no está registrado
            v_visto_dispositivo_id := resolver_dispositivo_ble(v_visto_id, 'logger');
            IF v_visto_dispositivo_id IS NULL THEN
                CONTINUE;
            END IF;

            -- Upsert: en conflicto solo se pisa el dato si el nuevo trae RSSI; el anillo se recalcula siempre con el RSSI que quede.
            INSERT INTO avistamientos_por_ventana
                (dispositivo_id, visto_dispositivo_id, tipo, ts_ventana, obs, rssi, anillo, origen,
                 medicion_ble_id, medicion_lorawan_id)
            VALUES
                (NEW.dispositivo_id, v_visto_dispositivo_id, 'logger', NEW.timestamp, v_obs, v_rssi,
                 fn_clasificar_anillo_ble(v_rssi), v_origen,
                 CASE WHEN v_origen = 'ble' THEN NEW.id ELSE NULL END,
                 CASE WHEN v_origen = 'lorawan' THEN NEW.id ELSE NULL END)
            ON CONFLICT (dispositivo_id, visto_dispositivo_id, tipo, ts_ventana)
            DO UPDATE SET
                obs                 = COALESCE(EXCLUDED.obs, avistamientos_por_ventana.obs),
                rssi                = COALESCE(EXCLUDED.rssi, avistamientos_por_ventana.rssi),
                anillo              = fn_clasificar_anillo_ble(COALESCE(EXCLUDED.rssi, avistamientos_por_ventana.rssi)),
                origen              = CASE WHEN EXCLUDED.rssi IS NOT NULL THEN EXCLUDED.origen ELSE avistamientos_por_ventana.origen END,
                medicion_ble_id     = COALESCE(EXCLUDED.medicion_ble_id, avistamientos_por_ventana.medicion_ble_id),
                medicion_lorawan_id = COALESCE(EXCLUDED.medicion_lorawan_id, avistamientos_por_ventana.medicion_lorawan_id);
        END LOOP;
    END IF;

    -- BEACONS (misma lógica que loggers, con tipo = 'beacon')
    IF NEW.beacons IS NOT NULL THEN
        FOR v_elemento IN SELECT * FROM jsonb_array_elements(NEW.beacons)
        LOOP
            v_visto_id := (v_elemento->>'id')::INTEGER;
            v_rssi := (v_elemento->>'rssi')::SMALLINT;
            v_obs := (v_elemento->>'obs')::SMALLINT;

            v_visto_dispositivo_id := resolver_dispositivo_ble(v_visto_id, 'beacon');
            IF v_visto_dispositivo_id IS NULL THEN
                CONTINUE;
            END IF;

            INSERT INTO avistamientos_por_ventana
                (dispositivo_id, visto_dispositivo_id, tipo, ts_ventana, obs, rssi, anillo, origen,
                 medicion_ble_id, medicion_lorawan_id)
            VALUES
                (NEW.dispositivo_id, v_visto_dispositivo_id, 'beacon', NEW.timestamp, v_obs, v_rssi,
                 fn_clasificar_anillo_ble(v_rssi), v_origen,
                 CASE WHEN v_origen = 'ble' THEN NEW.id ELSE NULL END,
                 CASE WHEN v_origen = 'lorawan' THEN NEW.id ELSE NULL END)
            ON CONFLICT (dispositivo_id, visto_dispositivo_id, tipo, ts_ventana)
            DO UPDATE SET
                obs                 = COALESCE(EXCLUDED.obs, avistamientos_por_ventana.obs),
                rssi                = COALESCE(EXCLUDED.rssi, avistamientos_por_ventana.rssi),
                anillo              = fn_clasificar_anillo_ble(COALESCE(EXCLUDED.rssi, avistamientos_por_ventana.rssi)),
                origen              = CASE WHEN EXCLUDED.rssi IS NOT NULL THEN EXCLUDED.origen ELSE avistamientos_por_ventana.origen END,
                medicion_ble_id     = COALESCE(EXCLUDED.medicion_ble_id, avistamientos_por_ventana.medicion_ble_id),
                medicion_lorawan_id = COALESCE(EXCLUDED.medicion_lorawan_id, avistamientos_por_ventana.medicion_lorawan_id);
        END LOOP;
    END IF;

    RETURN NEW;
END;
$function$
;
