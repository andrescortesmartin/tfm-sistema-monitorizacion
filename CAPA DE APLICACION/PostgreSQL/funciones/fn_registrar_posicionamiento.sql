-- DROP FUNCTION public.fn_registrar_posicionamiento(int4, float8, float8, timestamptz, text);

CREATE OR REPLACE FUNCTION public.fn_registrar_posicionamiento(p_dispositivo_id integer, p_lat double precision, p_lon double precision, p_ts timestamp with time zone, p_origen text)
 RETURNS void
 LANGUAGE plpgsql
AS $function$
DECLARE
    v_zona_id INTEGER;      -- zona del grid en la que cae (p_lat, p_lon)
    v_hora    TIMESTAMPTZ;  -- inicio de la hora local a la que se imputa la fijación
BEGIN
    -- Zona del grid correspondiente al punto
    v_zona_id := fn_resolver_zona(p_lat, p_lon);
    IF v_zona_id IS NULL THEN
        RETURN;  -- sin config de grid activa; ya se avisa dentro de fn_resolver_zona
    END IF;

    -- Se trunca a la hora en zona horaria local (Europe/Madrid), no en UTC
    v_hora := date_trunc('hour', p_ts AT TIME ZONE 'Europe/Madrid') AT TIME ZONE 'Europe/Madrid';

    -- Upsert del contador: primera fijación de la hora la crea, el resto suma 1
    INSERT INTO posicionamientos_por_zona_hora
        (dispositivo_id, zona_id, hora, origen_posicion, n_fijaciones)
    VALUES
        (p_dispositivo_id, v_zona_id, v_hora, p_origen, 1)
    ON CONFLICT (dispositivo_id, zona_id, hora, origen_posicion)
    DO UPDATE SET n_fijaciones = posicionamientos_por_zona_hora.n_fijaciones + 1;
END;
$function$
;
