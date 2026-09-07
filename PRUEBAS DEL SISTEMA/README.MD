# Pruebas del sistema

Pruebas experimentales de validación del sistema: datos crudos (CSV/XLSX exportados de PostgreSQL o embebidos) y notebooks de análisis para cada una de las pruebas de campo. Cada subcarpeta es una prueba independiente con su propio README.

## Contenido

| Carpeta | Prueba | Memoria | Anexo |
|---|---|---|---|
| [`descarga_ble/`](descarga_ble/) | Throughput y RSSI de la descarga de flash por BLE en función de la distancia y el tamaño de paquete | Capítulo 4.7.2 | Anexo XII |
| [`posicionamiento/`](posicionamiento/) | Validación de la posición estimada (ranging LoRa + trilateración y GNSS) frente a referencia por cinta métrica y comparación de ocupación de zonas | Capítulo 4.7.3 | Anexo XII y Anexo II |
| [`indicadores_clasificacion/`](indicadores_clasificacion/) | Validación del modelo de clasificación de comportamiento frente a posturas estáticas de referencia y a vídeo anotado | Capítulo 4.7.4 | Anexo XII |

## Reproducción

Cada notebook se ejecuta de forma autónoma sobre los ficheros de su propia carpeta; no requieren conexión al sistema en producción. Los datos exportados de PostgreSQL corresponden a las campañas de ensayo descritas en la memoria.

## Librerías

Python 3 con Jupyter. El conjunto de paquetes cubre las tres pruebas; cada subcarpeta indica en su README los que realmente usa.

```
pip install jupyter numpy pandas matplotlib openpyxl
```

| Carpeta | Paquetes |
|---|---|
| `descarga_ble/` | `numpy`, `pandas` |
| `posicionamiento/` | `numpy`, `pandas`, `matplotlib` |
| `indicadores_clasificacion/` | `numpy`, `pandas`, `matplotlib`, `openpyxl` |
