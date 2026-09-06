-- DROP FUNCTION public.fn_resolver_zona(float8, float8);

CREATE OR REPLACE FUNCTION public.fn_resolver_zona(p_lat double precision, p_lon double precision)
 RETURNS integer
 LANGUAGE plpgsql
AS $function$
DECLARE
    v_config_id     INTEGER;            -- config de despliegue activa
    v_lat_origen    DOUBLE PRECISION;   -- esquina de referencia del grid
    v_lon_origen    DOUBLE PRECISION;
    v_tamano_celda  DOUBLE PRECISION;   -- lado de la celda en metros
    v_x_m           DOUBLE PRECISION;   -- desplazamiento E-O del punto respecto al origen (m)
    v_y_m           DOUBLE PRECISION;   -- desplazamiento N-S del punto respecto al origen (m)
    v_col           INTEGER;            -- columna de la celda (índice X)
    v_fila          INTEGER;            -- fila de la celda (índice Y)
    v_zona_id       INTEGER;            -- id de la zona resultante
BEGIN
    -- Parámetros del grid de la última config activa
    SELECT id, lat_origen, lon_origen, tamano_celda_m
    INTO v_config_id, v_lat_origen, v_lon_origen, v_tamano_celda
    FROM config_despliegue
    WHERE activo = true
    ORDER BY fecha_creacion DESC
    LIMIT 1;

    IF v_config_id IS NULL THEN
        RAISE WARNING 'fn_resolver_zona: no hay config_despliegue activa';
        RETURN NULL;
    END IF;

    -- Proyección equirectangular: grados a metros respecto al origen del grid
    v_x_m := (p_lon - v_lon_origen) * cos(radians(v_lat_origen)) * 111320.0;
    v_y_m := (p_lat - v_lat_origen) * 111320.0;

    -- Índice de celda = nº de celdas enteras que caben en ese desplazamiento
    v_col  := floor(v_x_m / v_tamano_celda)::INTEGER;
    v_fila := floor(v_y_m / v_tamano_celda)::INTEGER;

    -- Se crea la celda si aún no existe, calculando sus límites lat/lon
    INSERT INTO zonas (config_despliegue_id, zona_col, zona_fila, lat_min, lat_max, lon_min, lon_max)
    VALUES (
        v_config_id, v_col, v_fila,
        v_lat_origen + (v_fila * v_tamano_celda) / 111320.0,
        v_lat_origen + ((v_fila + 1) * v_tamano_celda) / 111320.0,
        v_lon_origen + (v_col * v_tamano_celda) / (111320.0 * cos(radians(v_lat_origen))),
        v_lon_origen + ((v_col + 1) * v_tamano_celda) / (111320.0 * cos(radians(v_lat_origen)))
    )
    ON CONFLICT (config_despliegue_id, zona_col, zona_fila) DO NOTHING;

    -- Se recupera el id (recién creado o ya existente)
    SELECT id INTO v_zona_id
    FROM zonas
    WHERE config_despliegue_id = v_config_id AND zona_col = v_col AND zona_fila = v_fila;

    RETURN v_zona_id;
END;
$function$
;
