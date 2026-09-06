# Prueba: validación del modelo de clasificación de comportamiento

Valida `predecir_comportamiento.sql` (capa de aplicación) por dos vías: accuracy sobre dos posturas estáticas de referencia (standing, lying chest), y comparación frente a un vídeo anotado manualmente durante un paseo de 5 minutos (matriz de confusión y accuracy global y por clase).

## Datos

- `analisis.ipynb` — notebook de análisis.
- `standing_result.csv`, `lying_chest_result.csv` — predicciones sobre las posturas estáticas de referencia.
- `indicadores_1s_paseo.csv` — indicadores IMU (VeDBA, pitch/roll) del paseo, con la predicción del modelo por segundo.
- `referencia_video.xlsx` — anotación manual del vídeo del paseo (comportamiento real por tramo), usada como ground truth.

## Librerías

Python 3 con Jupyter. Paquetes: `numpy`, `pandas`, `matplotlib`, `openpyxl` (lectura de `referencia_video.xlsx`).

```
pip install jupyter numpy pandas matplotlib openpyxl
```

## Referencias

| Sección | Memoria | Anexo |
|---|---|---|
| Pruebas — Clasificación de comportamiento | Capítulo 4.7.4 | Anexo XII |