# Prueba: throughput y RSSI de descarga BLE

Mide el tiempo de descarga y la potencia de señal recibida (RSSI) al transferir paquetes de distinto tamaño por BLE, en función de la distancia al dispositivo. Analiza correlación distancia - RSSI, RSSI - tiempo/throughput, y distancia - tiempo/throughput, además de estadísticas (media, SD, CV) por tamaño de paquete y distancia.

## Datos

`analisis_descarga.ipynb` — los datos crudos están embebidos directamente en el notebook: 4 tamaños de paquete × 3 distancias × 4repeticiones.

## Librerías

Python 3 con Jupyter. Paquetes: `numpy`, `pandas`.

```
pip install jupyter numpy pandas
```

## Referencias

| Sección | Memoria | Anexo |
|---|---|---|
| Pruebas — Descarga BLE | Capítulo 4.7.2 | Anexo XII |