-- DROP FUNCTION public.fn_clasificar_anillo_ble(int2);

CREATE OR REPLACE FUNCTION public.fn_clasificar_anillo_ble(p_rssi smallint)
 RETURNS text
 LANGUAGE plpgsql
AS $function$
DECLARE
    v_umbral_inmediata SMALLINT;   -- RSSI mínimo para considerar "inmediata"
    v_umbral_cercana   SMALLINT;   -- RSSI mínimo para considerar "cercana"
BEGIN
    -- Umbrales de la última config de despliegue activa
    SELECT anillo_logger_rssi_umbral_inmediata, anillo_logger_rssi_umbral_cercana
      INTO v_umbral_inmediata, v_umbral_cercana
      FROM config_despliegue
     WHERE activo = true
     ORDER BY fecha_creacion DESC
     LIMIT 1;

    -- Sin config activa no hay umbrales: no se puede clasificar
    IF v_umbral_inmediata IS NULL THEN
        RAISE EXCEPTION 'No hay config_despliegue activa: no se puede clasificar anillo BLE';
    END IF;

    -- Clasificación por tramos de RSSI (mayor RSSI = más cerca)
    IF p_rssi IS NULL THEN
        RETURN NULL;
    ELSIF p_rssi >= v_umbral_inmediata THEN
        RETURN 'inmediata';
    ELSIF p_rssi >= v_umbral_cercana THEN
        RETURN 'cercana';
    ELSE
        RETURN 'lejana';
    END IF;
END;
$function$
;
