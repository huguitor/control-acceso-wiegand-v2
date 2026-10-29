# Protocolo serial Arduino ↔ Gateway

- 9600 baudios, 8N1, líneas en texto terminadas en `\n` (se ignora `\r`).
- Campos separados por `:`.
- `<L>` = lectora que originó el evento (`L1` o `L2`). Los eventos que no son de una
  lectora en particular usan `LECTOR_ID` (por defecto `L1`).
- Con `DEBUG_SERIAL true` se mezclan mensajes de texto: **no usar con el gateway**.

## Eventos (Arduino → Gateway)

| Mensaje | Cuándo | Ejemplo |
|---|---|---|
| `BOOT:<id>:FW:<v>:WIEGAND:<v>:READERS:<n>:CAP:<n>` | Al arrancar y con `CMD:VERSION` | `BOOT:L1:FW:2.3.0:WIEGAND:2.0.0:READERS:2:CAP:1000` |
| `ACCESS:<L>:<uid>:<autorizado>:<abre>` | Tarjeta leída en modo lectura | `ACCESS:L2:5033541:1:1` |
| `BUTTON:<id>:1:<abre>` | Pulsador de salida | `BUTTON:L1:1:1` |
| `DOOR:<id>:OPEN` / `CLOSED` | Cambio del sensor magnético y al arrancar | `DOOR:L1:OPEN` |
| `MODE:<id>:PROGRAM` | Entra en modo grabación | |
| `MODE:<id>:READ:MANUAL` | Sale con tarjeta maestra | |
| `MODE:<id>:READ:TIMEOUT` | Sale por tiempo | |
| `ENROLL:<id>:<uid>:OK` / `EXISTS` / `FULL` | Alta (local o por comando) | `ENROLL:L1:5033541:OK` |
| `DELETE:<id>:<uid>:OK` / `NOT_FOUND` / `FIXED` | Respuesta a `CMD:DELETE` | |
| `LIST:<id>:COUNT:<n>` | Inicio del listado (fijas + EEPROM) | `LIST:L1:COUNT:12` |
| `LIST:<id>:<uid>` | Una por tarjeta: primero fijas, después EEPROM | `LIST:L1:5673301` |
| `LIST:<id>:END` | Fin del listado | |
| `RESET:<id>:OK` | Respuesta a `CMD:RESET` | |
| `ERROR:<L>:WIEGAND:<tipo>:<bits>` | Trama Wiegand inválida | `ERROR:L1:WIEGAND:DecodeFailed:26` |
| `ERROR:<L>:WIEGAND:INVALID_UID:<uid>` | UID 0 o 16777215 filtrado | `ERROR:L2:WIEGAND:INVALID_UID:0` |
| `STATS:<L>:OK:<n>:ERR:<n>:INVALID:<n>` | Respuesta a `CMD:STATS` (una línea por lectora) | `STATS:L1:OK:120:ERR:2:INVALID:1` |
| `STATS:RESET:OK` | Respuesta a `CMD:STATS:RESET` | |

Notas:

- En `ACCESS` hoy `autorizado` y `abre` siempre valen lo mismo; están separados para
  permitir a futuro "autorizado pero sin apertura" (horarios, antipassback, etc.).
- Una tarjeta **maestra** no genera `ACCESS`: solo `MODE:...`.
- Una tarjeta pasada **en modo grabación** no genera `ACCESS`: solo `ENROLL:...`.
- `<tipo>` de error lo da `Wiegand::DataErrorStr()` de la librería
  (ej. `SizeTooBig`, `SizeUnexpected`, `DecodeFailed`, `Communication`).

## Comandos (Gateway → Arduino)

| Comando | Respuesta |
|---|---|
| `CMD:ENROLL:<uid>` | `ENROLL:...:OK` / `EXISTS` / `FULL` |
| `CMD:DELETE:<uid>` | `DELETE:...:OK` / `NOT_FOUND` / `FIXED` |
| `CMD:LIST` | `LIST:...:COUNT`, N × `LIST:...:<uid>`, `LIST:...:END` |
| `CMD:RESET` | `RESET:...:OK` — **borra todas las tarjetas de la EEPROM** |
| `CMD:STATS` | `STATS:L1:...` (+ `STATS:L2:...` si hay 2 lectoras) |
| `CMD:STATS:RESET` | `STATS:RESET:OK` |
| `CMD:VERSION` | `BOOT:...` |

Máximo 79 caracteres por línea; los comandos desconocidos se ignoran sin respuesta.

## Mapeo MQTT (gateway `esp8266_gateway_wiegand_mqtt`)

- Eventos: `iot/devices/<DEVICE_ID>/event` (JSON).
- Comandos: `iot/devices/<DEVICE_ID>/cmd` (JSON con `cmd` y `card_uid`).
- Estado: `iot/devices/<DEVICE_ID>/status`.

| JSON en `/cmd` | Comando serial |
|---|---|
| `{"cmd":"enroll","card_uid":"5033541"}` | `CMD:ENROLL:5033541` |
| `{"cmd":"delete","card_uid":"5033541"}` | `CMD:DELETE:5033541` |
| `{"cmd":"list"}` | `CMD:LIST` |
| `{"cmd":"reset"}` | `CMD:RESET` |

Ejemplo de evento publicado:

```json
{"event":"access","reader":"L1","card_uid":"5033541","authorized":true,"door_opened":true}
```

> El gateway (revisado el 2026-10-29) traduce `ACCESS`, `BUTTON`, `DOOR`, `MODE`, `ENROLL`,
> `DELETE`, `LIST`, `RESET` y `ERROR`. **Todavía no maneja `BOOT` ni `STATS`, y no tiene
> comandos MQTT para `stats` ni `version`.** Ver pendientes en [DESARROLLO.md](DESARROLLO.md).
