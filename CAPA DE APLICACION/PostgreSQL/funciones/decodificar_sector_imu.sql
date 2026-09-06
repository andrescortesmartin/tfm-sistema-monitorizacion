-- DROP FUNCTION public.decodificar_sector_imu();

CREATE OR REPLACE FUNCTION public.decodificar_sector_imu()
 RETURNS trigger
 LANGUAGE plpgsql
AS $function$
DECLARE
    -- CONFIG DE DESPLIEGUE (leída una única vez al principio del trigger)
    v_escala            FLOAT;       -- factor real de conversión ADC→g (escala_g/32768.0)
    v_max_n_para_dft    INT;         -- límite de muestras para omitir la dft
    v_ventana_din_ms    FLOAT;       -- ventana de referencia estática del din, en ms

	-- CONSTANTES DEL FORMATO DE MUESTRA
    c_sample_bytes CONSTANT INT := 7;  -- 1 byte de tipo + 3 ejes int16
    c_tag_xl       CONSTANT INT := 1;  -- IMU_TAG_XL del firmware
    c_tag_gy       CONSTANT INT := 2;  -- IMU_TAG_GY del firmware

	ms_por_bdr CONSTANT FLOAT[] := ARRAY[
	        80.0,           --  0: sin batch, fallback 12.5 Hz
	        80.0,           --  1: 12.5 Hz
	        38.46153846,    --  2: 26 Hz
	        19.23076923,    --  3: 52 Hz
	        9.61538461,     --  4: 104 Hz
	        4.8076923,      --  5: 208 Hz
	        2.239808153,    --  6: 417 Hz
	        1.20048019,     --  7: 833 Hz
	        0.5998802,      --  8: 1667 Hz
	        0.30003,        --  9: 3333 Hz
	        0.1499925,      -- 10: 6667 Hz
	        153.8461538     -- 11: 6.5 Hz
	    ];

    -- VARIABLES DE CONTROL DEL BUCLE
    -- El sector IMU puede contener múltiples bloques concatenados.
    -- Cada bloque agrupa un burst de muestras capturadas por el FIFO del IMU.
    d                   BYTEA;       -- Copia local del campo data del sector raw
    pos                 INT := 0;    -- Posición actual en el buffer (cursor de lectura)
    header              INT;         -- Byte de cabecera del bloque (0xA2, 0xA3 o 0xA4)
    longitud            INT;         -- Longitud en bytes de las muestras del bloque
    bdr                 INT;         -- Byte de configuración ODR: nibble alto=XL, nibble bajo=GY
    actividad           INT;         -- Flag de actividad: 0=continuo, 1=inicio burst, 2=fin burst, 4=paquete intermedio,
									 -- 8 = paquete periodico, 10=un burst puntual
    block_total         INT;         -- Tamaño total del bloque incluyendo cabecera y padding
    padding             INT;         -- Bytes de relleno para alinear el bloque a 4 bytes
 
    -- VARIABLES DE TIPO Y FRECUENCIA
	tipo_str            VARCHAR(4);  -- Tipo de la muestra actual: 'xl' o 'gy'
    n_muestras          INT;         -- Número de muestras en el bloque (longitud / 7)
    xl_ms               FLOAT;       -- Intervalo entre muestras de acelerómetro, en ms
    gy_ms               FLOAT;       -- Intervalo entre muestras de giroscopio, en ms
    n_xl                INT;         -- Muestras de XL en el bloque (primera pasada)
    n_gy                INT;         -- Muestras de GY en el bloque (primera pasada)
    i_xl                INT;         -- Índice de la muestra de XL en curso
    i_gy                INT;         -- Índice de la muestra de GY en curso
    tag                 INT;         -- Byte de tipo de la muestra actual

    -- VARIABLE DE VENTANA DE ACELERACIÓN DINÁMICA
    v_ventana_muestras  INT;          -- Nº de muestras equivalente a ~2s de referencia estática, recalculado según el ODR real del bloque.
 
    -- VARIABLES DE TIMESTAMP
    ts_sector           BIGINT;      -- Timestamp Unix del bloque (uint32, última muestra)
    ts_muestra          TIMESTAMPTZ; -- Timestamp calculado para cada muestra individual
    ts_inicio_sector    TIMESTAMPTZ;  -- Timestamp de la primera muestra procesada (todo el sector)
    ts_fin_sector       TIMESTAMPTZ;  -- Timestamp de la última muestra procesada (todo el sector)
 
    -- VARIABLES DE DECODIFICACIÓN DE MUESTRAS. Prefijadas con v_ para evitar ambigüedad con columnas de la tabla
    s                   INT;         -- Índice de la muestra actual dentro del bloque
    b                   INT;         -- Posición del primer byte de la muestra actual
	dstart              INT;         -- Posición del primer byte de datos (b + 1)
    n_muestra_contador  INT := 0;    -- Contador global de muestras insertadas
    v_numero_bloque     INT := 0;    -- Índice del bloque dentro de la sesión (1, 2, 3...)
    raw_val             INT;         -- Variable auxiliar para lectura de int16 con signo
    v_xl_x              SMALLINT;    -- Acelerómetro eje X (raw ADC, int16)
    v_xl_y              SMALLINT;    -- Acelerómetro eje Y (raw ADC, int16)
    v_xl_z              SMALLINT;    -- Acelerómetro eje Z (raw ADC, int16)
    v_gy_x              SMALLINT;    -- Giroscopio eje X (raw ADC, int16)
    v_gy_y              SMALLINT;    -- Giroscopio eje Y (raw ADC, int16)
    v_gy_z              SMALLINT;    -- Giroscopio eje Z (raw ADC, int16)
 
BEGIN

    -- Leer config de despliegue activa. Si no hay ninguna, avisar y no
    -- procesar este sector (no perder el dato: sigue en sectores_raw
    -- crudo, se podrá reprocesar en cuanto exista una config activa).
    SELECT escala_acelerometro_g / 32768.0, max_n_muestras_dft, ventana_din_s * 1000.0
    INTO v_escala, v_max_n_para_dft, v_ventana_din_ms
    FROM config_despliegue
    WHERE activo = true
    ORDER BY fecha_creacion DESC
    LIMIT 1;

    IF v_escala IS NULL THEN
        RAISE WARNING 'decodificar_sector_imu: no hay config_despliegue activa, sector_raw_id=% sin procesar', NEW.id;
        RETURN NEW;
    END IF;

    d := NEW.data;
 
    -- BUCLE PRINCIPAL: itera sobre los bloques IMU del sector
    -- Estructura de cada bloque:
    --   [1 byte]  cabecera (0xA2=solo XL, 0xA3=solo GY, 0xA4=XL+GY)
    --   [2 bytes] longitud de muestras (little-endian)
    --   [1 byte]  bdr (nibble alto=ODR XL, nibble bajo=ODR GY)
    --   [1 byte]  actividad
    --   [N bytes] muestras (7 bytes cada una [tipo][x_lo][x_hi][y_lo][y_hi][z_lo][z_hi])
    --   [4 bytes] timestamp Unix de la última muestra (little-endian)
    --   [M bytes] padding para alinear el bloque total a 4 bytes
    WHILE pos < octet_length(d) LOOP
 
        header := get_byte(d, pos);
 
        -- Verificar que la cabecera es válida (0xA2, 0xA3 o 0xA4)
        IF header <> 0xA2 AND header <> 0xA3 AND header <> 0xA4 THEN
            IF pos + 5 + 4 > octet_length(d) THEN
                EXIT;  -- no queda margen para otra cabecera completa, fin del sector
            END IF;
            pos := pos + 1;
            CONTINUE;
        END IF;
 
        -- Leer campos de la cabecera del bloque (5 bytes totales)
        longitud  := get_byte(d, pos + 1) | (get_byte(d, pos + 2) << 8);
        bdr       := get_byte(d, pos + 3);
        actividad := get_byte(d, pos + 4);
        pos       := pos + 5;

		-- Todas las muestras ocupan lo mismo: el tipo lo dice su primer byte, no la cabecera del bloque.
        n_muestras := longitud / c_sample_bytes;
 
        -- Cada tipo avanza a su propia frecuecia: nibble alto = BDR del XL, nibble bajo = BDR del GY
        xl_ms := COALESCE(ms_por_bdr[((bdr >> 4) & 0x0F) + 1], 80.0);
        gy_ms := COALESCE(ms_por_bdr[(bdr & 0x0F) + 1], 80.0);

        -- Nº de muestras de acelerómetro equivalente a la ventana de referencia estática, según el ODR real de este bloque
        v_ventana_muestras := GREATEST(1, round(v_ventana_din_ms / xl_ms)::INT);
 
        -- Leer timestamp del bloque (4 bytes uint32 LE tras las muestras)
        ts_sector := get_byte(d, pos + longitud)
                   | (get_byte(d, pos + longitud + 1) << 8)
                   | (get_byte(d, pos + longitud + 2) << 16)
                   | (get_byte(d, pos + longitud + 3) << 24);
 
        -- Descartar bloques sin timestamp válido (no cuentan como numero_bloque)
        IF ts_sector = 0 THEN
            padding     := (4 - ((5 + longitud + 4) % 4)) % 4;
            block_total := 5 + longitud + 4 + padding;
            pos         := pos - 5 + block_total;
            CONTINUE;
        END IF;
 
        -- Bloque válido: incrementamos el contador de bloque de la sesión
        v_numero_bloque := v_numero_bloque + 1;

		-- Primero se cuenta cuántas muestras hay de cada tipo. Hace falta para reconstruir hacia atrás la frecuencia de cada serie por separado.
        n_xl := 0;
        n_gy := 0;
        FOR s IN 0..n_muestras - 1 LOOP
            tag := get_byte(d, pos + s * c_sample_bytes);
            IF tag = c_tag_xl THEN
                n_xl := n_xl + 1;
            ELSIF tag = c_tag_gy THEN
                n_gy := n_gy + 1;
            END IF;
        END LOOP;
 
        -- Bucle donde se decodifica e inserta cada muestra del bloque
		i_xl := 0;
        i_gy := 0;
        FOR s IN 0..n_muestras - 1 LOOP
            b := pos + s * c_sample_bytes;
			tag    := get_byte(d, b); -- tipo de dato (acelerometro o giroscopio)
            dstart := b + 1;   -- los datos empiezan tras el byte de tipo
 
            v_xl_x := NULL; v_xl_y := NULL; v_xl_z := NULL;
            v_gy_x := NULL; v_gy_y := NULL; v_gy_z := NULL;
			
			IF tag = c_tag_xl THEN -- Muestra de acelerometro
                -- Timestamp hacia atrás desde el volcado, contando solo muestras de XL
                ts_muestra := to_timestamp(ts_sector)
                            - make_interval(secs => (n_xl - 1 - i_xl) * xl_ms / 1000.0);
                i_xl     := i_xl + 1;
                tipo_str := 'xl';

                -- Acelerómetro (XL): 3 ejes × int16 LE = 6 bytes
                -- Escala aplicada más adelante según config_despliegue.escala_acelerometro_g
                raw_val := get_byte(d, dstart) | (get_byte(d, dstart + 1) << 8);
                IF raw_val > 32767 THEN raw_val := raw_val - 65536; END IF;
                v_xl_x := raw_val;

                raw_val := get_byte(d, dstart + 2) | (get_byte(d, dstart + 3) << 8);
                IF raw_val > 32767 THEN raw_val := raw_val - 65536; END IF;
                v_xl_y := raw_val;

                raw_val := get_byte(d, dstart + 4) | (get_byte(d, dstart + 5) << 8);
                IF raw_val > 32767 THEN raw_val := raw_val - 65536; END IF;
                v_xl_z := raw_val;

            ELSIF tag = c_tag_gy THEN -- Muestra de giroscopio
                -- Timestamp hacia atrás desde el volcado, contando solo muestras de GY
                ts_muestra := to_timestamp(ts_sector)
                            - make_interval(secs => (n_gy - 1 - i_gy) * gy_ms / 1000.0);
                i_gy     := i_gy + 1;
                tipo_str := 'gy';

                -- Giroscopio (GY): 3 ejes × int16 LE = 6 bytes
                -- Escala: valor * 1000.0 / 32768.0 = dps
                raw_val := get_byte(d, dstart) | (get_byte(d, dstart + 1) << 8);
                IF raw_val > 32767 THEN raw_val := raw_val - 65536; END IF;
                v_gy_x := raw_val;

                raw_val := get_byte(d, dstart + 2) | (get_byte(d, dstart + 3) << 8);
                IF raw_val > 32767 THEN raw_val := raw_val - 65536; END IF;
                v_gy_y := raw_val;

                raw_val := get_byte(d, dstart + 4) | (get_byte(d, dstart + 5) << 8);
                IF raw_val > 32767 THEN raw_val := raw_val - 65536; END IF;
                v_gy_z := raw_val;

            ELSE
                CONTINUE;  -- tipo desconocido, muestra descartada
            END IF;
 
          	-- Rango temporal del sector. Las dos series se intercalan, asi que los timestamps NO llegan en orden: hay que quedarse con min y max
            ts_inicio_sector := LEAST(COALESCE(ts_inicio_sector, ts_muestra), ts_muestra);
            ts_fin_sector    := GREATEST(COALESCE(ts_fin_sector, ts_muestra), ts_muestra);

            -- INSERT en mediciones_rapidas
            -- ON CONFLICT DO NOTHING: si ya existe una muestra con el mismo dispositivo, timestamp y tipo, se ignora (descarga duplicada). 
			-- El tipo forma parte de la clave porque la ultima muestra de XL y la de GY caen en el mismo instante.
            INSERT INTO mediciones_rapidas (dispositivo_id, sector_raw_id, timestamp, n_muestra, numero_bloque, actividad, tipo, xl_x, xl_y, xl_z, gy_x, gy_y, gy_z)
            VALUES (NEW.dispositivo_id, NEW.id, ts_muestra, n_muestra_contador, v_numero_bloque, actividad, tipo_str, v_xl_x, v_xl_y, v_xl_z, v_gy_x, v_gy_y, v_gy_z)
            ON CONFLICT (dispositivo_id, timestamp, tipo) DO NOTHING;

            -- UPDATE aceleración dinámica (din_x, din_y, din_z), solo para muestras de XL.
            -- Media móvil de referencia estática (~ventana_din_s reales, según ODR).
            -- Restringida al MISMO bloque (sector_raw_id + numero_bloque) para no contaminar la referencia con el bloque anterior si
            -- hay un gap temporal entre bursts de actividad.
            IF tag = c_tag_xl THEN
                UPDATE mediciones_rapidas m SET -- Se actualiza la fila que se acaba de insertar
                    din_x = m.xl_x - avg_vals.avg_x, -- Resto al valor crudo su media movil (su componente estática)
                    din_y = m.xl_y - avg_vals.avg_y,
                    din_z = m.xl_z - avg_vals.avg_z
                FROM (
                    SELECT
                        AVG(r2.xl_x) AS avg_x,
                        AVG(r2.xl_y) AS avg_y,
                        AVG(r2.xl_z) AS avg_z
                    FROM (
                        SELECT r2.xl_x, r2.xl_y, r2.xl_z
                        FROM mediciones_rapidas r2
                        WHERE r2.dispositivo_id = NEW.dispositivo_id
                        AND r2.sector_raw_id = NEW.id
                        AND r2.numero_bloque = v_numero_bloque
                        AND r2.tipo = 'xl'
                        AND r2.timestamp <= ts_muestra
                        AND r2.xl_x IS NOT NULL
                        ORDER BY r2.timestamp DESC
                        LIMIT v_ventana_muestras
                    ) r2
                ) avg_vals
                WHERE m.dispositivo_id = NEW.dispositivo_id
                AND m.timestamp = ts_muestra
                AND m.tipo = 'xl';
            END IF;

            n_muestra_contador := n_muestra_contador + 1;
        END LOOP;

        -- Calcular indicadores de ventana (VeDBA + dft) para este bloque
        PERFORM calcular_indicadores_imu_ventana(
            NEW.dispositivo_id,
            NEW.id,
            v_numero_bloque,
            v_escala,
            v_max_n_para_dft
        );
 
        -- AVANCE AL SIGUIENTE BLOQUE
        padding     := (4 - ((5 + longitud + 4) % 4)) % 4;
        block_total := 5 + longitud + 4 + padding;
        pos         := pos - 5 + block_total;
 
    END LOOP;
 
    -- Calcular indicadores de 1s para el rango temporal cubierto por este sector
    IF ts_inicio_sector IS NOT NULL THEN
        PERFORM calcular_indicadores_imu_1s(
            NEW.dispositivo_id,
            NEW.id,
            ts_inicio_sector,
            ts_fin_sector,
            v_escala
        );
    END IF;
 
    RETURN NEW;
END;
$function$
;
