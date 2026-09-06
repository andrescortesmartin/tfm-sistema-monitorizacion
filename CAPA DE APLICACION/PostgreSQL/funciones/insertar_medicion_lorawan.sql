-- DROP FUNCTION public.insertar_medicion_lorawan(text, jsonb);

CREATE OR REPLACE FUNCTION public.insertar_medicion_lorawan(p_mac text, p_telemetry jsonb)
 RETURNS void
 LANGUAGE plpgsql
AS $function$
DECLARE
    v_dispositivo_id INTEGER;           -- id del dispositivo resuelto por la MAC
    v_ts             TIMESTAMPTZ;       -- timestamp del módulo que se procesa en cada bloque
    v_ts_max         BIGINT := 0;       -- mayor timestamp de todos los módulos (para métricas)
	v_medicion_id    INTEGER;           -- id de la fila insertada (para los cálculos derivados)
BEGIN
    SELECT id INTO v_dispositivo_id FROM dispositivos WHERE identificador = p_mac;
    IF v_dispositivo_id IS NULL THEN
        RETURN;
    END IF;

    -- Módulo ambiente: batería + presión + temperatura, y calculo de gradiente derivado
    IF (p_telemetry->>'ts_ambiente') IS NOT NULL THEN
	    v_ts := to_timestamp((p_telemetry->>'ts_ambiente')::BIGINT);
	    INSERT INTO mediciones_lentas_lorawan (dispositivo_id, timestamp, bateria_mv, presion, temperatura)
	    VALUES (v_dispositivo_id, v_ts, (p_telemetry->>'bateria_mv')::SMALLINT, (p_telemetry->>'presion')::SMALLINT, (p_telemetry->>'temperatura')::SMALLINT)
	    ON CONFLICT (dispositivo_id, timestamp) DO UPDATE SET
	        presion = EXCLUDED.presion, temperatura = EXCLUDED.temperatura
	    RETURNING id INTO v_medicion_id;
	
	    PERFORM fn_registrar_gradiente_presion_temp(
	        p_dispositivo_id      => v_dispositivo_id,
	        p_origen              => 'lorawan',
	        p_timestamp           => v_ts,
	        p_presion             => (p_telemetry->>'presion')::SMALLINT,
	        p_temperatura         => (p_telemetry->>'temperatura')::SMALLINT,
	        p_medicion_lorawan_id => v_medicion_id
	    );
	END IF;

    -- Módulo ángulo: pitch y roll
    IF (p_telemetry->>'ts_angulo') IS NOT NULL THEN
        v_ts := to_timestamp((p_telemetry->>'ts_angulo')::BIGINT);
        INSERT INTO mediciones_lentas_lorawan (dispositivo_id, timestamp, bateria_mv, pitch, roll)
        VALUES (v_dispositivo_id, v_ts, (p_telemetry->>'bateria_mv')::SMALLINT, (p_telemetry->>'pitch')::FLOAT, (p_telemetry->>'roll')::FLOAT)
        ON CONFLICT (dispositivo_id, timestamp) DO UPDATE SET
            pitch = EXCLUDED.pitch, roll = EXCLUDED.roll;
    END IF;

    -- Módulo GPS: latitud y longitud
    IF (p_telemetry->>'ts_gps') IS NOT NULL THEN
        v_ts := to_timestamp((p_telemetry->>'ts_gps')::BIGINT);
        INSERT INTO mediciones_lentas_lorawan (dispositivo_id, timestamp, bateria_mv, gps_lat, gps_lon)
        VALUES (v_dispositivo_id, v_ts, (p_telemetry->>'bateria_mv')::SMALLINT, (p_telemetry->>'latitud')::FLOAT, (p_telemetry->>'longitud')::FLOAT)
        ON CONFLICT (dispositivo_id, timestamp) DO UPDATE SET
            gps_lat = EXCLUDED.gps_lat, gps_lon = EXCLUDED.gps_lon;
    END IF;

    -- Módulo ranging: JSON de distancias a anclas, y posición derivada
    IF (p_telemetry->>'ts_ranging') IS NOT NULL THEN
	    v_ts := to_timestamp((p_telemetry->>'ts_ranging')::BIGINT);
	    INSERT INTO mediciones_lentas_lorawan (dispositivo_id, timestamp, bateria_mv, ranging)
	    VALUES (v_dispositivo_id, v_ts, (p_telemetry->>'bateria_mv')::SMALLINT, (p_telemetry->'ranging'))
	    ON CONFLICT (dispositivo_id, timestamp) DO UPDATE SET
	        ranging = EXCLUDED.ranging
	    RETURNING id INTO v_medicion_id;
	
	    PERFORM fn_calcular_posicion_ranging(
	        p_dispositivo_id      => v_dispositivo_id,
	        p_ranging_json        => (p_telemetry->'ranging'),
	        p_medicion_ble_id     => NULL,
	        p_medicion_lorawan_id => v_medicion_id,
	        p_origen              => 'lorawan',
	        p_timestamp           => v_ts
	    );
	END IF;

    -- Módulo beacons: avistamientos BLE (loggers y beacons)
    IF (p_telemetry->>'ts_beacons') IS NOT NULL THEN
        v_ts := to_timestamp((p_telemetry->>'ts_beacons')::BIGINT);
        INSERT INTO mediciones_lentas_lorawan (dispositivo_id, timestamp, bateria_mv, loggers, beacons)
        VALUES (v_dispositivo_id, v_ts, (p_telemetry->>'bateria_mv')::SMALLINT, (p_telemetry->'ble_loggers'), (p_telemetry->'ble_beacons'))
        ON CONFLICT (dispositivo_id, timestamp) DO UPDATE SET
            loggers = EXCLUDED.loggers, beacons = EXCLUDED.beacons;
    END IF;
	

	-- BLOQUE MÉTRICAS DE SISTEMA (diagnóstico del logger)
    -- Se inserta siempre que llegue un uplink, independientemente de
    -- qué módulos estén activos. Contiene datos de estado interno del
    -- firmware que no son mediciones científicas sino telemetría técnica.
    --
    -- El timestamp se calcula como el máximo de los timestamps de los
    -- módulos activos en este uplink, igual que hace el script de TB.
    -- Si ningún módulo tiene timestamp (uplink vacío), no se inserta.

	-- Calcular ts_max como el mayor de los timestamps presentes
    IF (p_telemetry->>'ts_ambiente') IS NOT NULL AND (p_telemetry->>'ts_ambiente')::BIGINT > v_ts_max THEN
        v_ts_max := (p_telemetry->>'ts_ambiente')::BIGINT;
    END IF;
    IF (p_telemetry->>'ts_angulo') IS NOT NULL AND (p_telemetry->>'ts_angulo')::BIGINT > v_ts_max THEN
        v_ts_max := (p_telemetry->>'ts_angulo')::BIGINT;
    END IF;
    IF (p_telemetry->>'ts_gps') IS NOT NULL AND (p_telemetry->>'ts_gps')::BIGINT > v_ts_max THEN
        v_ts_max := (p_telemetry->>'ts_gps')::BIGINT;
    END IF;
    IF (p_telemetry->>'ts_ranging') IS NOT NULL AND (p_telemetry->>'ts_ranging')::BIGINT > v_ts_max THEN
        v_ts_max := (p_telemetry->>'ts_ranging')::BIGINT;
    END IF;
    IF (p_telemetry->>'ts_beacons') IS NOT NULL AND (p_telemetry->>'ts_beacons')::BIGINT > v_ts_max THEN
        v_ts_max := (p_telemetry->>'ts_beacons')::BIGINT;
    END IF;

    -- Solo insertar si hay al menos un timestamp válido (uplink no vacío)
    IF v_ts_max > 0 THEN
        INSERT INTO metricas_sistema (dispositivo_id, timestamp, tipo, datos)
        VALUES (
            v_dispositivo_id,
            to_timestamp(v_ts_max),
            'logger',
            jsonb_build_object(
                'reset_count',       (p_telemetry->>'reset_count')::INTEGER,
                'ttff_gps',          (p_telemetry->>'ttff_gps')::INTEGER,
                'flash_dir_lora',    (p_telemetry->>'flash_dir_lora')::BIGINT,
                'flash_dir_imu',     (p_telemetry->>'flash_dir_imu')::BIGINT,
                'actividad_eventos', (p_telemetry->>'actividad_eventos')::INTEGER,
                'flash_descarte',    (p_telemetry->>'flash_descarte')::INTEGER
            )
        )
        ON CONFLICT (dispositivo_id, timestamp, tipo) DO NOTHING;
        -- DO NOTHING porque si el mismo uplink llega dos veces, no queremos duplicar las métricas
    END IF;

END;
$function$
;
