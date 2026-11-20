# Changelog

Formato: [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/) · Versionado semántico.

## [2.4.0] — 2026-11-05

### Agregado
- **Suspensión y licencia por tiempo de uso**, para equipos vendidos en cuotas:
  - `CMD:SUSPEND` / `CMD:RESUME`: suspende o rehabilita el equipo sin borrar nada.
  - `CMD:RENEW:<dias>`: modo cuotas; vence tras `<dias>` días de uso si no se renueva.
  - `CMD:FREE`: licencia libre (cuotas pagadas), no vence más.
  - Los cuatro van **firmados** (SipHash-2-4 con clave por equipo en `clave_licencia.h`,
    fuera de Git) y con número de secuencia creciente: no sirven sin la clave ni se
    pueden repetir. Tras un fallo de firma se acepta un intento por segundo.
  - `CMD:LICENSE` (sin firma): `LICENSE:L1:MODE:TERM:STATE:ACTIVE:LEFT:<min>:SEQ:<n>`.
  - Bloqueado (suspendido o vencido): las tarjetas válidas **no abren ni desarman**
    (`ACCESS:...:1:0`), la maestra no entra en modo grabación (`MODE:L1:LOCKED`).
    **El pulsador de salida sigue abriendo siempre.**
  - Sin reloj: se cuenta tiempo encendido, se guarda cada 15 min rotando 8 celdas de
    EEPROM, y cada arranque suma 15 min (cortar la luz no estira la licencia).
  - `BOOT` agrega al final `:LOCK:NONE|SUSPENDED|EXPIRED`.
- **Reset local**: en modo grabación, mantener el pulsador 10 s borra las tarjetas.
- `CMD:SUM` → `SUM:L1:COUNT:<n>:HASH:<8 hex>`: suma de control independiente del
  orden, para detectar diferencias con la plataforma sin listar todo.
- `CMD:LIST:<desde>:<cantidad>`: listado por páginas (máx. 50). `CMD:LIST` sigue igual.
- `tools/firmar_licencia.py`: genera la clave del equipo y firma comandos.

### Cambiado
- **`CMD:RESET` ya no borra**: responde `RESET:L1:DENIED`. El borrado total es solo local.
- `NUM_LECTORAS` se elige solo según la placa (Mega 2, UNO 1); se puede forzar con
  `--build-property "build.extra_flags=-DNUM_LECTORAS=1"` sin editar el archivo.

### Corregido
- `CMD:ENROLL` / `CMD:DELETE` validan el UID (solo dígitos, ≠ 0, < 16777215): antes un
  texto no numérico grababa la tarjeta `0`. Ahora responden `ENROLL:L1:<texto>:INVALID`.

### Compatibilidad
- La EEPROM de tarjetas no cambia (misma firma): las tarjetas cargadas se conservan.
  El bloque de licencia ocupa una zona libre con su propia marca; al actualizar desde
  2.3.x el equipo queda en licencia libre y sin suspender.
- Gateways anteriores: siguen funcionando; para `LICENSE`/`AUTH`/`SUM` y los comandos
  firmados hace falta la versión del gateway que los traduzca.

## [Sin publicar]

### Documentación
- `docs/ARQUITECTURA.md`: división placa de control / gateway y comparación de gateways.
- `docs/PROTOCOLO.md` y `docs/ARQUITECTURA.md`: gateway ESP8266 1.1.0 (`boot`, `stats`,
  `error` con detalle, topic `/info`, comandos `stats`, `stats_reset`, `version`) y cableado.
- `docs/PROTOCOLO.md`: gateway 1.4.0 (telemetría/heartbeat y cola de eventos con `delay_s`).
- `CLAUDE.md`: compilación con `arduino-cli`.

## [2.3.2]

### Agregado
- Filtro anti-ruido en las ISR: se ignora un flanco que llega antes de
  `WIEGAND_MIN_INTERVAL_US` (200 µs) desde el último bit de esa lectora. 0 = desactivado.
- Clasificación de ruido: tramas con menos de `RUIDO_MAX_BITS` (8) bits se cuentan como
  `NOISE` y no se envían como `ERROR` (evita tráfico MQTT cuando conmuta la cerradura).
- `STATS` agrega los campos `FILTERED` y `NOISE`:
  `STATS:L1:OK:120:ERR:2:INVALID:1:FILTERED:35:NOISE:12`.

### Cambiado
- `NUM_LECTORAS` por defecto pasa a `1`.

### Documentación
- README, manual de instalación y uso, protocolo serial y guía de desarrollo.

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
