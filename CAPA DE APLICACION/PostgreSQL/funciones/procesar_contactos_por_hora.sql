-- DROP FUNCTION public.procesar_contactos_por_hora();

CREATE OR REPLACE FUNCTION public.procesar_contactos_por_hora()
 RETURNS trigger
 LANGUAGE plpgsql
AS $function$
DECLARE
    v_slot_inicio TIMESTAMPTZ;   -- inicio de la hora local a la que se imputa el contacto
BEGIN
    -- Truncar a la hora exacta, en hora de Madrid
    v_slot_inicio := (date_trunc('hour', NEW.ts_ventana AT TIME ZONE 'Europe/Madrid')) AT TIME ZONE 'Europe/Madrid';

    -- Upsert del resumen horario: la primera ventana crea la fila con los contadores a 1; las siguientes acumulan sobre lo ya guardado.
    -- rssi_medio se mantiene como media incremental sobre n_rssi_validos (las ventanas sin RSSI cuentan como ventana pero no alteran la media).
    INSERT INTO contactos_por_hora
        (dispositivo_id, visto_dispositivo_id, tipo, slot_inicio,
         n_ventanas, n_rssi_validos, primer_contacto, ultimo_contacto, rssi_medio, rssi_max,
         n_inmediata, n_cercana, n_lejana)
    VALUES
        (NEW.dispositivo_id, NEW.visto_dispositivo_id, NEW.tipo, v_slot_inicio, 1,
         CASE WHEN NEW.rssi IS NOT NULL THEN 1 ELSE 0 END,
         NEW.ts_ventana, NEW.ts_ventana, NEW.rssi, NEW.rssi,
         CASE WHEN NEW.anillo = 'inmediata' THEN 1 ELSE 0 END,
         CASE WHEN NEW.anillo = 'cercana'   THEN 1 ELSE 0 END,
         CASE WHEN NEW.anillo = 'lejana'    THEN 1 ELSE 0 END)
    ON CONFLICT (dispositivo_id, visto_dispositivo_id, tipo, slot_inicio)
    DO UPDATE SET
        n_ventanas       = contactos_por_hora.n_ventanas + 1,
        primer_contacto  = LEAST(contactos_por_hora.primer_contacto, NEW.ts_ventana),
        ultimo_contacto  = GREATEST(contactos_por_hora.ultimo_contacto, NEW.ts_ventana),
        n_rssi_validos   = contactos_por_hora.n_rssi_validos
                           + CASE WHEN NEW.rssi IS NOT NULL THEN 1 ELSE 0 END,
        rssi_medio       = CASE
                              WHEN NEW.rssi IS NULL THEN contactos_por_hora.rssi_medio
                              WHEN contactos_por_hora.n_rssi_validos = 0 THEN NEW.rssi
                              ELSE (contactos_por_hora.rssi_medio * contactos_por_hora.n_rssi_validos + NEW.rssi)
                                   / (contactos_por_hora.n_rssi_validos + 1.0)
                           END,
        rssi_max         = GREATEST(contactos_por_hora.rssi_max, NEW.rssi),
        n_inmediata      = contactos_por_hora.n_inmediata + CASE WHEN NEW.anillo = 'inmediata' THEN 1 ELSE 0 END,
        n_cercana        = contactos_por_hora.n_cercana   + CASE WHEN NEW.anillo = 'cercana'   THEN 1 ELSE 0 END,
        n_lejana         = contactos_por_hora.n_lejana    + CASE WHEN NEW.anillo = 'lejana'    THEN 1 ELSE 0 END,
        actualizado_en   = now();

    RETURN NEW;
END;
$function$
;
