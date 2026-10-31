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
| Repo | Todavía no | Sí (público) |
| Placa | NodeMCU / ESP-12F | Arduino UNO + shield Ethernet (W5100) |
| Serial con la placa de control | SoftwareSerial D5 (RX) / D6 (TX) | Serial |
| Configuración de red | Portal web en modo AP (llave AP/ST en D7), credenciales en LittleFS | MAC e IP del broker en el código |
| Publica eventos | ✅ | ✅ |
| Recibe comandos (`enroll`, `delete`, `list`, `reset`) | ✅ | ❌ (solo publica) |
| `BOOT` y `STATS` | ❌ | ❌ |

Topics MQTT (ambos): `iot/devices/<DEVICE_ID>/event`, `/cmd` (solo ESP8266) y `/status`.
`DEVICE_ID` por defecto: `lector-acceso-01`.

> Estado relevado el 2026-10-31. Si cambia algún gateway, actualizar esta tabla.

## Cableado entre módulos

| Placa de control | Gateway |
|---|---|
| TX (pin 1) | RX del gateway — **divisor 5 V → 3,3 V si es ESP8266** |
| RX (pin 0) | TX del gateway |
| GND | GND (**común obligatorio**) |

Los pines 0/1 de la placa de control son los del USB: desconectar el gateway para cargar
firmware o usar el Monitor Serie.
