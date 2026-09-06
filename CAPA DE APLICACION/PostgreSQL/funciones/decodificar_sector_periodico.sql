-- DROP FUNCTION public.decodificar_sector_periodico();

CREATE OR REPLACE FUNCTION public.decodificar_sector_periodico()
 RETURNS trigger
 LANGUAGE plpgsql
AS $function$
DECLARE
    d            BYTEA;          -- copia local del campo data del sector
    pos          INT := 0;       -- cursor de lectura dentro del buffer
    longitud     INT;            -- longitud declarada del sub-paquete
    padding      INT;            -- bytes de relleno para alinear a 4

    flags        INT;            -- byte de flags: indica qué campos trae el sub-paquete
    bateria      INT;            -- tensión de batería en mV
    raw_int      INT;            -- auxiliar para leer enteros con signo
    presion      INT;            -- presión (raw, int16)
    temperatura  INT;            -- temperatura (raw, int8)
    pitch        FLOAT;          -- cabeceo en grados (raw/10)
    roll         FLOAT;          -- alabeo en grados (raw/10)
    gps_lat      FLOAT;          -- latitud (raw/1e6)
    gps_lon      FLOAT;          -- longitud (raw/1e6)
    ranging      JSONB;          -- medidas de distancia UWB por ancla
    loggers      JSONB;          -- avistamientos de loggers BLE
    beacons      JSONB;          -- avistamientos de beacons BLE
    ts           TIMESTAMPTZ;    -- timestamp del sub-paquete

    ranging_len  INT;            -- nº de anclas en el bloque de ranging
    ranging_arr  JSONB;          -- acumulador del array de ranging
    ranging_addrs BIGINT[];      -- direcciones de las anclas (en orden de aparición)
    i            INT;            -- índice de ancla / de logger / de beacon
    addr         BIGINT;         -- dirección de la ancla en curso
    n_muestras_anc INT;          -- nº de medidas de la ancla en curso
    muestras_arr JSONB;          -- acumulador de medidas de una ancla
    j            INT;            -- índice de medida dentro de la ancla
    m_dist       INT;            -- distancia medida (int16 con signo)
    m_rssi       INT;            -- RSSI de la medida (int8 con signo)

    logger_count INT;            -- nº de loggers en el bloque BLE
    beacon_count INT;            -- nº de beacons en el bloque BLE
    loggers_arr  JSONB;          -- acumulador del array de loggers
    beacons_arr  JSONB;          -- acumulador del array de beacons
    b_id         INT;            -- id del dispositivo BLE avistado
    b_rssi       INT;            -- RSSI del avistamiento (int8 con signo)
    b_obs        INT;            -- nº de observaciones en la ventana
    b_len        INT;            -- longitud del payload del beacon
    b_data       TEXT;           -- payload del beacon en hex

    v_medicion_id INT;           -- id de la fila insertada en mediciones_lentas_ble

BEGIN
    d := NEW.data;

    -- Bucle principal: recorre los sub-paquetes concatenados del sector
    WHILE pos < octet_length(d) LOOP

        -- Cada sub-paquete empieza por 0xA1; si no, se ha llegado al final / padding
        IF get_byte(d, pos) <> 0xA1 THEN
            EXIT;
        END IF;

        -- Cabecera: 0xA1 + longitud (2 bytes LE)
        longitud := get_byte(d, pos + 1) | (get_byte(d, pos + 2) << 8);
        pos := pos + 3;

        -- Se limpian los campos opcionales (solo se rellenan si su flag está activo)
        presion     := NULL;
        temperatura := NULL;
        pitch       := NULL;
        roll        := NULL;
        gps_lat     := NULL;
        gps_lon     := NULL;
        ranging     := NULL;
        loggers     := NULL;
        beacons     := NULL;

        -- Byte de flags: cada bit indica la presencia de un bloque de datos
        flags := get_byte(d, pos);
        pos := pos + 1;

        -- Batería: siempre presente (2 bytes LE, mV)
        bateria := get_byte(d, pos) | (get_byte(d, pos + 1) << 8);
        pos := pos + 2;

        -- flag bit 3 (8): presión (int16 LE) + temperatura (int8)
        IF (flags & 8) <> 0 THEN
            presion := get_byte(d, pos) | (get_byte(d, pos + 1) << 8);
            IF presion > 32767 THEN presion := presion - 65536; END IF;
            pos := pos + 2;

            temperatura := get_byte(d, pos);
            IF temperatura > 127 THEN temperatura := temperatura - 256; END IF;
            pos := pos + 1;
        END IF;

        -- flag bit 6 (64): pitch y roll (int16 LE, en décimas de grado)
        IF (flags & 64) <> 0 THEN
            raw_int := get_byte(d, pos) | (get_byte(d, pos + 1) << 8);
            IF raw_int > 32767 THEN raw_int := raw_int - 65536; END IF;
            pitch := raw_int / 10.0;
            pos := pos + 2;

            raw_int := get_byte(d, pos) | (get_byte(d, pos + 1) << 8);
            IF raw_int > 32767 THEN raw_int := raw_int - 65536; END IF;
            roll := raw_int / 10.0;
            pos := pos + 2;
        END IF;

        -- flag bit 0 (1): coordenadas GPS (int32 LE, en millonésimas de grado)
        IF (flags & 1) <> 0 THEN
            raw_int := get_byte(d, pos) | (get_byte(d, pos + 1) << 8) | (get_byte(d, pos + 2) << 16) | (get_byte(d, pos + 3) << 24);
            IF raw_int > 2147483647 THEN raw_int := raw_int - 4294967296; END IF;
            gps_lat := raw_int / 1000000.0;
            pos := pos + 4;

            raw_int := get_byte(d, pos) | (get_byte(d, pos + 1) << 8) | (get_byte(d, pos + 2) << 16) | (get_byte(d, pos + 3) << 24);
            IF raw_int > 2147483647 THEN raw_int := raw_int - 4294967296; END IF;
            gps_lon := raw_int / 1000000.0;
            pos := pos + 4;
        END IF;

        -- flag bit 2 (4): bloque de ranging UWB
        IF (flags & 4) <> 0 THEN
            ranging_arr := '[]';
            -- nº de anclas
            ranging_len := get_byte(d, pos);
            pos := pos + 1;

            -- Primero vienen las direcciones de todas las anclas (4 bytes LE cada una)
            ranging_addrs := ARRAY[]::BIGINT[];
            FOR i IN 0..ranging_len - 1 LOOP
                addr := get_byte(d, pos + i * 4)::BIGINT
                    | (get_byte(d, pos + i * 4 + 1)::BIGINT << 8)
                    | (get_byte(d, pos + i * 4 + 2)::BIGINT << 16)
                    | (get_byte(d, pos + i * 4 + 3)::BIGINT << 24);
                ranging_addrs[i] := addr;
            END LOOP;
            pos:= pos + ranging_len * 4;

            -- Después, para cada ancla, su lista de medidas
            FOR i IN 0..ranging_len - 1 LOOP
                -- nº de medidas de esta ancla
                n_muestras_anc := get_byte(d,pos);
                pos := pos + 1;

                -- Layout por ancla: N distancias (int16 LE) seguidas de N RSSI (int8)
                muestras_arr := '[]';
                FOR j IN 0..n_muestras_anc - 1 LOOP
                    m_dist := get_byte(d, pos + j*2) | (get_byte(d, pos + j*2 + 1) << 8);
                    IF m_dist > 32767 THEN m_dist := m_dist - 65536; END IF;

                    m_rssi := get_byte(d, pos + n_muestras_anc*2 + j);
                    IF m_rssi > 127 THEN m_rssi := m_rssi - 256; END IF;

                    muestras_arr := muestras_arr || jsonb_build_object('dist', m_dist, 'rssi', m_rssi);
                END LOOP;
                pos := pos + n_muestras_anc * 3;   -- 2 bytes dist + 1 byte rssi por medida

                ranging_arr := ranging_arr || jsonb_build_object('addr',ranging_addrs[i], 'muestras', muestras_arr);
            END LOOP;

            ranging := ranging_arr;
        END IF;

        -- flag bit 1 (2): bloque de avistamientos BLE (loggers + beacons)
        IF (flags & 2) <> 0 THEN
            loggers_arr := '[]';
            beacons_arr := '[]';

            -- Loggers: id + rssi + nº de observaciones (3 bytes útiles, +1 byte saltado)
            logger_count := get_byte(d, pos);
            pos := pos + 1;

            FOR i IN 0..logger_count - 1 LOOP
                pos := pos + 1;   -- byte de longitud/reservado, no usado en loggers
                b_id   := get_byte(d, pos);
                b_rssi := get_byte(d, pos + 1);
                IF b_rssi > 127 THEN b_rssi := b_rssi - 256; END IF;
                b_obs  := get_byte(d, pos + 2);
                pos := pos + 3;
                loggers_arr := loggers_arr || jsonb_build_object('id', b_id, 'rssi', b_rssi, 'obs', b_obs);
            END LOOP;

            -- Beacons: como los loggers pero con payload adicional de longitud variable
            beacon_count := get_byte(d, pos);
            pos := pos + 1;

            FOR i IN 0..beacon_count - 1 LOOP
                pos := pos + 1;   -- byte reservado
                b_id   := get_byte(d, pos);
                b_rssi := get_byte(d, pos + 1);
                IF b_rssi > 127 THEN b_rssi := b_rssi - 256; END IF;
                b_obs  := get_byte(d, pos + 2);
                b_len  := get_byte(d, pos + 3);   -- longitud del payload
                pos := pos + 4;
                b_data := encode(substring(d from pos + 1 for b_len), 'hex');
                pos := pos + b_len;
                beacons_arr := beacons_arr || jsonb_build_object('id', b_id, 'rssi', b_rssi, 'obs', b_obs, 'data', b_data);
            END LOOP;

            loggers := loggers_arr;
            beacons := beacons_arr;
        END IF;

        -- Cierre del sub-paquete: timestamp Unix (uint32 LE)
        ts := to_timestamp(
            get_byte(d, pos) | (get_byte(d, pos + 1) << 8) | (get_byte(d, pos + 2) << 16) | (get_byte(d, pos + 3) << 24)
        );
        pos := pos + 4;

        -- Upsert en mediciones_lentas_ble. En caso de conflicto se conserva el
        -- valor previo de cada campo opcional si el nuevo sub-paquete no lo trae
        -- (COALESCE con la fila existente).
        INSERT INTO mediciones_lentas_ble
            (dispositivo_id, sector_raw_id, timestamp, bateria_mv, presion, temperatura, pitch, roll, gps_lat, gps_lon, ranging, loggers, beacons)
        VALUES
            (NEW.dispositivo_id, NEW.id, ts, bateria, presion, temperatura, pitch, roll, gps_lat, gps_lon, ranging, loggers, beacons)
        ON CONFLICT (dispositivo_id, timestamp) DO UPDATE SET
            sector_raw_id = EXCLUDED.sector_raw_id,
            bateria_mv    = EXCLUDED.bateria_mv,
            presion       = COALESCE(EXCLUDED.presion,     mediciones_lentas_ble.presion),
            temperatura   = COALESCE(EXCLUDED.temperatura, mediciones_lentas_ble.temperatura),
            pitch         = COALESCE(EXCLUDED.pitch,       mediciones_lentas_ble.pitch),
            roll          = COALESCE(EXCLUDED.roll,        mediciones_lentas_ble.roll),
            gps_lat       = COALESCE(EXCLUDED.gps_lat,     mediciones_lentas_ble.gps_lat),
            gps_lon       = COALESCE(EXCLUDED.gps_lon,     mediciones_lentas_ble.gps_lon),
            ranging       = COALESCE(EXCLUDED.ranging,     mediciones_lentas_ble.ranging),
            loggers       = COALESCE(EXCLUDED.loggers,     mediciones_lentas_ble.loggers),
            beacons       = COALESCE(EXCLUDED.beacons,     mediciones_lentas_ble.beacons)
        RETURNING id INTO v_medicion_id;

        -- Si el sub-paquete trae ranging, se calcula la posición por trilateración
        IF ranging IS NOT NULL THEN
            PERFORM fn_calcular_posicion_ranging(
                NEW.dispositivo_id, ranging, v_medicion_id, NULL, 'ble', ts
            );
        END IF;

		-- Si trae presión/temperatura, se registra el gradiente presión-temperatura
        IF presion IS NOT NULL THEN
		    PERFORM fn_registrar_gradiente_presion_temp(
		        p_dispositivo_id  => NEW.dispositivo_id,
		        p_origen          => 'ble',
		        p_timestamp       => ts,
		        p_presion         => presion::SMALLINT,
		        p_temperatura     => temperatura::SMALLINT,
		        p_medicion_ble_id => v_medicion_id
		    );
		END IF;

        -- Avance al siguiente sub-paquete: se alinea la posición a múltiplo de 4
        padding := (4 - ((longitud + 3) % 4)) % 4;
        pos := pos + padding;

    END LOOP;

    RETURN NEW;
END;
$function$
;
