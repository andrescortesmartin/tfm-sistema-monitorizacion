# Baliza móvil (BLE beacon)

## Rol en el sistema

Baliza BLE portada por el animal. A diferencia de los nodos sensores y las anclas (basados en nRF52840 + LR1110), este dispositivo no lleva microcontrolador programable ni pila LoRaWAN: es un emisor BLE puro, no conectable, que transmite un paquete de telemetría con periodo fijo y no participa en ninguna otra lógica (sin recepción, sin almacenamiento).

El detalle de funcionamiento (diseño hardware, justificación de componentes, esquemático, layout y secuencias de lectura de cada sensor) está en la documentación indicada en [Referencias](#referencias).

## Hardware (resumen)

| Bloque | Componente | Función |
|---|---|---|
| SoC / radio | IN100 (InPlay) | Radio BLE |
| Orientación | LSM303AGR (STMicroelectronics) | Acelerómetro 3D + magnetómetro 3D (brújula) |
| Ambiental (luz) | Fotorresistor NSL-SMD6549 (Advanced Photonix) | Divisor resistivo → ADC del SoC |
| Alimentación | 2× pila botón óxido de plata (soporte BK-335-SM) | 3 V nominales (2× 1.5 V en serie) |

PCB de 13.5 × 15.6 mm. Detalle de diseño (control de alimentación de periféricos vía "pin de carga", divisor resistivo de la fotorresistencia, cálculo de impedancia de antena en Altium): ver Anexo III.

## Programación

El IN100 no tiene flash regrabable: usa una memoria OTP (eFuse, 4 kB), una vez grabado, el comportamiento no se puede modificar. La programación se hace con la herramienta (NanoBeacon™ Config Tool) del fabricante (InPlay), conectada por UART, cargando el fichero de configuración de esta carpeta:

- `nano_beacon_config.cfg` fichero de configuración generado por la herramienta: parámetros de advertising BLE (intervalo, dirección, TX power) y el payload de datos a transmitir.

## Referencias

| Sección | Memoria | Anexo |
|---|---|---|
| Baliza móvil — Hardware y firmware | Capítulo 4 | Anexo III |