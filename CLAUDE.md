# CLAUDE.md

Firmware Arduino (AVR, UNO/Mega) de control de acceso Wiegand. Todo el código está en
`control_acceso_wiegand_v2.ino`. Documentación en español en `docs/`.

- Leer `docs/DESARROLLO.md` (arquitectura, EEPROM, problemas conocidos) antes de cambiar código.
- El protocolo serial (`docs/PROTOCOLO.md`) lo consume el gateway
  `~/Arduino/esp8266_gateway_wiegand_mqtt`: cualquier cambio de mensajes debe ser compatible
  o actualizarse en ambos lados.
- Subir `FW_VERSION` y anotar en `CHANGELOG.md` en cada cambio de firmware.
- Sin `delay()` en el loop, sin `Serial`/`EEPROM` en ISRs/callbacks, strings con `F()`.
- Cambios de layout de EEPROM: nueva firma + migración en `verificarEEPROM()`.
- No hay `arduino-cli` instalado; la compilación la hace el usuario en el Arduino IDE.
- Comentarios y mensajes en español, igual que el código existente.
