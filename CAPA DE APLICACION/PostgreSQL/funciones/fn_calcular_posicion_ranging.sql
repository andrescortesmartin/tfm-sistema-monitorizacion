-- DROP FUNCTION public.fn_calcular_posicion_ranging(int4, jsonb, int4, int4, varchar, timestamptz);


CREATE OR REPLACE FUNCTION public.fn_calcular_posicion_ranging(p_dispositivo_id integer, p_ranging_json jsonb, p_medicion_ble_id integer, p_medicion_lorawan_id integer, p_origen character varying, p_timestamp timestamp with time zone)
 RETURNS void
 LANGUAGE plpgsql
AS $function$
DECLARE
    elem           JSONB;             -- elemento (ancla) del array de ranging en curso
    v_addr_text    TEXT;              -- dirección de la ancla tal como viene en el JSON
    v_ancla_id     INTEGER;           -- id de la ancla resuelto en la tabla dispositivos
    v_dist         REAL;              -- distancia a la ancla (mediana de sus muestras)
    v_lat          DOUBLE PRECISION;  -- latitud de la ancla
    v_lon          DOUBLE PRECISION;  -- longitud de la ancla

    arr_lat DOUBLE PRECISION[] := ARRAY[]::DOUBLE PRECISION[];   -- lat de cada ancla usable
    arr_lon DOUBLE PRECISION[] := ARRAY[]::DOUBLE PRECISION[];   -- lon de cada ancla usable
    arr_d   DOUBLE PRECISION[] := ARRAY[]::DOUBLE PRECISION[];   -- distancia a cada ancla usable
    n INTEGER := 0;                   -- nº de anclas usables
    i INTEGER;

    v_lat_ref DOUBLE PRECISION;       -- ancla de referencia: origen del plano local
    v_lon_ref DOUBLE PRECISION;
    v_ref_dist DOUBLE PRECISION;      -- distancia medida a la ancla de referencia
    v_ref_x DOUBLE PRECISION := 0;    -- coordenadas locales de la referencia (0,0 por definición)
    v_ref_y DOUBLE PRECISION := 0;

    xi DOUBLE PRECISION;              -- coordenada local X de la ancla i (metros)
    yi DOUBLE PRECISION;              -- coordenada local Y de la ancla i (metros)
    di DOUBLE PRECISION;              -- distancia medida a la ancla i
    a  DOUBLE PRECISION;              -- coeficientes de la ecuación linealizada de la ancla i
    b  DOUBLE PRECISION;
    c  DOUBLE PRECISION;

    v_sxx DOUBLE PRECISION := 0;      -- sumas normales del ajuste por mínimos cuadrados
    v_sxy DOUBLE PRECISION := 0;
    v_syy DOUBLE PRECISION := 0;
    v_sxc DOUBLE PRECISION := 0;
    v_syc DOUBLE PRECISION := 0;
    v_det DOUBLE PRECISION;           -- determinante del sistema 2x2

    v_x_est DOUBLE PRECISION;         -- posición estimada en el plano local (metros)
    v_y_est DOUBLE PRECISION;
    v_lat_est DOUBLE PRECISION;       -- posición estimada reconvertida a lat/lon
    v_lon_est DOUBLE PRECISION;

    v_residuales DOUBLE PRECISION[] := ARRAY[]::DOUBLE PRECISION[];   -- |dist medida - dist estimada| por ancla
    v_residual_medio DOUBLE PRECISION;
    v_residual_max DOUBLE PRECISION;

    -- Acumuladores para HDOP (matriz G^T*G, vectores unitarios posicion_estimada->ancla)
    v_dx DOUBLE PRECISION;
    v_dy DOUBLE PRECISION;
    v_dist_exp DOUBLE PRECISION;
    v_gxx DOUBLE PRECISION := 0;
    v_gxy DOUBLE PRECISION := 0;
    v_gyy DOUBLE PRECISION := 0;
    v_det_hdop DOUBLE PRECISION;
    v_hdop DOUBLE PRECISION;         -- dilución de precisión horizontal (calidad de la geometría)
BEGIN
    IF p_ranging_json IS NULL THEN
        RETURN;
    END IF;

    -- Primero se recorre el JSON, resolver cada ancla y su distancia mediana
    FOR elem IN SELECT * FROM jsonb_array_elements(p_ranging_json) LOOP
        v_addr_text := elem->>'addr';

        -- La dirección puede venir como entero decimal o ya como identificador hex
        IF v_addr_text ~ '^[0-9]+$' THEN
            SELECT id INTO v_ancla_id
            FROM dispositivos
            WHERE tipo = 'ancla_lora'
              AND identificador = '0x' || lpad(to_hex(v_addr_text::INTEGER), 8, '0');
        ELSE
            SELECT id INTO v_ancla_id
            FROM dispositivos
            WHERE tipo = 'ancla_lora'
              AND identificador = v_addr_text;
        END IF;

        IF v_ancla_id IS NULL THEN
            RAISE WARNING 'Ranging: no se pudo resolver addr=% (dispositivo_id=%, timestamp=%)',
                v_addr_text, p_dispositivo_id, p_timestamp;
            CONTINUE;
        END IF;

        -- Distancia a la ancla: mediana de las muestras si las hay, o valor único
        IF elem ? 'muestras' THEN
            SELECT percentile_cont(0.5) WITHIN GROUP (ORDER BY (m->>'dist')::REAL)
              INTO v_dist
              FROM jsonb_array_elements(elem->'muestras') m;
        ELSE
            v_dist := (elem->>'dist')::REAL;
        END IF;

        IF v_dist IS NULL THEN
		    CONTINUE;
		END IF;
		IF v_dist < 0 THEN
		    v_dist := 0;   -- distancia negativa (ruido de medida) se satura a 0
		END IF;

        -- Se necesita la posición de la ancla; sin ella no sirve para multilaterar
        SELECT lat, lon INTO v_lat, v_lon FROM dispositivos WHERE id = v_ancla_id;
        IF v_lat IS NULL OR v_lon IS NULL THEN
            CONTINUE;
        END IF;

        n := n + 1;
        arr_lat := array_append(arr_lat, v_lat);
        arr_lon := array_append(arr_lon, v_lon);
        arr_d   := array_append(arr_d, v_dist::DOUBLE PRECISION);
    END LOOP;

    -- Con menos de 3 anclas no hay solución 2D: se registra la fila sin posición
    IF n < 3 THEN
        INSERT INTO indicadores_posicion_ranging
            (dispositivo_id, medicion_ble_id, medicion_lorawan_id, origen, timestamp,
             lat_estimada, lon_estimada, n_anclas_usadas, residual_medio, residual_max, hdop)
        VALUES
            (p_dispositivo_id, p_medicion_ble_id, p_medicion_lorawan_id, p_origen, p_timestamp,
             NULL, NULL, n, NULL, NULL, NULL);
        RETURN;
    END IF;

    -- Se elige la ancla 1 (orden de llegada en el JSON) como referencia del plano local
    v_lat_ref := arr_lat[1];
    v_lon_ref := arr_lon[1];
    v_ref_dist := arr_d[1];

    -- Acumular sumas de minimos cuadrados restando la ecuacion de referencia
    -- Restar la ecuación de la ancla de referencia a las demás elimina los términos cuadráticos y deja un sistema lineal en (x,y).
    FOR i IN 2..n LOOP
        -- Proyección equirectangular: grados a metros en el plano local
        xi := (arr_lon[i] - v_lon_ref) * cos(radians(v_lat_ref)) * 111320.0;
        yi := (arr_lat[i] - v_lat_ref) * 111320.0;
        di := arr_d[i];

        -- Coeficientes de la ecuación linealizada de la ancla i
        a := 2 * (xi - v_ref_x);
        b := 2 * (yi - v_ref_y);
        c := v_ref_dist^2 - di^2 - v_ref_x^2 + xi^2 - v_ref_y^2 + yi^2;

        -- Acumulación de las ecuaciones normales (A^T A y A^T c)
        v_sxx := v_sxx + a*a;
        v_sxy := v_sxy + a*b;
        v_syy := v_syy + b*b;
        v_sxc := v_sxc + a*c;
        v_syc := v_syc + b*c;
    END LOOP;

    v_det := v_sxx * v_syy - v_sxy * v_sxy;

    -- Determinante nulo = anclas colineales: sistema sin solución única
    IF v_det = 0 THEN
        INSERT INTO indicadores_posicion_ranging
            (dispositivo_id, medicion_ble_id, medicion_lorawan_id, origen, timestamp,
             lat_estimada, lon_estimada, n_anclas_usadas, residual_medio, residual_max, hdop)
        VALUES
            (p_dispositivo_id, p_medicion_ble_id, p_medicion_lorawan_id, p_origen, p_timestamp,
             NULL, NULL, n, NULL, NULL, NULL);
        RETURN;
    END IF;

    -- Solución del sistema 2x2 por regla de Cramer: posición estimada (metros)
    v_x_est := (v_sxc * v_syy - v_sxy * v_syc) / v_det;
    v_y_est := (v_sxx * v_syc - v_sxc * v_sxy) / v_det;

    -- Indicadores de calidad. Residuales por ancla Y matriz de geometria para HDOP (mismo bucle, todas las anclas 1..n)
    FOR i IN 1..n LOOP
        -- coordenadas locales de la ancla i (para i=1 da 0,0 por ser la referencia)
        xi := (arr_lon[i] - v_lon_ref) * cos(radians(v_lat_ref)) * 111320.0;
        yi := (arr_lat[i] - v_lat_ref) * 111320.0;
        di := arr_d[i];

        v_dx := v_x_est - xi;
        v_dy := v_y_est - yi;
        v_dist_exp := sqrt(v_dx^2 + v_dy^2);

        -- residual: distancia medida vs distancia que implicaria la posicion estimada
        v_residuales := array_append(v_residuales, abs(di - v_dist_exp));

        -- geometria: vector unitario posicion_estimada -> ancla i, acumulado en G^T*G
        IF v_dist_exp > 0 THEN
            v_gxx := v_gxx + (v_dx / v_dist_exp)^2;
            v_gxy := v_gxy + (v_dx / v_dist_exp) * (v_dy / v_dist_exp);
            v_gyy := v_gyy + (v_dy / v_dist_exp)^2;
        END IF;
    END LOOP;

    -- Resumen de residuales: medio (ajuste global) y máximo (peor ancla)
    SELECT avg(r), max(r) INTO v_residual_medio, v_residual_max FROM unnest(v_residuales) AS r;

    -- HDOP = sqrt(traza((G^T*G)^-1)), via Cramer sobre la matriz 2x2 acumulada
    v_det_hdop := v_gxx * v_gyy - v_gxy * v_gxy;
    IF v_det_hdop > 0 THEN
        v_hdop := sqrt(v_gyy / v_det_hdop + v_gxx / v_det_hdop);
    ELSE
        v_hdop := NULL;  -- geometria degenerada (anclas y posicion perfectamente colineales)
    END IF;

    -- Paso 5: reconvertir (x,y) a lat/lon
    v_lat_est := v_lat_ref + (v_y_est / 111320.0);
    v_lon_est := v_lon_ref + (v_x_est / (111320.0 * cos(radians(v_lat_ref))));

    -- Se guarda la posición estimada junto con sus métricas de calidad
    INSERT INTO indicadores_posicion_ranging
        (dispositivo_id, medicion_ble_id, medicion_lorawan_id, origen, timestamp,
         lat_estimada, lon_estimada, n_anclas_usadas, residual_medio, residual_max, hdop)
    VALUES
        (p_dispositivo_id, p_medicion_ble_id, p_medicion_lorawan_id, p_origen, p_timestamp,
         v_lat_est, v_lon_est, n, v_residual_medio, v_residual_max, v_hdop);

END;
$function$
;
