-- DROP FUNCTION public.resolver_dispositivo_ble(int4, text);

CREATE OR REPLACE FUNCTION public.resolver_dispositivo_ble(p_visto_id integer, p_tipo text)
 RETURNS integer
 LANGUAGE plpgsql
AS $function$
DECLARE
    v_identificador TEXT;        -- identificador hex reconstruido del dispositivo
    v_dispositivo_id INTEGER;    -- id resuelto en la tabla dispositivos
BEGIN
    -- Identificador = prefijo fijo '484f574c' ("HOWL") + tipo ('4c'=L logger / '42'=B beacon) + id corto en hex a 2 dígitos
    v_identificador := '484f574c'
        || (CASE WHEN p_tipo = 'logger' THEN '4c' ELSE '42' END)
        || lpad(to_hex(p_visto_id), 2, '0');

    SELECT id INTO v_dispositivo_id
    FROM dispositivos
    WHERE identificador = v_identificador;
 
    IF v_dispositivo_id IS NULL THEN
        RAISE WARNING 'No se pudo resolver dispositivo BLE: tipo=%, visto_id=%, identificador esperado=%', p_tipo, p_visto_id, v_identificador;
    END IF;
 
    RETURN v_dispositivo_id;
END;
$function$
;
