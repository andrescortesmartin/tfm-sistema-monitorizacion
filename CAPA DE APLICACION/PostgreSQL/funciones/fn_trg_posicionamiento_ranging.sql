-- DROP FUNCTION public.fn_trg_posicionamiento_ranging();

CREATE OR REPLACE FUNCTION public.fn_trg_posicionamiento_ranging()
 RETURNS trigger
 LANGUAGE plpgsql
AS $function$
BEGIN
    IF NEW.lat_estimada IS NOT NULL AND NEW.lon_estimada IS NOT NULL THEN
        PERFORM fn_registrar_posicionamiento(NEW.dispositivo_id, NEW.lat_estimada, NEW.lon_estimada, NEW.timestamp, 'ranging');
    END IF;
    RETURN NEW;
END;
$function$
;
