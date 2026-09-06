# Entrenamiento del modelo de clasificación de comportamiento

Entrenamiento del modelo de clasificación de comportamiento usado en producción por `predecir_comportamiento.sql` (capa de aplicación). Genera `modelo_comportamiento.joblib`, el mismo fichero servido desde el contenedor Docker de PostgreSQL.

**Nota de alcance:** se entrena con un dataset público de perros instrumentados con IMU, no con datos propios de la especie objetivo, al no disponer de datos etiquetados propios en el momento de este trabajo. Se trata como una prueba de concepto: valida la viabilidad del pipeline de features (VeDBA/pitch/roll) y la arquitectura del clasificador, no un modelo específico de la especie final. Justificación y limitaciones: ver [Referencias](#referencias).

## Dataset

Este notebook no incluye el dataset por tratarse de datos públicos de
terceros.

> Vehkaoja, Antti; Somppi, Sanni; Törnqvist, Heini; Valldeoriola Cardó, Anna; Kumpulainen, Pekka; Väätäjä, Heli; Majaranta, Päivi; Surakka, Veikko; Kujala, Miiamaaria; Vainio, Outi (2025), "Movement Sensor Dataset for Dog Behavior Classification", Mendeley Data, V4, doi: [10.17632/vxhx934tbn.4](https://data.mendeley.com/datasets/vxhx934tbn/4)

## Pipeline

1. Filtrado a las 4 clases objetivo (Trotting, Lying chest, Walking, Standing) combinando las columnas `Behavior_1/2/3` del dataset original.
2. Detección de tramos (*runs*) temporalmente contiguos con la misma etiqueta, para no mezclar comportamientos distintos al construir ventanas.
3. Remuestreo de 100 Hz (nativo del dataset) a 12.5 Hz (frecuencia real del logger) y cálculo de VeDBA/pitch/roll con la misma lógica que `decodificar_sector_imu()` (capa de aplicación), para que las features de entrenamiento sean equivalentes a las de producción.
4. Ventanas de 1 s con `vedba_mean`, `vedba_max`, `pitch_mean`, `roll_mean` (mismas features que `predecir_comportamiento.sql`).
5. Validación Leave-One-Dog-Out (45 perros) comparando MLP (16 neuronas) vs. regresión logística de referencia — F1-macro y matriz de confusión.
6. Entrenamiento final con todos los datos y exportación a `modelo_comportamiento.joblib`, con benchmark de latencia de inferencia y tamaño del modelo serializado.

## Contenido

- `analisis_perros_tfm.ipynb` — notebook completo del proceso anterior

## Librerías

Python 3 con Jupyter. Paquetes: `numpy`, `pandas`, `matplotlib`, `scipy` (remuestreo `decimate`), `scikit-learn` (MLP, regresión logística, validación LOGO), `joblib` (serialización del modelo).

```
pip install jupyter numpy pandas matplotlib scipy scikit-learn joblib
```

> El fichero `modelo_comportamiento.joblib` generado debe cargarse con la misma versión mayor de `scikit-learn`/`joblib` con la que se entrenó (ver capa de aplicación).

## Referencias

| Sección | Memoria | Anexo |
|---|---|---|
| Entrenamiento del modelo de comportamiento. PoC y justificación del dataset | Capítulo 4.6.5 | Anexo XI |