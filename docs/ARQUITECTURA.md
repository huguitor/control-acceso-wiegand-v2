# Arquitectura del sistema

## Idea principal: cada placa hace lo que mejor sabe hacer

El sistema está dividido **a propósito** en dos módulos independientes:

```
                                      ┌─► Gateway ESP8266 (WiFi) ──────┐
 Lectoras ─► PLACA DE CONTROL ─serial─┤                                ├─► Broker MQTT
 Wiegand     (este repo)              └─► Gateway UNO + Ethernet ──────┘   (192.168.0.60)
               │                            (cableado)
               ├─ cerradura (relé)
               ├─ desarme alarma (relé)
               ├─ pulsador, sensor de puerta
               └─ buzzer, LED, EEPROM de tarjetas
```

| Módulo | Responsabilidad | NO hace |
|---|---|---|
| **Placa de control** (Arduino UNO/Mega, este repo) | Leer tarjetas, decidir el acceso, manejar cerradura, alarma, pulsador, sensor, buzzer y la lista de tarjetas en EEPROM | Nada de red: ni WiFi, ni Ethernet, ni MQTT |
| **Gateway** (ESP8266 WiFi o UNO + Ethernet) | Conectarse a la red y al broker, convertir los mensajes serial a JSON/MQTT, reenviar comandos remotos, reconexiones, LWT online/offline | No decide accesos ni guarda tarjetas |

### Por qué así

- **La puerta funciona sin red.** Si se cae el WiFi, el cable o el broker, la placa de control
  sigue abriendo con las tarjetas autorizadas. Solo se pierde la publicación de eventos.
- **No se sobrecarga el hardware.** La placa de control dedica su tiempo a las interrupciones
  Wiegand y a los temporizadores. El manejo de red (pilas TCP/IP, reconexiones,
  buffers) queda en otra placa y no le quita tiempo ni memoria.
- **El gateway es intercambiable.** Según la instalación se usa WiFi o cable sin tocar
  el firmware de control.
- **Se desarrollan y prueban por separado.** La placa de control se prueba con el Monitor
  Serie escribiendo comandos `CMD:...`; el gateway se prueba simulando mensajes.

## El contrato entre los dos: el protocolo serial

Lo único que comparten es el **protocolo serial** de texto a 9600 baudios, documentado en
[PROTOCOLO.md](PROTOCOLO.md).

Reglas:

1. Cualquier gateway que respete el protocolo puede usarse con cualquier placa de control.
2. Si se agrega o cambia un mensaje, se actualiza `PROTOCOLO.md` **y todos los gateways**.
3. Preferir cambios compatibles: agregar campos **al final** o mensajes nuevos, en vez de
   cambiar los existentes, para que un gateway viejo no se rompa.
4. La placa de control no espera respuestas del gateway: si nadie escucha, sigue funcionando.

## Gateways disponibles

| | ESP8266 WiFi | UNO + Ethernet |
|---|---|---|
| Código | `~/Arduino/esp8266_gateway_wiegand_mqtt` | https://github.com/huguitor/uno_mqtt_json_serial |
| Repo | Git local (todavía no en GitHub) | Sí (público) |
| Placa | NodeMCU / ESP-12F | Arduino UNO + shield Ethernet (W5100) |
| Serial con la placa de control | SoftwareSerial D5 (RX) / D6 (TX) | Serial |
| Configuración de red | Portal web en modo AP (llave AP/ST en D7), credenciales en LittleFS | MAC e IP del broker en el código |
| Publica eventos | ✅ | ✅ |
| Recibe comandos (`enroll`, `delete`, `list`, `reset`) | ✅ | ❌ (solo publica) |
| `BOOT`, `STATS` y comandos `stats`, `stats_reset`, `version` | ✅ (1.1.0) | ❌ |

Topics MQTT (ambos): `iot/devices/<DEVICE_ID>/event`, `/cmd` y `/info` (solo ESP8266) y `/status`.
`DEVICE_ID` por defecto: `lector-acceso-01`.

> Estado relevado el 2026-11-03. Si cambia algún gateway, actualizar esta tabla.

## Cableado entre módulos

| Placa de control | Gateway |
|---|---|
| TX (pin 1) | RX del gateway — ESP8266: **D5** con **divisor 5 V → 3,3 V** (1 kΩ serie + 2 kΩ a GND) |
| RX (pin 0) | TX del gateway — ESP8266: **D6**, directo |
| GND | GND (**común obligatorio**) |

Detalle, dibujo del divisor y alimentación en el README del gateway.

Los pines 0/1 de la placa de control son los del USB:

- Para **cargar firmware**, desconectar el TX del gateway → pin 0.
- El **Monitor Serie** puede quedar abierto para *mirar* lo que manda la placa, pero no
  para escribir comandos (el USB y el gateway se pisarían en el pin 0). Los comandos van por MQTT.
- No se puede usar `SoftwareSerial` en la placa de control para liberar el USB: bloquea
  las interrupciones ~1 ms por byte a 9600 y se pierden bits Wiegand.
- En el **Mega** sí se puede: pasar el gateway a `Serial2` (TX2 = 16, RX2 = 17) y dejar el
  USB libre (`Serial1`, pines 18/19, ya está ocupado por la lectora L2). Ver pendientes.
