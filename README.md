# Control de acceso Wiegand v2

Firmware para Arduino UNO / Mega que controla una puerta con 1 o 2 lectoras
Wiegand 26 bits, cerradura por relé, pulsador de salida, sensor magnético,
buzzer, LED y relé de desarme de alarma. Reporta todo por serial a un
gateway ESP8266 → MQTT y acepta comandos remotos (alta, baja, listado de tarjetas).

**Versión actual:** FW `2.3.2` · Librería Wiegand `2.0.0`

## Características

- Lectura Wiegand **por interrupciones** (no se pierden bits).
- 1 lectora (UNO o Mega) o 2 lectoras (solo Mega), se elige con `NUM_LECTORAS`.
- Hasta **200 tarjetas** (UNO) o **1000 tarjetas** (Mega) en EEPROM + tarjetas fijas en flash.
- Alta de tarjetas en el equipo con **tarjeta maestra** (modo grabación) o remota por MQTT.
- Todo *non-blocking*: buzzer, temporizadores y antirrebotes con `millis()`.
- Filtra UIDs basura (0 y 16777215) y cuenta estadísticas por lectora.
- Migración automática de EEPROM del layout viejo (200) al nuevo (1000) en el Mega.

## Documentación

| Documento | Para qué |
|---|---|
| [docs/MANUAL.md](docs/MANUAL.md) | Manual de instalación y uso: cableado, carga, operación diaria, solución de problemas |
| [docs/PROTOCOLO.md](docs/PROTOCOLO.md) | Protocolo serial Arduino ↔ gateway (eventos y comandos) |
| [docs/DESARROLLO.md](docs/DESARROLLO.md) | Arquitectura del código, EEPROM, cómo extenderlo, problemas conocidos y pendientes |
| [CHANGELOG.md](CHANGELOG.md) | Historial de versiones |

## Inicio rápido

1. Instalar en el Arduino IDE la librería **"Yet Another Arduino Wiegand Library"** v2.0.0 (Paulo Costa).
2. Abrir `control_acceso_wiegand_v2.ino`.
3. Ajustar `NUM_LECTORAS` (1 o 2), las tarjetas maestras y las tarjetas fijas.
4. Elegir la placa (**Arduino Mega 2560** si son 2 lectoras) y cargar.
5. **Desconectar el gateway de los pines 0/1 antes de cargar** y reconectarlo después.

## Proyectos relacionados

- Gateway ESP8266 → MQTT: `~/Arduino/esp8266_gateway_wiegand_mqtt` (todavía sin repo).
- Versión anterior (1 lectora, UNO): https://github.com/huguitor/arduino-control-acceso-wiegand
