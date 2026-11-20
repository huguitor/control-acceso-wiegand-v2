#pragma once
// ============================================================
// CLAVE DE LICENCIA DEL EQUIPO — plantilla
//
// Copiar como clave_licencia.h (no se sube al repositorio) y reemplazar
// por 16 bytes aleatorios, distintos en cada equipo. Para generarla:
//   python3 tools/firmar_licencia.py generar clave_licencia.h
// La plataforma necesita la MISMA clave para firmar los comandos
// SUSPEND / RESUME / RENEW / FREE. Si se pierde, hay que recompilar.
// ============================================================
#include <avr/pgmspace.h>

const uint8_t CLAVE_LICENCIA[16] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
