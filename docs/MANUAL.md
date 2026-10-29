# Manual de instalación y uso — Control de acceso Wiegand v2

FW 2.3.0

---

## 1. Qué hace el equipo

- Cuando se pasa una **tarjeta autorizada**, abre la cerradura durante **3 s**, da
  un pulso de **3 s** al relé de desarme de alarma y emite un pitido corto.
- Cuando se pasa una **tarjeta no autorizada**, emite 4 pitidos y no abre.
- El **pulsador de salida** abre la cerradura 3 s (no desarma la alarma).
- El **sensor magnético** informa si la puerta está abierta o cerrada.
- Todo se informa por serial al gateway, que lo publica por MQTT.

## 2. Materiales

| Componente | Notas |
|---|---|
| Arduino **Mega 2560** (2 lectoras) o **UNO** (1 lectora) | |
| 1 o 2 lectoras Wiegand **26 bits** | Configuradas en salida W26 |
| Módulo relé para la cerradura | Activo en LOW por defecto |
| Módulo relé para desarme de alarma | Activo en LOW por defecto |
| Buzzer activo | Suena con LOW |
| LED indicador | Encendido con LOW (ver §4) |
| Pulsador de salida (NA) | Necesita **pull-up externa** (10 kΩ a 5 V) |
| Sensor magnético de puerta | Necesita **pull-up externa** si es de contacto seco |
| Gateway ESP8266 | Opcional, para MQTT |
| Fuente común | **GND común** entre Arduino, lectoras y relés |

## 3. Conexionado

### Lectoras

| Señal | L1 (entrada) | L2 (salida, solo Mega) |
|---|---|---|
| DATA0 (verde) | pin **2** | pin **18** |
| DATA1 (blanco) | pin **3** | pin **19** |
| GND | GND | GND |
| +12 V | Fuente de la lectora | Fuente de la lectora |

> Los colores de cable son los habituales, pero verificalos en la hoja de datos de tu lectora.
> Si las líneas quedan flotando con la lectora desconectada, se puede poner
> `WIEGAND_PULLUP true` en el código.

### Entradas y salidas

| Pin | Función | Lógica |
|---|---|---|
| 6 | Pulsador de salida | Al presionar va a **LOW** (pull-up externa a 5 V) |
| 7 | LED | HIGH = apagado en reposo; LOW durante la apertura |
| 8 | Buzzer | **LOW = suena** |
| 9 | Relé cerradura | LOW = activo (`RELAY_INVERTIDO false`) |
| 10 | Sensor magnético | **LOW = cerrada**, HIGH = abierta |
| 11 | Relé desarme alarma | LOW = activo (`RELE_ALARMA_INVERTIDO false`) |

### Gateway ESP8266

| Arduino | ESP8266 |
|---|---|
| TX (pin 1) | D5 / GPIO14 (RX) — **usar divisor de tensión 5 V → 3,3 V** |
| RX (pin 0) | D6 / GPIO12 (TX) |
| GND | GND |

Velocidad: **9600 baudios**.

> ⚠️ Los pines 0/1 son los mismos del USB. **Desconectá el gateway para cargar
> el firmware** y para usar el Monitor Serie.

## 4. Configuración antes de cargar

Todo está al principio de `control_acceso_wiegand_v2.ino`:

| Parámetro | Qué cambiar |
|---|---|
| `NUM_LECTORAS` | `1` o `2`. Con 2 la placa tiene que ser Mega (si no, no compila). |
| `LECTOR_ID` | Identificador en los mensajes (por defecto `"L1"`). |
| `DEBUG_SERIAL` | `false` en producción. `true` solo para pruebas **sin** gateway. |
| `RELAY_INVERTIDO` / `RELE_ALARMA_INVERTIDO` | `true` si el relé se activa con HIGH. |
| `WIEGAND_PULLUP` | `true` para activar las pull-ups internas en las líneas Wiegand. |
| `PUERTA_CERRADA` / `PUERTA_ABIERTA` | Invertir si el sensor trabaja al revés. |
| `TARJETAS_FIJAS[]` | Tarjetas que siempre abren y no se pueden borrar. |
| `NUMERO_MAESTRO_1..3` | Tarjetas maestras. **Quitar la de test (`NUMERO_MAESTRO_3`) en producción.** |
| `duracionApertura` | Tiempo de cerradura abierta (ms, por defecto 3000). |
| `duracionPulsoAlarma` | Pulso de desarme (ms, por defecto 3000). |
| `timeoutGrabacion` | Tiempo máximo en modo grabación (ms, por defecto 120000). |

### Número de tarjeta

El equipo usa el número decimal de 24 bits de la trama W26:
`número = código_de_instalación × 65536 + número_de_tarjeta`.
Ejemplo: `5673301` = instalación 86, tarjeta 37205.
Si la tarjeta tiene impreso "086,37205", eso equivale a 5673301.

## 5. Carga del firmware

1. Arduino IDE → *Herramientas → Administrar bibliotecas* → instalar
   **"Yet Another Arduino Wiegand Library"** versión **2.0.0**.
2. Abrir `control_acceso_wiegand_v2.ino`.
3. *Herramientas → Placa*: **Arduino Mega or Mega 2560** (o **Arduino Uno**).
4. Desconectar el gateway de los pines 0/1.
5. Subir el sketch.
6. Abrir el Monitor Serie a 9600: debe aparecer
   `BOOT:L1:FW:2.3.0:WIEGAND:2.0.0:READERS:2:CAP:1000` y `DOOR:L1:CLOSED`.
7. Reconectar el gateway.

> La primera vez en una placa nueva, la EEPROM se inicializa vacía (solo quedan
> las tarjetas fijas). Si la placa venía con la versión de 200 tarjetas, el Mega
> migra los datos automáticamente sin perder tarjetas.

## 6. Operación

### Señales sonoras

| Sonido | Significado |
|---|---|
| 1 pitido corto (0,2 s) | Acceso concedido o pulsador |
| 1 pitido largo (1 s) | Tarjeta grabada OK (modo grabación) |
| 4 pitidos | Acceso denegado, o tarjeta ya existente en modo grabación |
| 3 pitidos lentos | Memoria llena |

### Dar de alta tarjetas en el equipo (modo grabación)

1. Pasar una **tarjeta maestra** por cualquiera de las lectoras → el LED empieza a parpadear.
2. Pasar las tarjetas nuevas una por una (pitido largo = grabada).
3. Para salir, pasar de nuevo la tarjeta maestra. También sale solo si pasan
   **2 minutos sin grabar** ninguna tarjeta (el tiempo se reinicia con cada alta).

### Alta, baja y listado remoto (MQTT)

Topic de comandos: `iot/devices/<DEVICE_ID>/cmd`

```json
{"cmd":"enroll","card_uid":"5033541"}
{"cmd":"delete","card_uid":"5033541"}
{"cmd":"list"}
{"cmd":"reset"}
```

Los eventos llegan por `iot/devices/<DEVICE_ID>/event`.

> ⚠️ `reset` **borra todas las tarjetas grabadas** (no reinicia el equipo).
> Las tarjetas fijas y las maestras no se borran.

### Capacidad

| Placa | Tarjetas grabables | + fijas |
|---|---|---|
| UNO | 200 | las de `TARJETAS_FIJAS` |
| Mega | 1000 | las de `TARJETAS_FIJAS` |

## 7. Solución de problemas

| Síntoma | Causa probable / solución |
|---|---|
| No compila: "NUM_LECTORAS = 2 requiere Arduino Mega" | Elegir placa Mega o poner `NUM_LECTORAS 1`. |
| No compila: falta `Wiegand.h` | Instalar la librería (§5). |
| Error al subir el sketch | El gateway está conectado a los pines 0/1: desconectarlo. |
| Llegan `ERROR:L1:WIEGAND:...` | Ruido o cableado: revisar GND común, cable mallado, distancia. Probar `WIEGAND_PULLUP true`. |
| Llegan `ERROR:...:INVALID_UID:0` o `:16777215` | Línea D0 o D1 pegada a GND/5 V, o lectora desconectada. |
| `SizeUnexpected` con 34 bits u otro número | La lectora no está en W26: configurarla en 26 bits. |
| La puerta abre al revés (queda abierta en reposo) | Cambiar `RELAY_INVERTIDO`. |
| El estado de puerta está invertido | Intercambiar `PUERTA_CERRADA` / `PUERTA_ABIERTA`. |
| El pulsador abre solo / dispara al azar | Falta la pull-up externa en el pin 6. |
| El gateway muestra texto raro | `DEBUG_SERIAL` quedó en `true`: ponerlo en `false`. |
| `ENROLL:...:FULL` | Memoria llena: borrar tarjetas que no se usen. |
| `DELETE:...:FIXED` | Es una tarjeta fija: hay que quitarla del código y recargar. |

### Consultas de diagnóstico (Monitor Serie, sin gateway)

Escribir y enviar con salto de línea:

- `CMD:VERSION` → versión del firmware.
- `CMD:LIST` → lista de tarjetas.
- `CMD:STATS` → lecturas OK, errores e inválidas por lectora (desde el último arranque).
- `CMD:STATS:RESET` → pone las estadísticas a cero.
