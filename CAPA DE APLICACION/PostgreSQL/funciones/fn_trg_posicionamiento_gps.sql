-- DROP FUNCTION public.fn_trg_posicionamiento_gps();

CREATE OR REPLACE FUNCTION public.fn_trg_posicionamiento_gps()
 RETURNS trigger
 LANGUAGE plpgsql
AS $function$
BEGIN
    IF NEW.gps_lat IS NOT NULL AND NEW.gps_lon IS NOT NULL THEN
        PERFORM fn_registrar_posicionamiento(NEW.dispositivo_id, NEW.gps_lat, NEW.gps_lon, NEW.timestamp, 'gps');
    END IF;
    RETURN NEW;
END;
$function$
;
