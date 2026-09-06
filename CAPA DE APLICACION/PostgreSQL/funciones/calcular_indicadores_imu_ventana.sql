-- DROP FUNCTION public.calcular_indicadores_imu_ventana(int4, int4, int4, float8, int4);

CREATE OR REPLACE FUNCTION public.calcular_indicadores_imu_ventana(p_dispositivo_id integer, p_sector_raw_id integer, p_numero_bloque integer, p_escala double precision, p_max_n_para_dft integer)
 RETURNS void
 LANGUAGE plpgsql
AS $function$
DECLARE
    v_n_muestras        INT;            -- nº de muestras válidas del bloque
    v_ts_inicio         TIMESTAMPTZ;    -- timestamp de la primera muestra
    v_ts_fin            TIMESTAMPTZ;    -- timestamp de la última muestra
    v_duracion_s        FLOAT;          -- duración del bloque en segundos
    v_actividad         SMALLINT;       -- actividad dominante (moda) del bloque
    v_vedba_media       FLOAT;          -- VeDBA media del bloque
    v_vedba_max         FLOAT;          -- VeDBA máxima del bloque
    v_fs                FLOAT;          -- frecuencia de muestreo estimada (Hz)
    v_primera_medicion_id INTEGER;      -- id de la primera medición rápida del bloque
    v_ultima_medicion_id  INTEGER;      -- id de la última medición rápida del bloque

    arr_x               FLOAT[];        -- serie temporal de aceleración dinámica eje X (escalada)
    arr_y               FLOAT[];        -- idem eje Y
    arr_z               FLOAT[];        -- idem eje Z

    v_freqs             FLOAT[] := '{}';   -- frecuencias de cada bin de la DFT (Hz)
    v_mag_x             FLOAT[] := '{}';   -- magnitud del espectro por bin, eje X
    v_mag_y             FLOAT[] := '{}';   -- idem eje Y
    v_mag_z             FLOAT[] := '{}';   -- idem eje Z

    k                   INT;            -- índice del bin de frecuencia en curso
    n_bins              INT;            -- nº de bins a calcular (hasta Nyquist)
    re_x FLOAT; im_x FLOAT; mag_x FLOAT;   -- parte real/imag/magnitud del bin, eje X
    re_y FLOAT; im_y FLOAT; mag_y FLOAT;   -- idem eje Y
    re_z FLOAT; im_z FLOAT; mag_z FLOAT;   -- idem eje Z
    v_freq_k            FLOAT;          -- frecuencia asociada al bin k (Hz)
    v_factor_amplitud    FLOAT;         -- factor de normalización de amplitud del bin

    v_freq_dom_x FLOAT := 0; v_energia_dom_x FLOAT := 0;   -- frecuencia y magnitud del pico dominante, eje X
    v_freq_dom_y FLOAT := 0; v_energia_dom_y FLOAT := 0;   -- idem eje Y
    v_freq_dom_z FLOAT := 0; v_energia_dom_z FLOAT := 0;   -- idem eje Z

	-- Acumuladores para energía total, centroide y ancho de banda espectral (sum de mag, sum de mag*f, sum de mag*f^2 y sum de mag^2), por eje
	v_sum_mag_x FLOAT := 0; v_sum_magf_x FLOAT := 0; v_sum_magf2_x FLOAT := 0; v_sum_magsq_x FLOAT := 0;
    v_sum_mag_y FLOAT := 0; v_sum_magf_y FLOAT := 0; v_sum_magf2_y FLOAT := 0; v_sum_magsq_y FLOAT := 0;
    v_sum_mag_z FLOAT := 0; v_sum_magf_z FLOAT := 0; v_sum_magf2_z FLOAT := 0; v_sum_magsq_z FLOAT := 0;

    v_energia_total_x FLOAT; v_centroide_x FLOAT; v_ancho_banda_x FLOAT;   -- indicadores espectrales finales, eje X
    v_energia_total_y FLOAT; v_centroide_y FLOAT; v_ancho_banda_y FLOAT;   -- idem eje Y
    v_energia_total_z FLOAT; v_centroide_z FLOAT; v_ancho_banda_z FLOAT;   -- idem eje Z

    v_calcular_dft      BOOLEAN := TRUE;   -- se pone a FALSE si el bloque es demasiado grande
BEGIN
    -- Estadísticos del bloque y volcado de las tres series temporales a arrays
    SELECT
        COUNT(*)::INT,
        MIN(timestamp), MAX(timestamp),
        MODE() WITHIN GROUP (ORDER BY actividad),
        AVG(sqrt(din_x::FLOAT^2 + din_y::FLOAT^2 + din_z::FLOAT^2) * p_escala),
        MAX(sqrt(din_x::FLOAT^2 + din_y::FLOAT^2 + din_z::FLOAT^2) * p_escala),
        array_agg(din_x::FLOAT * p_escala ORDER BY timestamp),
        array_agg(din_y::FLOAT * p_escala ORDER BY timestamp),
        array_agg(din_z::FLOAT * p_escala ORDER BY timestamp),
        MIN(id), MAX(id)
    INTO
        v_n_muestras, v_ts_inicio, v_ts_fin, v_actividad,
        v_vedba_media, v_vedba_max,
        arr_x, arr_y, arr_z,
        v_primera_medicion_id, v_ultima_medicion_id
    FROM mediciones_rapidas
    WHERE dispositivo_id = p_dispositivo_id
      AND sector_raw_id  = p_sector_raw_id
      AND numero_bloque  = p_numero_bloque
      AND din_x IS NOT NULL          -- se descartan filas sin aceleración dinámica
      AND xl_x IS NOT NULL;          -- ni sin aceleración cruda

    -- Sin datos suficientes no hay análisis posible: se sale sin escribir nada
    IF v_n_muestras IS NULL OR v_n_muestras < 4 THEN
        RETURN;
    END IF;

    -- Duración real del bloque (mínimo 1 ms para evitar dividir por cero)
    v_duracion_s := GREATEST(extract(epoch FROM (v_ts_fin - v_ts_inicio)), 0.001);
    -- Frecuencia de muestreo estimada a partir de nº de muestras y duración
    v_fs := (v_n_muestras - 1) / v_duracion_s;

    -- Si el bloque supera el máximo permitido, se omite la DFT (coste O(n^2) aquí)
    IF v_n_muestras > p_max_n_para_dft THEN
        v_calcular_dft := FALSE;
        RAISE NOTICE 'calcular_indicadores_imu_ventana: bloque dispositivo=% sector=% numero_bloque=% tiene % muestras (> %), se omite la dft',
            p_dispositivo_id, p_sector_raw_id, p_numero_bloque, v_n_muestras, p_max_n_para_dft;
    END IF;

    IF v_calcular_dft THEN
        -- Solo se calcula hasta la frecuencia de Nyquist (mitad de las muestras)
        n_bins := v_n_muestras / 2;

        -- Para cada bin de frecuencia k se calcula la DFT de los tres ejes
        FOR k IN 1..n_bins LOOP
            -- Frecuencia física asociada al bin k
            v_freq_k := k * v_fs / v_n_muestras::FLOAT;

            -- Parte real e imaginaria del bin k para el eje X.
            -- Cada muestra se pondera con la ventana de Hann (0.5*(1-cos(...))) antes de proyectarla sobre coseno/seno.
            SELECT
                SUM(val * (0.5 * (1 - cos(2*pi()*(idx-1)/(v_n_muestras-1)::FLOAT)))
                        * cos(2 * pi() * k * (idx - 1) / v_n_muestras::FLOAT)),
                SUM(val * (0.5 * (1 - cos(2*pi()*(idx-1)/(v_n_muestras-1)::FLOAT)))
                        * sin(2 * pi() * k * (idx - 1) / v_n_muestras::FLOAT))
            INTO re_x, im_x
            FROM unnest(arr_x) WITH ORDINALITY AS t(val, idx);

            SELECT
                SUM(val * (0.5 * (1 - cos(2*pi()*(idx-1)/(v_n_muestras-1)::FLOAT))) * cos(2 * pi() * k * (idx - 1) / v_n_muestras::FLOAT)),
                SUM(val * (0.5 * (1 - cos(2*pi()*(idx-1)/(v_n_muestras-1)::FLOAT))) * sin(2 * pi() * k * (idx - 1) / v_n_muestras::FLOAT))
            INTO re_y, im_y   -- idem para el eje Y
            FROM unnest(arr_y) WITH ORDINALITY AS t(val, idx);

            SELECT
                SUM(val * (0.5 * (1 - cos(2*pi()*(idx-1)/(v_n_muestras-1)::FLOAT))) * cos(2 * pi() * k * (idx - 1) / v_n_muestras::FLOAT)),
                SUM(val * (0.5 * (1 - cos(2*pi()*(idx-1)/(v_n_muestras-1)::FLOAT))) * sin(2 * pi() * k * (idx - 1) / v_n_muestras::FLOAT))
            INTO re_z, im_z   -- idem para el eje Z
            FROM unnest(arr_z) WITH ORDINALITY AS t(val, idx);

            -- Normalización de amplitud: el último bin de una señal de longitud par no se duplica (no tiene pareja simétrica); el resto sí (x2)
            IF k = n_bins AND v_n_muestras % 2 = 0 THEN
                v_factor_amplitud := 1.0 / v_n_muestras::FLOAT;
            ELSE
                v_factor_amplitud := 2.0 / v_n_muestras::FLOAT;
            END IF;

            -- Magnitud (amplitud) del bin k en cada eje
            mag_x := sqrt(re_x^2 + im_x^2) * v_factor_amplitud;
            mag_y := sqrt(re_y^2 + im_y^2) * v_factor_amplitud;
            mag_z := sqrt(re_z^2 + im_z^2) * v_factor_amplitud;

            -- Se guarda el bin en los arrays del espectro (para el JSON final)
            v_freqs := array_append(v_freqs, v_freq_k);
            v_mag_x := array_append(v_mag_x, mag_x);
            v_mag_y := array_append(v_mag_y, mag_y);
            v_mag_z := array_append(v_mag_z, mag_z);

            -- Se va quedando con el bin de mayor magnitud (frecuencia dominante)
            IF mag_x > v_energia_dom_x THEN v_energia_dom_x := mag_x; v_freq_dom_x := v_freq_k; END IF;
            IF mag_y > v_energia_dom_y THEN v_energia_dom_y := mag_y; v_freq_dom_y := v_freq_k; END IF;
            IF mag_z > v_energia_dom_z THEN v_energia_dom_z := mag_z; v_freq_dom_z := v_freq_k; END IF;

			-- Acumuladores para energía total / centroide / ancho de banda,
            -- calculados en el mismo pase que la DFT (sin recorrer el espectro dos veces).
            v_sum_mag_x   := v_sum_mag_x   + mag_x;
            v_sum_magf_x  := v_sum_magf_x  + mag_x * v_freq_k;
            v_sum_magf2_x := v_sum_magf2_x + mag_x * v_freq_k^2;
            v_sum_magsq_x := v_sum_magsq_x + mag_x^2;

            v_sum_mag_y   := v_sum_mag_y   + mag_y;
            v_sum_magf_y  := v_sum_magf_y  + mag_y * v_freq_k;
            v_sum_magf2_y := v_sum_magf2_y + mag_y * v_freq_k^2;
            v_sum_magsq_y := v_sum_magsq_y + mag_y^2;

            v_sum_mag_z   := v_sum_mag_z   + mag_z;
            v_sum_magf_z  := v_sum_magf_z  + mag_z * v_freq_k;
            v_sum_magf2_z := v_sum_magf2_z + mag_z * v_freq_k^2;
            v_sum_magsq_z := v_sum_magsq_z + mag_z^2;
        END LOOP;

		-- Energía total: suma de mag^2 sobre todos los bins del espectro de amplitud.
		-- Es una métrica relativa, útil para comparar entre ejes o entre bloques que tengan el mismo tamaño de ventana (n_muestras).
        v_energia_total_x := v_sum_magsq_x;
        v_energia_total_y := v_sum_magsq_y;
        v_energia_total_z := v_sum_magsq_z;

		-- Centroide y ancho de banda: NULL si el espectro es plano (energía nula), para no forzar una división por cero ni un 0 engañoso.
        IF v_sum_mag_x > 0 THEN
            v_centroide_x   := v_sum_magf_x / v_sum_mag_x;
            v_ancho_banda_x := sqrt(GREATEST(v_sum_magf2_x / v_sum_mag_x - v_centroide_x^2, 0));
        END IF;
        IF v_sum_mag_y > 0 THEN
            v_centroide_y   := v_sum_magf_y / v_sum_mag_y;
            v_ancho_banda_y := sqrt(GREATEST(v_sum_magf2_y / v_sum_mag_y - v_centroide_y^2, 0));
        END IF;
        IF v_sum_mag_z > 0 THEN
            v_centroide_z   := v_sum_magf_z / v_sum_mag_z;
            v_ancho_banda_z := sqrt(GREATEST(v_sum_magf2_z / v_sum_mag_z - v_centroide_z^2, 0));
        END IF;
    END IF;

    -- Se guarda el resultado del bloque; si ya existe, se actualiza (upsert). El espectro completo se serializa como JSONB solo si se calculó la DFT.
    INSERT INTO indicadores_imu_ventana (
        dispositivo_id, sector_raw_id, numero_bloque,
        timestamp_inicio, timestamp_fin, duracion_s, n_muestras, actividad,
        vedba_media, vedba_max,
        dft_freq_dominante_x, dft_energia_dominante_x,
        dft_freq_dominante_y, dft_energia_dominante_y,
        dft_freq_dominante_z, dft_energia_dominante_z,
        dft_energia_total_x, dft_centroide_x, dft_ancho_banda_x,
        dft_energia_total_y, dft_centroide_y, dft_ancho_banda_y,
        dft_energia_total_z, dft_centroide_z, dft_ancho_banda_z,
        dft_espectro,
        primera_medicion_id, ultima_medicion_id
    ) VALUES (
        p_dispositivo_id, p_sector_raw_id, p_numero_bloque,
        v_ts_inicio, v_ts_fin, v_duracion_s, v_n_muestras, v_actividad,
        v_vedba_media, v_vedba_max,
        NULLIF(v_freq_dom_x, 0), v_energia_dom_x,
        NULLIF(v_freq_dom_y, 0), v_energia_dom_y,
        NULLIF(v_freq_dom_z, 0), v_energia_dom_z,
        v_energia_total_x, v_centroide_x, v_ancho_banda_x,
        v_energia_total_y, v_centroide_y, v_ancho_banda_y,
        v_energia_total_z, v_centroide_z, v_ancho_banda_z,
        CASE WHEN v_calcular_dft
             THEN jsonb_build_object('freqs', v_freqs, 'mag_x', v_mag_x, 'mag_y', v_mag_y, 'mag_z', v_mag_z)
             ELSE NULL
        END,
        v_primera_medicion_id, v_ultima_medicion_id
    )
    ON CONFLICT (dispositivo_id, sector_raw_id, numero_bloque) DO UPDATE SET
        timestamp_inicio         = EXCLUDED.timestamp_inicio,
        timestamp_fin            = EXCLUDED.timestamp_fin,
        duracion_s               = EXCLUDED.duracion_s,
        n_muestras               = EXCLUDED.n_muestras,
        actividad                = EXCLUDED.actividad,
        vedba_media              = EXCLUDED.vedba_media,
        vedba_max                = EXCLUDED.vedba_max,
        dft_freq_dominante_x     = EXCLUDED.dft_freq_dominante_x,
        dft_energia_dominante_x  = EXCLUDED.dft_energia_dominante_x,
        dft_freq_dominante_y     = EXCLUDED.dft_freq_dominante_y,
        dft_energia_dominante_y  = EXCLUDED.dft_energia_dominante_y,
        dft_freq_dominante_z     = EXCLUDED.dft_freq_dominante_z,
        dft_energia_dominante_z  = EXCLUDED.dft_energia_dominante_z,
        dft_energia_total_x      = EXCLUDED.dft_energia_total_x,
        dft_centroide_x          = EXCLUDED.dft_centroide_x,
        dft_ancho_banda_x        = EXCLUDED.dft_ancho_banda_x,
        dft_energia_total_y      = EXCLUDED.dft_energia_total_y,
        dft_centroide_y          = EXCLUDED.dft_centroide_y,
        dft_ancho_banda_y        = EXCLUDED.dft_ancho_banda_y,
        dft_energia_total_z      = EXCLUDED.dft_energia_total_z,
        dft_centroide_z          = EXCLUDED.dft_centroide_z,
        dft_ancho_banda_z        = EXCLUDED.dft_ancho_banda_z,
        dft_espectro             = EXCLUDED.dft_espectro,
        primera_medicion_id      = EXCLUDED.primera_medicion_id,
        ultima_medicion_id       = EXCLUDED.ultima_medicion_id;
END;
$function$
;
