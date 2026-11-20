# Protocolo serial Arduino ↔ Gateway

- 9600 baudios, 8N1, líneas en texto terminadas en `\n` (se ignora `\r`).
- Campos separados por `:`.
- `<L>` = lectora que originó el evento (`L1` o `L2`). Los eventos que no son de una
  lectora en particular usan `LECTOR_ID` (por defecto `L1`).
- Con `DEBUG_SERIAL true` se mezclan mensajes de texto: **no usar con el gateway**.

## Eventos (Arduino → Gateway)

| Mensaje | Cuándo | Ejemplo |
|---|---|---|
| `BOOT:<id>:FW:<v>:WIEGAND:<v>:READERS:<n>:CAP:<n>:LOCK:<b>` | Al arrancar y con `CMD:VERSION` (`LOCK` desde 2.4.0: `NONE`/`SUSPENDED`/`EXPIRED`) | `BOOT:L1:FW:2.4.0:WIEGAND:2.0.0:READERS:2:CAP:1000:LOCK:NONE` |
| `ACCESS:<L>:<uid>:<autorizado>:<abre>` | Tarjeta leída en modo lectura (`1:0` = válida pero equipo bloqueado) | `ACCESS:L2:5033541:1:1` |
| `BUTTON:<id>:1:<abre>` | Pulsador de salida | `BUTTON:L1:1:1` |
| `DOOR:<id>:OPEN` / `CLOSED` | Cambio del sensor magnético y al arrancar | `DOOR:L1:OPEN` |
| `MODE:<id>:PROGRAM` | Entra en modo grabación | |
| `MODE:<id>:READ:MANUAL` | Sale con tarjeta maestra | |
| `MODE:<id>:READ:TIMEOUT` | Sale por tiempo | |
| `MODE:<id>:LOCKED` | Maestra pasada con el equipo bloqueado (no entra a grabación) | |
| `ENROLL:<id>:<uid>:OK` / `EXISTS` / `FULL` / `INVALID` | Alta (local o por comando) | `ENROLL:L1:5033541:OK` |
| `DELETE:<id>:<uid>:OK` / `NOT_FOUND` / `FIXED` / `INVALID` | Respuesta a `CMD:DELETE` | |
| `LIST:<id>:COUNT:<n>` | Inicio del listado (fijas + EEPROM) | `LIST:L1:COUNT:12` |
| `LIST:<id>:<uid>` | Una por tarjeta: primero fijas, después EEPROM | `LIST:L1:5673301` |
| `LIST:<id>:END` | Fin del listado | |
| `LIST:<id>:BAD` | `CMD:LIST:<desde>:<cant>` mal formado | |
| `RESET:<id>:OK` | Reset local hecho (modo grabación + pulsador 10 s) | |
| `RESET:<id>:DENIED` | Respuesta a `CMD:RESET` desde 2.4.0 (ya no borra) | |
| `SUM:<id>:COUNT:<n>:HASH:<h>` | Respuesta a `CMD:SUM` | `SUM:L1:COUNT:650:HASH:1A2B3C4D` |
| `LICENSE:<id>:MODE:<m>:STATE:<e>:LEFT:<min>:SEQ:<n>` | `CMD:LICENSE`, comando firmado aceptado, y cuando cambia el bloqueo | `LICENSE:L1:MODE:TERM:STATE:ACTIVE:LEFT:43185:SEQ:7` |
| `AUTH:<id>:FAIL` / `OLD_SEQ` / `BAD` / `WAIT` | Comando firmado rechazado | |
| `ERROR:<L>:WIEGAND:<tipo>:<bits>` | Trama Wiegand inválida | `ERROR:L1:WIEGAND:DecodeFailed:26` |
| `ERROR:<L>:WIEGAND:INVALID_UID:<uid>` | UID 0 o 16777215 filtrado | `ERROR:L2:WIEGAND:INVALID_UID:0` |
| `STATS:<L>:OK:<n>:ERR:<n>:INVALID:<n>:FILTERED:<n>:NOISE:<n>` | Respuesta a `CMD:STATS` (una línea por lectora) | `STATS:L1:OK:120:ERR:2:INVALID:1:FILTERED:35:NOISE:12` |
| `STATS:RESET:OK` | Respuesta a `CMD:STATS:RESET` | |

Notas:

- En `ACCESS` hoy `autorizado` y `abre` siempre valen lo mismo; están separados para
  permitir a futuro "autorizado pero sin apertura" (horarios, antipassback, etc.).
- Una tarjeta **maestra** no genera `ACCESS`: solo `MODE:...`.
- Una tarjeta pasada **en modo grabación** no genera `ACCESS`: solo `ENROLL:...`.
- `<tipo>` de error lo da `Wiegand::DataErrorStr()` de la librería
  (ej. `SizeTooBig`, `SizeUnexpected`, `DecodeFailed`, `Communication`).
- Las tramas con menos de `RUIDO_MAX_BITS` (8) bits **no generan `ERROR`**: se cuentan
  como `NOISE` en `STATS` (típicamente ruido de la cerradura o un relé al conmutar).
- Campos de `STATS` (contadores desde el arranque o el último `CMD:STATS:RESET`):
  `OK` tramas válidas · `ERR` tramas fallidas de 8 bits o más (problema real) ·
  `INVALID` UID 0 o 16777215 · `FILTERED` flancos descartados por el filtro de
  `WIEGAND_MIN_INTERVAL_US` (200 µs; tope 65535) · `NOISE` tramas cortas descartadas.

## Comandos (Gateway → Arduino)

| Comando | Respuesta |
|---|---|
| `CMD:ENROLL:<uid>` | `ENROLL:...:OK` / `EXISTS` / `FULL` |
| `CMD:DELETE:<uid>` | `DELETE:...:OK` / `NOT_FOUND` / `FIXED` |
| `CMD:LIST` | `LIST:...:COUNT`, N × `LIST:...:<uid>`, `LIST:...:END` |
| `CMD:LIST:<desde>:<cant>` | Igual, solo posiciones `desde..desde+cant-1` (`cant` ≤ 50); `COUNT` es siempre el total |
| `CMD:SUM` | `SUM:...:COUNT:<n>:HASH:<h>` |
| `CMD:LICENSE` | `LICENSE:...` |
| `CMD:SUSPEND:<seq>:<mac>` | `LICENSE:...` o `AUTH:...` (firmado) |
| `CMD:RESUME:<seq>:<mac>` | ídem |
| `CMD:RENEW:<dias>:<seq>:<mac>` | ídem — `dias` ≤ 3650 |
| `CMD:FREE:<seq>:<mac>` | ídem |
| `CMD:RESET` | `RESET:...:DENIED` desde 2.4.0 (antes borraba todas las tarjetas) |
| `CMD:STATS` | `STATS:L1:...` (+ `STATS:L2:...` si hay 2 lectoras) |
| `CMD:STATS:RESET` | `STATS:RESET:OK` |
| `CMD:VERSION` | `BOOT:...` |

Máximo 79 caracteres por línea; los comandos desconocidos se ignoran sin respuesta.

### Detalles (FW 2.4.0)

- **UID en comandos**: solo dígitos, hasta 8 cifras, distinto de 0 y menor que 16777215.
- **HASH de `SUM`**: XOR, sobre todas las tarjetas (fijas + EEPROM), de
  `mezclar32(uid)` = finalizador de MurmurHash3 (`h^=h>>16; h*=0x85EBCA6B; h^=h>>13;
  h*=0xC2B2AE35; h^=h>>16`), en 8 dígitos hex mayúsculas. No depende del orden.
- **Comandos firmados**: `<mac>` = SipHash-2-4 con la clave de 16 bytes del equipo
  (`clave_licencia.h`) sobre el texto entre `CMD:` y `:<mac>` (ej. `RENEW:30:7`), en 16
  dígitos hex. `<seq>` debe ser mayor que el último aceptado (`SEQ` de `LICENSE`).
  `tools/firmar_licencia.py firmar clave_licencia.h RENEW:30:7` arma la línea.
- **Licencia**: `MODE` `FREE` (no vence) o `TERM` (vence); `STATE` `ACTIVE`,
  `SUSPENDED` o `EXPIRED`; `LEFT` = minutos de uso restantes (0 en `FREE`).
  `RENEW:<dias>` fija el vencimiento en *ahora + días* de uso (no se acumula).
  Suspensión y vencimiento son independientes: `RESUME` no renueva, `RENEW` no reanuda.
- **Bloqueado** = suspendido o vencido. Las tarjetas no abren, la maestra no graba;
  `ENROLL`/`DELETE`/`LIST` remotos siguen funcionando; el pulsador abre siempre.

## Mapeo MQTT (gateway `esp8266_gateway_wiegand_mqtt` 1.5.0)

- Eventos: `iot/devices/<DEVICE_ID>/event` (JSON).
- Comandos: `iot/devices/<DEVICE_ID>/cmd` (JSON con `cmd` y `card_uid`).
- Estado: `iot/devices/<DEVICE_ID>/status` (`online`/`offline`, retenido, LWT).
- Info: `iot/devices/<DEVICE_ID>/info` (último `boot`, retenido). El gateway manda
  `CMD:VERSION` cada vez que conecta a MQTT para mantenerlo al día.
- Telemetría: `iot/devices/<DEVICE_ID>/telemetry` cada 60 s (heartbeat):
  `{"ip":"192.168.0.16","rssi":-61,"uptime":3600,"heap":42000,"gateway_fw":"1.4.0","uno_rx_age":42,"queued":0,"dropped":0}`.
  `uno_rx_age` = segundos desde la última línea del UNO (-1 = ninguna).
- Sin conexión, el gateway encola hasta 40 eventos y los reenvía al reconectar con
  `"delay_s"` (segundos de espera): hora real = hora de recepción − `delay_s`.
  No se encolan `list`, `stats`, `stats_reset` ni `boot`.
- Heartbeat del worker: `iot/platform/worker/heartbeat` (lo publica el worker cada ~10 s,
  sin retener). El gateway solo vacía la cola con un heartbeat de hace < 30 s.

| JSON en `/cmd` | Comando serial |
|---|---|
| `{"cmd":"enroll","card_uid":"5033541"}` | `CMD:ENROLL:5033541` |
| `{"cmd":"delete","card_uid":"5033541"}` | `CMD:DELETE:5033541` |
| `{"cmd":"list"}` | `CMD:LIST` |
| `{"cmd":"reset"}` | `CMD:RESET` |
| `{"cmd":"stats"}` | `CMD:STATS` |
| `{"cmd":"stats_reset"}` | `CMD:STATS:RESET` |
| `{"cmd":"version"}` | `CMD:VERSION` |

Ejemplos de eventos publicados:

```json
{"event":"access","reader":"L1","card_uid":"5033541","authorized":true,"door_opened":true}
{"event":"boot","reader":"L1","gateway_fw":"1.1.0","fw":"2.3.2","wiegand":"2.0.0","readers":1,"cap":200}
{"event":"stats","reader":"L1","ok":120,"err":2,"invalid":1,"filtered":35,"noise":12}
{"event":"stats_reset","result":"OK"}
{"event":"error","reader":"L1","error":"WIEGAND","type":"DecodeFailed","bits":26}
{"event":"error","reader":"L2","error":"WIEGAND","type":"INVALID_UID","card_uid":"0"}
```

- `boot` y `stats` copian los pares `CLAVE:VALOR` del mensaje serial (clave en minúsculas,
  números sin comillas). Un campo nuevo agregado **al final** llega a MQTT sin cambiar el gateway.
- `boot` se publica al arrancar la placa **y** como respuesta a `version` (no implica reinicio).

> Gateway 1.5.0 (2026-11-03): traduce todos los mensajes de esta página.
