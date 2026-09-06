# Prueba: validación del sistema de posicionamiento

Valida la posición estimada por el sistema (ranging LoRa + trilateración, y GNSS del sensor) frente a una referencia por cinta métrica en 5 puntos de ensayo alrededor del triángulo de anclas. Calcula el sesgo y dispersión del ranging por ancla, el error de posición de RF vs. GPS, y compara la ocupación de zonas (`zonas`/`posicion_zona_hora`) entre ambas fuentes.

## Datos

- `analisis.ipynb` — notebook de análisis
- `mediciones.csv`, `resultados.csv`, `anclas_lora.csv` — export de PostgreSQL: mediciones de ranging/GPS, posición estimada y posición de las anclas
- `zonas.csv`, `posicion_zona_hora.csv` — export de PostgreSQL para la comparación de ocupación por zona

## Librerías

Python 3 con Jupyter. Paquetes: `numpy`, `pandas`, `matplotlib`.

```
pip install jupyter numpy pandas matplotlib
```

## Referencias

| Sección | Memoria | Anexo |
|---|---|---|
| Pruebas — Posicionamiento | Capítulo 4.7.3 | Anexo XII |