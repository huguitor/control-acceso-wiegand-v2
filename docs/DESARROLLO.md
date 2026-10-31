# Guía de desarrollo

Notas para seguir trabajando sobre el firmware.
Leer antes [ARQUITECTURA.md](ARQUITECTURA.md): esta placa no hace nada de red.

## Entorno

- **Arduino IDE** con la librería *Yet Another Arduino Wiegand Library* **2.0.0**
  (Paulo Costa — https://github.com/paulo-raca/YetAnotherArduinoWiegandLibrary).
- El sketch incluye `Arduino.h` para que también se pueda editar en VS Code / PlatformIO.
- Placas: Arduino Mega 2560 (`__AVR_ATmega2560__`) o UNO.
- Compilación por línea de comandos (si se instala `arduino-cli`):
  ```bash
  arduino-cli lib install "Yet Another Arduino Wiegand Library@2.0.0"
  arduino-cli compile -b arduino:avr:mega control_acceso_wiegand_v2.ino
  arduino-cli upload  -b arduino:avr:mega -p /dev/ttyACM0 .
  ```

## Reglas de trabajo

1. **Subir `FW_VERSION` en cada cambio que se cargue a un equipo** (semver: parche =
   corrección, menor = función nueva compatible, mayor = cambia el protocolo o la EEPROM).
2. Anotar el cambio en `CHANGELOG.md`.
3. Commit + tag: `git tag vX.Y.Z && git push --tags`.
4. Si cambia el protocolo serial, actualizar `docs/PROTOCOLO.md` **y** el gateway.
5. Nada de `delay()` en el `loop`: todo con `millis()`.
6. Nada de `Serial` ni `EEPROM` dentro de interrupciones o callbacks Wiegand.

## Arquitectura

```
 Interrupciones (FALLING)            loop()  — todo non-blocking
 ───────────────────────            ──────────────────────────────────────
 isrL1_D0/D1 ─► wiegand.receivedBit  atenderLectoras()      ◄─ toma pendL1/L2, errL1/L2
 isrL2_D0/D1 ─► wiegandSalida...         └─ atenderTarjeta()  filtra UID 0 / 0xFFFFFF
        │                                    └─ procesarTarjeta()
        ▼ callbacks (en contexto ISR)              ├─ maestra → alterna modo grabación
 receivedData*      → cardLx, pendLx              ├─ modo grabación → manejarModoGrabacion()
 receivedDataError* → errTipoLx, errBitsLx        └─ modo lectura  → manejarModoLectura()
                                     manejarPulsadorApertura()   debounce 80 ms
                                     manejarSensorMagnetico()    debounce 30 ms
                                     controlarTemporizadorPuerta()
                                     controlarTemporizadorAlarma()
                                     buzzerUpdate()              patrones en PROGMEM
                                     leerSerialEntrante()        → procesarComando()
                                     LED intermitente + timeout si modo grabación
```

### Lectura Wiegand

- Se usa `wiegand.receivedBit()` con interrupciones **FALLING** en cada línea
  (un flanco en D0 = bit 0, en D1 = bit 1). No se usa `setPinState` con `CHANGE`:
  con dos lectoras, el flag de interrupción latcheado del AVR garantiza que no se
  pierdan bits aunque una ISR demore a la otra.
- `wiegand.begin(26, true)`: 26 bits, `true` = decodifica (quita paridad) → 24 bits
  de datos. `bytesANumero()` arma el `uint32_t` (big-endian, máx. 4 bytes).
- `atenderLectoras()` copia las variables `volatile` con interrupciones deshabilitadas
  y llama a `flush()` para descartar tramas incompletas por timeout.

### EEPROM

| | UNO | Mega |
|---|---|---|
| Tarjetas (`uint32_t` c/u) | 0..799 (200) | 0..3999 (1000) |
| Contador (`int`, 2 bytes) | 800 | 4000 |
| Firma (`uint32_t`) | 1020 = `0xABCD0001` | 4092 = `0xABCD0002` |

- `verificarEEPROM()` al arrancar:
  - firma actual → no hace nada;
  - (Mega) firma vieja `0xABCD0001` en 1020 → migra: copia el contador de 800 a 4000
    y graba la firma nueva (las tarjetas ya están en el mismo lugar);
  - otra cosa → lista vacía + firma.
- **Borrar** una tarjeta mueve la última al hueco (máx. 4 bytes escritos). El orden no se conserva.
- **Reset** solo pone el contador en 0 (no borra celdas: escribir 4 KB lleva ~13 s).
- Si se cambia el layout: **cambiar la firma** y agregar la migración correspondiente.
- La EEPROM aguanta ~100.000 escrituras por celda; el contador (dirección fija) es la
  celda más gastada (una escritura por alta/baja). No es problema con uso normal.

### Tarjetas

- **Maestras** (`NUMERO_MAESTRO_*`): constantes en el código; alternan el modo grabación.
- **Fijas** (`TARJETAS_FIJAS[]` en PROGMEM): siempre autorizadas, no se pueden borrar,
  se listan primero en `CMD:LIST`.
- **EEPROM**: altas por modo grabación o `CMD:ENROLL`.
- La búsqueda es lineal; con 1000 tarjetas en el Mega lleva unos pocos ms (aceptable).

### Estadísticas

`StatsLectora` (ok / errores / inválidos) por lectora, **solo en RAM** (se pierden al
reiniciar). La `struct` está declarada antes de cualquier función a propósito: el IDE de
Arduino genera prototipos automáticos y fallaría si el tipo se declara después.

## Cómo agregar cosas

**Un comando serial nuevo:** agregar una rama en `procesarComando()`, una función
`enviarEvento...()` para la respuesta, documentarlo en `PROTOCOLO.md` y agregar el
mapeo en el gateway.

**Un evento nuevo:** función `enviarEvento...()` que imprima con `F("...")`
(strings en flash para ahorrar RAM) y una sola línea terminada en `println`.

**Un patrón de buzzer:** array `BuzzerStep` en PROGMEM terminado en `{0,0}`;
cada paso es `{ms_encendido, ms_apagado}`. Reproducir con `buzzerPlay(PAT_...)`.

**Un pin nuevo:** constante en la sección PINES, `pinMode` y estado seguro en `setup()`.
Pines con interrupción en el Mega: 2, 3, 18, 19, 20, 21 (20/21 son I²C).

## Problemas conocidos

1. **Las teclas de debug `l` y `b` no funcionan:** con `DEBUG_SERIAL true`,
   `leerSerialEntrante()` consume todo lo que llega por serial antes que el bloque de
   debug del `loop`. Opción: convertirlas en comandos (`CMD:DUMP`).
2. **Tarjeta maestra de test** (`NUMERO_MAESTRO_3 = 6486910`) está activa: quitarla en producción.
3. **`CMD:ENROLL:` con un UID no numérico** graba `0` (por `atol`). Validar el UID
   (≠ 0, ≠ 0xFFFFFF, solo dígitos) antes de grabar.
4. Las maestras pueden activar el modo grabación desde **cualquiera** de las lectoras,
   incluida L2 (lado de salida/interior).
5. El **pulsador no desarma la alarma** (la tarjeta sí). Verificar que sea el comportamiento deseado.
6. Pines 6 y 10 están en `INPUT` sin pull-up interna: dependen de resistencias externas.
   Se podría usar `INPUT_PULLUP` si el hardware lo permite.
7. `leerSerialEntrante()` procesa a lo sumo **una línea por vuelta** del `loop` (está bien,
   pero un `CMD:LIST` largo con 1000 tarjetas bloquea el loop mientras imprime a 9600 bps
   — ~10 s; durante ese tiempo no se atienden las lectoras).
8. El LED en modo grabación y el LED de apertura comparten el pin 7; si se abre la puerta
   durante el modo grabación, el parpadeo y el temporizador se pisan (solo visual).

## Pendientes / ideas

- [ ] Gateway: manejar `BOOT` y `STATS`, y agregar comandos MQTT `stats`, `stats_reset` y `version`.
- [ ] Gateway ESP8266: subirlo a su propio repo de GitHub (separar claves MQTT a un `config.h` ignorado).
- [ ] Gateway Ethernet: agregar recepción de comandos (`/cmd` → `CMD:...`) para igualarlo al ESP8266.
- [ ] Validar UIDs en `CMD:ENROLL` (problema 3).
- [ ] Hacer el listado non-blocking o subir el baud rate (problema 7; hay que cambiarlo en ambos lados).
- [ ] Mover las tarjetas maestras a EEPROM o hacerlas configurables por MQTT.
- [ ] Watchdog (`avr/wdt.h`) para recuperarse de cuelgues.
- [ ] Alarma de puerta abierta demasiado tiempo / forzada (DOOR:OPEN sin ACCESS/BUTTON previo).
- [ ] Relé de cerradura separado por lectora si se necesitan dos puertas.
