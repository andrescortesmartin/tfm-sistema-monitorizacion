-- DROP FUNCTION public.fn_registrar_gradiente_presion_temp(int4, varchar, timestamptz, int2, int2, int4, int4);

CREATE OR REPLACE FUNCTION public.fn_registrar_gradiente_presion_temp(p_dispositivo_id integer, p_origen character varying, p_timestamp timestamp with time zone, p_presion smallint, p_temperatura smallint, p_medicion_ble_id integer DEFAULT NULL::integer, p_medicion_lorawan_id integer DEFAULT NULL::integer)
 RETURNS void
 LANGUAGE plpgsql
AS $function$
DECLARE
    v_anterior RECORD;      -- muestra anterior del mismo origen con presión no nula
    v_delta_t  INTERVAL;    -- tiempo transcurrido desde esa muestra anterior
BEGIN
    -- Si no hay presión en esta muestra, no hay nada que calcular ni registrar (no todos los sub-paquetes BLE ni todos los uplinks LoRaWAN traen ambiente).
    IF p_presion IS NULL THEN
        RETURN;
    END IF;

    -- Última muestra anterior del MISMO origen con presión no nula. BLE (historial completo ordenado) y LoRaWAN (snapshot) son series
    -- independientes por diseño: el gradiente de una nunca se calcula contra la otra, de ahí que la búsqueda siga separada por tabla
    -- aunque la función ya sea una sola.
    IF p_origen = 'ble' THEN
        SELECT presion, temperatura, timestamp
        INTO v_anterior
        FROM mediciones_lentas_ble
        WHERE dispositivo_id = p_dispositivo_id
          AND timestamp < p_timestamp
          AND presion IS NOT NULL
        ORDER BY timestamp DESC
        LIMIT 1;
    ELSIF p_origen = 'lorawan' THEN
        SELECT presion, temperatura, timestamp
        INTO v_anterior
        FROM mediciones_lentas_lorawan
        WHERE dispositivo_id = p_dispositivo_id
          AND timestamp < p_timestamp
          AND presion IS NOT NULL
        ORDER BY timestamp DESC
        LIMIT 1;
    ELSE
        RAISE EXCEPTION 'fn_registrar_gradiente_presion_temp: origen no reconocido: %', p_origen;
    END IF;

    -- Sin muestra anterior no hay gradiente: se guarda solo el valor puntual
    IF v_anterior IS NULL THEN
        INSERT INTO indicadores_presion_temperatura (
            dispositivo_id, medicion_ble_id, medicion_lorawan_id, origen, timestamp,
            delta_t_real, presion_puntual, gradiente_presion_instantaneo,
            temperatura_puntual, gradiente_temperatura_instantaneo
        ) VALUES (
            p_dispositivo_id, p_medicion_ble_id, p_medicion_lorawan_id, p_origen, p_timestamp,
            NULL, p_presion, NULL,
            p_temperatura, NULL
        )
        ON CONFLICT (dispositivo_id, timestamp, origen) DO UPDATE SET
            medicion_ble_id      = EXCLUDED.medicion_ble_id,
            medicion_lorawan_id  = EXCLUDED.medicion_lorawan_id,
            presion_puntual      = EXCLUDED.presion_puntual,
            temperatura_puntual  = EXCLUDED.temperatura_puntual,
            delta_t_real         = NULL,
            gradiente_presion_instantaneo     = NULL,
            gradiente_temperatura_instantaneo = NULL;
        RETURN;
    END IF;

    v_delta_t := p_timestamp - v_anterior.timestamp;

    -- Con muestra anterior: valor puntual + gradiente = (valor - valor_anterior) dividido por los segundos transcurridos (NULLIF evita dividir por cero).
    INSERT INTO indicadores_presion_temperatura (
        dispositivo_id, medicion_ble_id, medicion_lorawan_id, origen, timestamp,
        delta_t_real, presion_puntual, gradiente_presion_instantaneo,
        temperatura_puntual, gradiente_temperatura_instantaneo
    ) VALUES (
        p_dispositivo_id, p_medicion_ble_id, p_medicion_lorawan_id, p_origen, p_timestamp,
        v_delta_t,
        p_presion,
        (p_presion - v_anterior.presion) / NULLIF(EXTRACT(EPOCH FROM v_delta_t), 0),
        p_temperatura,
        (p_temperatura - v_anterior.temperatura) / NULLIF(EXTRACT(EPOCH FROM v_delta_t), 0)
    )
    ON CONFLICT (dispositivo_id, timestamp, origen) DO UPDATE SET
        medicion_ble_id                    = EXCLUDED.medicion_ble_id,
        medicion_lorawan_id                = EXCLUDED.medicion_lorawan_id,
        delta_t_real                       = EXCLUDED.delta_t_real,
        presion_puntual                    = EXCLUDED.presion_puntual,
        gradiente_presion_instantaneo      = EXCLUDED.gradiente_presion_instantaneo,
        temperatura_puntual                = EXCLUDED.temperatura_puntual,
        gradiente_temperatura_instantaneo  = EXCLUDED.gradiente_temperatura_instantaneo;
END;
$function$
;
