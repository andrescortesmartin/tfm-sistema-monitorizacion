# Capa de Aplicación — PostgreSQL

Esta carpeta contiene la persistencia del sistema: la definición de tablas (DDL), las funciones de procesamiento y clasificación (`plpgsql` y `plpython3u`), y el entorno Docker necesario para la función de clasificación de comportamiento.

## Estructura

- `tablas/` — DDL de las tablas, organizado por dominio (ver Anexo X)
- `funciones/` — Funciones plpgsql y plpython3u
- `docker/` — Dockerfile + modelo de clasificación serializado

## Tablas

DDL generado desde DBeaver (Generate SQL → DDL), organizado por dominio funcional según la estructura del Anexo X. Cada fichero incluye el `CREATE TABLE`, sus restricciones de integridad (PK, FK, CHECK, UNIQUE) y los índices adicionales creados para optimizar los patrones de consulta más frecuentes del sistema — no todo índice corresponde a una restricción de integridad. Los triggers definidos sobre cada tabla se incluyen en el mismo fichero, con referencia a la función asociada (ver `funciones/`).

## Funciones

Una función por fichero. La mayoría son `plpgsql` a excepción de `predecir_comportamiento.sql` (`plpython3u`), que carga el modelo de clasificación de comportamiento vía `joblib.load()` desde el fichero incluido en `docker/`.

## Docker

`docker/DOCKERFILE` construye la imagen de PostgreSQL con las dependencias Python necesarias para `plpython3u` (`scikit-learn`, `joblib`) y con `modelo_comportamiento.joblib` (13 KB) ya incluido en la imagen, disponible en `/opt/modelos/modelo_comportamiento.joblib` para su carga desde `predecir_comportamiento.sql`.

## Referencias

| Sección | Memoria | Anexo |
|---|---|---|
| Aplicación / PostgreSQL — Tablas | Capítulo 4.5.2 | Anexo X |
| Aplicación / PostgreSQL — Funciones | Capítulo 4.6.1 a 4.6.4 | Anexo XI y Anexo II |