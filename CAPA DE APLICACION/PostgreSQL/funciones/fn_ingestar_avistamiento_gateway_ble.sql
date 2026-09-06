-- DROP FUNCTION public.fn_ingestar_avistamiento_gateway_ble();

CREATE OR REPLACE FUNCTION public.fn_ingestar_avistamiento_gateway_ble()
 RETURNS trigger
 LANGUAGE plpgsql
AS $function$
DECLARE
    v_elem          JSONB;   -- dispositivo visible en curso (elemento del JSON)
    v_mac_norm      TEXT;    -- MAC del dispositivo visible, normalizada (minúsculas, sin ':')
    v_gateway_mac   TEXT;    -- identificador (MAC) del gateway emisor
BEGIN
    -- Solo aplica a métricas emitidas por gateways BLE
    IF NEW.tipo <> 'gateway_ble' THEN
        RETURN NEW;
    END IF;

    -- Se resuelve la MAC del gateway a partir del dispositivo de origen
    SELECT lower(identificador) INTO v_gateway_mac
    FROM dispositivos
    WHERE id = NEW.dispositivo_id;

    IF v_gateway_mac IS NULL THEN
        RAISE EXCEPTION
            'No se pudo resolver identificador de gateway para dispositivo_id=%',
            NEW.dispositivo_id;
    END IF;

    -- Un avistamiento por cada dispositivo de la lista "visibles"
    FOR v_elem IN
        SELECT * FROM jsonb_array_elements(NEW.datos -> 'visibles')
    LOOP
        v_mac_norm := lower(replace(v_elem ->> 'mac', ':', ''));

        -- LEFT JOIN: si la MAC coincide con un dispositivo conocido se guarda su id; si no, device_id queda NULL. 
        -- ON CONFLICT evita duplicar el mismo avistamiento (gateway + MAC + instante).
        INSERT INTO avistamientos_gateway_ble (
            metrica_origen_id,
            gateway_id,
            device_mac,
            device_id,
            rssi,
            ts_evento
        )
        SELECT
            NEW.id,
            v_gateway_mac,
            v_mac_norm,
            d.id,
            (v_elem ->> 'rssi')::SMALLINT,
            to_timestamp((v_elem ->> 'last_seen')::DOUBLE PRECISION)
        FROM (SELECT 1) AS dummy
        LEFT JOIN dispositivos d ON lower(d.identificador) = v_mac_norm
        ON CONFLICT (gateway_id, device_mac, ts_evento) DO NOTHING;
    END LOOP;

    RETURN NEW;
END;
$function$
;
