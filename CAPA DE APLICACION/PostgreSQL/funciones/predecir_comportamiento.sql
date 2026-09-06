-- DROP FUNCTION public.predecir_comportamiento(float8, float8, float8, float8);

CREATE OR REPLACE FUNCTION public.predecir_comportamiento(p_vedba_mean double precision, p_vedba_max double precision, p_pitch_mean double precision, p_roll_mean double precision)
 RETURNS character varying
 LANGUAGE plpython3u
 STABLE
AS $function$
    # El modelo se carga una sola vez por sesión y se cachea en SD (dict de estado)
    if 'modelo' not in SD:
        import joblib
        SD['modelo'] = joblib.load('/opt/modelos/modelo_comportamiento.joblib')

    modelo = SD['modelo']
    # El modelo espera una matriz de muestras; aquí solo hay una fila de 4 features
    entrada = [[p_vedba_mean, p_vedba_max, p_pitch_mean, p_roll_mean]]
    prediccion = modelo.predict(entrada)[0]

    return prediccion
$function$
;
