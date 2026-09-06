-- DROP FUNCTION public.calcular_indicadores_imu_1s(int4, int4, timestamptz, timestamptz, float8);

CREATE OR REPLACE FUNCTION public.calcular_indicadores_imu_1s(p_dispositivo_id integer, p_sector_raw_id integer, p_ts_inicio timestamp with time zone, p_ts_fin timestamp with time zone, p_escala double precision)
 RETURNS void
 LANGUAGE plpgsql
AS $function$
DECLARE
    v_ventana       TIMESTAMPTZ;   -- inicio de la ventana de 1 s en curso
    v_ventana_fin   TIMESTAMPTZ;   -- fin (exclusivo) de la ventana en curso
    v_n_muestras    SMALLINT;      -- nº de muestras válidas dentro de la ventana
    v_actividad     SMALLINT;      -- actividad más frecuente (moda) en la ventana
    v_vedba_media   FLOAT;         -- VeDBA media de la ventana
    v_vedba_max     FLOAT;         -- VeDBA máxima de la ventana
    v_pitch_medio   FLOAT;         -- ángulo de cabeceo (pitch) medio, en grados
    v_roll_medio    FLOAT;         -- ángulo de alabeo (roll) medio, en grados
    v_primera_id    INTEGER;       -- id de la primera medición rápida de la ventana
BEGIN
    -- Se trunca el inicio al segundo para alinear las ventanas
    v_ventana := date_trunc('second', p_ts_inicio);

    -- Se recorre el rango avanzando de segundo en segundo
    WHILE v_ventana <= p_ts_fin LOOP
        v_ventana_fin := v_ventana + INTERVAL '1 second';

        -- Calcula los indicadores agregados de la ventana actual
        SELECT
            -- nº de muestras que caen en la ventana
            COUNT(*)::SMALLINT,
            -- actividad dominante: valor modal del campo actividad
            MODE() WITHIN GROUP (ORDER BY actividad),
            -- VeDBA media: módulo del vector de aceleración dinámica, escalado
            AVG(sqrt(din_x::FLOAT^2 + din_y::FLOAT^2 + din_z::FLOAT^2) * p_escala),
            -- VeDBA máxima: pico de aceleración dinámica en la ventana
            MAX(sqrt(din_x::FLOAT^2 + din_y::FLOAT^2 + din_z::FLOAT^2) * p_escala),
            -- pitch físico = rotación sobre eje X (lateral) - depende de Y
            -- se estima la gravedad como (xl_* - din_*) y se saca el ángulo con atan2
            AVG(degrees(atan2((xl_y::FLOAT - din_y::FLOAT), sqrt((xl_x::FLOAT - din_x::FLOAT)^2 + (xl_z::FLOAT - din_z::FLOAT)^2)))),
            -- roll físico = rotación sobre eje Y (longitudinal) - depende de X
            AVG(degrees(atan2((xl_x::FLOAT - din_x::FLOAT), sqrt((xl_y::FLOAT - din_y::FLOAT)^2 + (xl_z::FLOAT - din_z::FLOAT)^2)))),
            -- id más bajo = primera medición cronológica de la ventana
            MIN(id)
        INTO
            v_n_muestras,
            v_actividad,
            v_vedba_media,
            v_vedba_max,
            v_pitch_medio,
            v_roll_medio,
            v_primera_id
        FROM mediciones_rapidas
        WHERE dispositivo_id = p_dispositivo_id
        AND timestamp >= v_ventana          -- límite inferior incluido
        AND timestamp < v_ventana_fin       -- límite superior excluido
        AND din_x IS NOT NULL               -- se descartan filas sin aceleración dinámica
        AND xl_x IS NOT NULL;               -- y sin aceleración cruda (necesaria para pitch/roll)

        -- Solo se guarda la ventana si contiene alguna muestra válida
        IF v_n_muestras > 0 THEN
            -- Inserta el registro de indicadores de esta ventana de 1 s
            INSERT INTO indicadores_imu_1s (
                dispositivo_id, sector_raw_id, medicion_rapida_id,
                timestamp, n_muestras, actividad,
                vedba_media, vedba_max,
                pitch_medio, roll_medio
            )
            VALUES (
                p_dispositivo_id, p_sector_raw_id, v_primera_id,
                v_ventana, v_n_muestras, v_actividad,
                v_vedba_media, v_vedba_max,
                v_pitch_medio, v_roll_medio
            )
            -- Si ya existe la fila (mismo dispositivo y segundo), la actualiza
            ON CONFLICT (dispositivo_id, timestamp) DO UPDATE SET
                sector_raw_id      = EXCLUDED.sector_raw_id,
                medicion_rapida_id = EXCLUDED.medicion_rapida_id,
                n_muestras         = EXCLUDED.n_muestras,
                actividad          = EXCLUDED.actividad,
                vedba_media        = EXCLUDED.vedba_media,
                vedba_max          = EXCLUDED.vedba_max,
                pitch_medio        = EXCLUDED.pitch_medio,
                roll_medio         = EXCLUDED.roll_medio;

            -- Calcula el comportamiento predicho a partir de los indicadores
            -- de la ventana y lo guarda en la misma fila
			UPDATE indicadores_imu_1s
            SET comportamiento_predicho = predecir_comportamiento(
                v_vedba_media, v_vedba_max, v_pitch_medio, v_roll_medio
            )
            WHERE dispositivo_id = p_dispositivo_id
            AND timestamp = v_ventana;

        END IF;

        -- Avanza a la siguiente ventana de 1 segundo
        v_ventana := v_ventana_fin;
    END LOOP;
END;
$function$
;
