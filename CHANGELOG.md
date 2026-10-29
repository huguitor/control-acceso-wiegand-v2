# Changelog

Formato: [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/) · Versionado semántico.

## [Sin publicar]

### Documentación
- README, manual de instalación y uso, protocolo serial y guía de desarrollo.
- URL del repo actualizada en el encabezado del sketch y en el mensaje de debug.

## [2.3.0] — primera versión en este repositorio

Estado del firmware al momento de crear el repo:

- Lectura Wiegand 26 bits por interrupciones FALLING (`receivedBit`), 1 o 2 lectoras (`NUM_LECTORAS`).
- Capacidad 200 tarjetas (UNO) / 1000 tarjetas (Mega) con migración automática de EEPROM en el Mega.
- Borrado O(1) (mueve la última tarjeta al hueco) y reset por contador.
- Filtro de UIDs inválidos (0 y 16777215) con evento `ERROR:...:INVALID_UID`.
- Estadísticas por lectora: `CMD:STATS`, `CMD:STATS:RESET`.
- Evento `BOOT` con versión de FW, librería, cantidad de lectoras y capacidad; `CMD:VERSION`.
- Comandos remotos `CMD:ENROLL`, `CMD:DELETE`, `CMD:LIST`, `CMD:RESET`.
- Desarme de alarma por tarjeta autorizada (pin 11).

## Anteriores

Versión de 1 lectora (UNO), tag `v1.0.0`:
https://github.com/huguitor/arduino-control-acceso-wiegand
