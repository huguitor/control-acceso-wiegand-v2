// https://github.com/huguitor/control-acceso-wiegand-v2
// Control de acceso Wiegand por INTERRUPCIONES — 1 o 2 lectoras
//   L1: D0 = pin 2,  D1 = pin 3    (UNO o Mega)
//   L2: D0 = pin 18, D1 = pin 19   (solo Mega)
//
// Librería Wiegand: "Yet Another Arduino Wiegand Library" v2.0.0 — Paulo Costa
//   https://github.com/paulo-raca/YetAnotherArduinoWiegandLibrary
//   Instalar desde el Gestor de Librerías: "Yet Another Arduino Wiegand Library"
//   Nota: se usa receivedBit() con interrupciones FALLING (no setPinState con CHANGE)

// ============================================================
// VERSIÓN — se informa al arrancar como (ejemplos):
//   UNO , 1 lectora : BOOT:L1:FW:2.4.0:WIEGAND:2.0.0:READERS:1:CAP:200:LOCK:NONE
//   Mega, 2 lectoras: BOOT:L1:FW:2.4.0:WIEGAND:2.0.0:READERS:2:CAP:1000:LOCK:NONE
// Subir FW_VERSION en cada cambio que se cargue al equipo.
// ============================================================
#define FW_VERSION      "2.4.0"
#define WIEGAND_LIB_VER "2.0.0"

// ============================================================
// CANTIDAD DE LECTORAS
//   1 = solo L1 (pines 2/3) — funciona en UNO y Mega
//   2 = L1 + L2 (pines 2/3 y 18/19) — requiere Mega
// Por defecto: 2 en el Mega, 1 en el UNO. Para forzar otro valor sin
// editar el archivo (por ejemplo un Mega con una sola lectora):
//   arduino-cli compile -b arduino:avr:mega \
//     --build-property "build.extra_flags=-DNUM_LECTORAS=1" .
// ============================================================
#ifndef NUM_LECTORAS
  #if defined(__AVR_ATmega2560__)
    #define NUM_LECTORAS 2
  #else
    #define NUM_LECTORAS 1
  #endif
#endif

#if NUM_LECTORAS != 1 && NUM_LECTORAS != 2
  #error "NUM_LECTORAS debe ser 1 o 2"
#endif
#if NUM_LECTORAS == 2 && !defined(__AVR_ATmega2560__)
  #error "NUM_LECTORAS = 2 requiere Arduino Mega (pines 18/19)"
#endif

#include <Arduino.h>
#include <Wiegand.h>
#include <EEPROM.h>
#include <avr/pgmspace.h>

// ============================================================
// CLAVE DE LICENCIA — una por equipo, FUERA del repositorio
//   Copiar clave_licencia.example.h como clave_licencia.h y poner
//   16 bytes aleatorios. La misma clave la guarda la plataforma para
//   firmar SUSPEND / RESUME / RENEW / FREE (ver docs/PROTOCOLO.md).
// ============================================================
#if __has_include("clave_licencia.h")
  #include "clave_licencia.h"
#else
  #error "Falta clave_licencia.h: copiar clave_licencia.example.h y completar la clave del equipo"
#endif

Wiegand wiegand;          // L1
#if NUM_LECTORAS == 1
Wiegand wiegandSalida;    // L2
#endif

// ============================================================
// IDENTIFICADOR DE LECTORA
// ============================================================
const char LECTOR_ID[] = "L1";

// ============================================================
// DEBUG
// false = Serial limpio para gateway MQTT  ← producción
// true  = mensajes humanos, NO usar conectado al gateway
// ============================================================
#define DEBUG_SERIAL false

#if DEBUG_SERIAL
  #define DEBUG_PRINT(x)   Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
#endif

// ============================================================
// PINES
// ============================================================
const uint8_t PIN_D0             = 2;   // L1 DATA0  (INT)
const uint8_t PIN_D1             = 3;   // L1 DATA1  (INT)
const uint8_t PIN_SALIDA_D0      = 18;  // L2 DATA0  (INT)
const uint8_t PIN_SALIDA_D1      = 19;  // L2 DATA1  (INT)
const uint8_t PIN_PULSADOR_1     = 6;   // Pulsador de salida
const uint8_t PIN_LED            = 7;   // LED indicador
const uint8_t PIN_BUZZER         = 8;   // Buzzer activo bajo
const uint8_t PIN_CERRADURA      = 9;   // Relé cerradura
const uint8_t PIN_MG             = 10;  // Sensor magnético de puerta
const uint8_t PIN_DESARME_ALARMA = 11;  // Relé para desactivar alarma

// false = relé activo en LOW / true = relé activo en HIGH
#define RELE_ALARMA_INVERTIDO false
#define RELAY_INVERTIDO       false

// true = pull-ups internos en las líneas Wiegand
#define WIEGAND_PULLUP false

// Estado lógico del sensor magnético
#define PUERTA_CERRADA LOW
#define PUERTA_ABIERTA HIGH

// ============================================================
// TARJETAS FIJAS EN FLASH (PROGMEM)
// ============================================================
const uint32_t TARJETAS_FIJAS[] PROGMEM = {
  5673301,
  2743621,
};
const int NUM_TARJETAS_FIJAS = sizeof(TARJETAS_FIJAS) / sizeof(uint32_t);

// ============================================================
// EEPROM
// ============================================================
// La capacidad depende de la placa (EEPROM: UNO 1 KB, Mega 4 KB).
//
//   UNO  : 200 tarjetas  | tarjetas 0..799   | contador 800  | firma 1020 (0xABCD0001)
//   Mega : 1000 tarjetas | tarjetas 0..3999  | contador 4000 | firma 4092 (0xABCD0002)
//
// El UNO conserva el layout original (compatible con equipos instalados).
// El Mega migra solo desde el layout viejo de 200 tarjetas sin perder datos
// (ver verificarEEPROM()).
const int      EEPROM_START_ADDR = 0;

#if defined(__AVR_ATmega2560__)
const int      MAX_TARJETAS      = 1000;
const int      ADDR_CONTADOR     = EEPROM_START_ADDR + MAX_TARJETAS * sizeof(uint32_t); // 4000
const uint32_t EEPROM_FIRMA      = 0xABCD0002;
const int      ADDR_FIRMA        = 4092;

// Layout viejo (200 tarjetas) — solo para migrar
const int      OLD_MAX_TARJETAS  = 200;
const int      OLD_ADDR_CONTADOR = EEPROM_START_ADDR + OLD_MAX_TARJETAS * sizeof(uint32_t); // 800
const uint32_t OLD_EEPROM_FIRMA  = 0xABCD0001;
const int      OLD_ADDR_FIRMA    = 1020;
#else
const int      MAX_TARJETAS      = 200;
const int      ADDR_CONTADOR     = EEPROM_START_ADDR + MAX_TARJETAS * sizeof(uint32_t); // 800
const uint32_t EEPROM_FIRMA      = 0xABCD0001;
const int      ADDR_FIRMA        = 1020;
#endif

// ------------------------------------------------------------
// BLOQUE DE LICENCIA (desde FW 2.4.0)
// Zona libre entre el contador y la firma; tiene su propia marca,
// así que NO cambia el layout de tarjetas (no hace falta migrar):
//   UNO : 804..847   Mega: 4004..4047
//   +0  uint16 marca 0x4C31   +2 uint8 modo (0 libre / 1 cuotas)
//   +3  uint8  suspendido     +4 uint32 último seq aceptado
//   +8  uint32 límite (minutos de uso en que vence)
//   +12 8 × uint32 minutos usados (se rota la celda para repartir
//       el desgaste; vale el mayor)
// Un RESET de tarjetas no toca este bloque.
// ------------------------------------------------------------
const int      ADDR_LICENCIA      = ADDR_CONTADOR + 4;
const int      ADDR_LIC_MODO      = ADDR_LICENCIA + 2;
const int      ADDR_LIC_SUSP      = ADDR_LICENCIA + 3;
const int      ADDR_LIC_SEQ       = ADDR_LICENCIA + 4;
const int      ADDR_LIC_LIMITE    = ADDR_LICENCIA + 8;
const int      ADDR_LIC_SLOTS     = ADDR_LICENCIA + 12;
const uint8_t  LIC_NUM_SLOTS      = 8;
const uint16_t LIC_MARCA          = 0x4C31;
const uint8_t  LIC_MODO_LIBRE     = 0;
const uint8_t  LIC_MODO_CUOTAS    = 1;

// Duración de un "minuto" de uso. Solo se cambia para pruebas de banco:
//   --build-property "build.extra_flags=-DLICENCIA_MINUTO_MS=50"
#ifndef LICENCIA_MINUTO_MS
  #define LICENCIA_MINUTO_MS 60000UL
#endif
// Cada cuántos minutos se guarda el uso en EEPROM. Al arrancar se suman
// estos mismos minutos como penalización: cortar la luz seguido no
// estira la licencia, la acorta.
const uint8_t  LIC_GUARDAR_CADA   = 15;
const uint16_t LIC_MAX_DIAS       = 3650;

// ============================================================
// ESTADÍSTICAS POR LECTORA (tipo)
// Va antes de cualquier función para que el IDE de Arduino
// pueda generar los prototipos automáticos sin error.
// Se consultan con CMD:STATS y se ponen a cero con CMD:STATS:RESET
// ============================================================
struct StatsLectora {
  uint32_t ok;         // tramas válidas (autorizadas o no)
  uint32_t errores;    // errores de trama Wiegand
  uint32_t invalidos;  // UID 0 o 0xFFFFFF filtrados
  uint32_t ruido;      // tramas cortas (< RUIDO_MAX_BITS): ruido eléctrico
};

// ============================================================
// BUZZER NON-BLOCKING
// ============================================================
struct BuzzerStep { uint16_t on; uint16_t off; };

const BuzzerStep PAT_OK[]     PROGMEM = { {1000,0}, {0,0} };
const BuzzerStep PAT_BUTTON[] PROGMEM = { {200,0},  {0,0} };
const BuzzerStep PAT_DENIED[] PROGMEM = { {250,250},{250,250},{250,250},{250,0},{0,0} };
const BuzzerStep PAT_ERROR[]  PROGMEM = { {300,300},{300,300},{300,0},{0,0} };

const BuzzerStep* buzzerPattern = nullptr;
uint8_t           buzzerStep    = 0;
bool              buzzerPhaseOn = false;
unsigned long     buzzerTimer   = 0;

void buzzerPlay(const BuzzerStep* pattern) {
  buzzerPattern = pattern;
  buzzerStep    = 0;
  buzzerPhaseOn = true;
  buzzerTimer   = millis();
  digitalWrite(PIN_BUZZER, LOW);
}

void buzzerUpdate() {
  if (!buzzerPattern) return;

  BuzzerStep step;
  step.on  = pgm_read_word(&buzzerPattern[buzzerStep].on);
  step.off = pgm_read_word(&buzzerPattern[buzzerStep].off);

  if (step.on == 0 && step.off == 0) {
    digitalWrite(PIN_BUZZER, HIGH);
    buzzerPattern = nullptr;
    return;
  }

  unsigned long elapsed = millis() - buzzerTimer;

  if (buzzerPhaseOn) {
    if (elapsed >= step.on) {
      digitalWrite(PIN_BUZZER, HIGH);
      if (step.off == 0) {
        buzzerStep++;
        buzzerPhaseOn = true;
        buzzerTimer   = millis();
        uint16_t nextOn = pgm_read_word(&buzzerPattern[buzzerStep].on);
        if (nextOn != 0) digitalWrite(PIN_BUZZER, LOW);
      } else {
        buzzerPhaseOn = false;
        buzzerTimer   = millis();
      }
    }
  } else {
    if (elapsed >= step.off) {
      buzzerStep++;
      buzzerPhaseOn = true;
      buzzerTimer   = millis();
      uint16_t nextOn = pgm_read_word(&buzzerPattern[buzzerStep].on);
      if (nextOn != 0) digitalWrite(PIN_BUZZER, LOW);
    }
  }
}

// ============================================================
// ESTADO GENERAL
// ============================================================
int  estadoAnteriorMG = LOW;
bool modograbacion    = false;
bool timerpuerta      = false;
bool timerAlarma      = false;

unsigned long tiempoInicioAlarma    = 0;
unsigned long tiempoApertura        = 0;
unsigned long tiempoInicioGrabacion = 0;
unsigned long ultimoCambioPulsador  = 0;

const unsigned long duracionPulsoAlarma = 3000;
const unsigned long duracionApertura    = 3000;
const unsigned long timeoutGrabacion    = 120000;
const unsigned long debouncePulsador    = 80;
// RESET local: en modo grabación, mantener el pulsador este tiempo
const unsigned long tiempoResetLocal    = 10000;

int numTarjetasLeidas = 0;

const uint32_t NUMERO_MAESTRO_1 = 5292616;
const uint32_t NUMERO_MAESTRO_2 = 6484418;
const uint32_t NUMERO_MAESTRO_3 = 6486910; // test

// ============================================================
// WIEGAND — datos pendientes (los llenan los callbacks,
// los procesa el loop). Nada de Serial/EEPROM en interrupción.
// ============================================================
volatile bool     pendL1 = false, pendL2 = false;
volatile bool     errL1  = false, errL2  = false;
volatile uint32_t cardL1 = 0,     cardL2 = 0;
volatile uint8_t  errTipoL1 = 0,  errTipoL2 = 0;   // Wiegand::DataError
volatile uint8_t  errBitsL1 = 0,  errBitsL2 = 0;   // bits crudos recibidos

// UIDs que no son tarjetas reales (ruido / línea pegada)
const uint32_t UID_INVALIDO_CEROS = 0;
const uint32_t UID_INVALIDO_UNOS  = 0xFFFFFF;   // 16777215 (24 bits en 1)

// ESTADÍSTICAS POR LECTORA — solo RAM, nunca EEPROM
// (struct StatsLectora definido arriba, antes de cualquier función)
StatsLectora statsL1 = {0, 0, 0, 0};
StatsLectora statsL2 = {0, 0, 0, 0};

// CLASIFICACIÓN DE RUIDO: una trama con menos de RUIDO_MAX_BITS bits nunca
// puede ser una tarjeta (una lectura real tiene 26, y una lectura dañada
// conserva normalmente 20 o más). Esas tramas se cuentan como NOISE en
// STATS y NO se envían como ERROR, para no generar tráfico serial/MQTT
// por cada conmutación de la cerradura. Las tramas >= RUIDO_MAX_BITS que
// fallan siguen informándose como ERROR: esas sí indican un problema real.
// 0 = desactivado (todo se informa como ERROR).
#define RUIDO_MAX_BITS 8

uint32_t bytesANumero(uint8_t* data, uint8_t bits) {
  uint8_t bytes = (bits + 7) / 8;
  if (bytes > 4) bytes = 4;
  uint32_t n = 0;
  for (uint8_t i = 0; i < bytes; i++) n = (n << 8) | data[i];
  return n;
}

// ISRs — un handler por pin, disparados por FLANCO DE BAJADA.
// Cada flanco de bajada en D0 = un bit 0; en D1 = un bit 1.
// No se lee el pin: el flag de interrupción del AVR queda latcheado
// aunque la ISR se demore (p. ej. mientras corre la ISR de la otra
// lectora), así que el bit no se pierde aunque el pulso ya haya terminado.
//
// FILTRO ANTI-RUIDO: en Wiegand hay al menos ~200 µs entre bits
// (típico 1–2 ms). Un relé o la cerradura al conmutar generan ráfagas
// de flancos separados por pocos µs. Se ignora cualquier flanco que
// llegue antes de WIEGAND_MIN_INTERVAL_US desde el último bit aceptado
// en ESA lectora (D0 o D1). Cada lectora tiene su propio tiempo, así que
// no interfiere con lecturas simultáneas. 0 = filtro desactivado.
// Los flancos descartados se cuentan en STATS como FILTERED.
#define WIEGAND_MIN_INTERVAL_US 200

volatile unsigned long ultimoBitL1 = 0, ultimoBitL2 = 0;
volatile uint16_t      filtradosL1 = 0, filtradosL2 = 0;

inline bool flancoValido(volatile unsigned long& ultimo, volatile uint16_t& filtrados) {
#if WIEGAND_MIN_INTERVAL_US > 0
  unsigned long ahora = micros();
  if (ahora - ultimo < WIEGAND_MIN_INTERVAL_US) {
    if (filtrados < 0xFFFF) filtrados++;
    return false;
  }
  ultimo = ahora;
#endif
  return true;
}

void isrL1_D0() { if (flancoValido(ultimoBitL1, filtradosL1)) wiegand.receivedBit(false); }
void isrL1_D1() { if (flancoValido(ultimoBitL1, filtradosL1)) wiegand.receivedBit(true); }
#if NUM_LECTORAS == 2
void isrL2_D0() { if (flancoValido(ultimoBitL2, filtradosL2)) wiegandSalida.receivedBit(false); }
void isrL2_D1() { if (flancoValido(ultimoBitL2, filtradosL2)) wiegandSalida.receivedBit(true); }
#endif

// Callbacks
void receivedData(uint8_t* data, uint8_t bits, const char*) { cardL1 = bytesANumero(data, bits); pendL1 = true; }
void receivedDataError(Wiegand::DataError e, uint8_t*, uint8_t rawBits, const char*) {
  errTipoL1 = (uint8_t)e; errBitsL1 = rawBits; errL1 = true;
}
#if NUM_LECTORAS == 2
void receivedDataSalida(uint8_t* data, uint8_t bits, const char*) { cardL2 = bytesANumero(data, bits); pendL2 = true; }
void receivedDataErrorSalida(Wiegand::DataError e, uint8_t*, uint8_t rawBits, const char*) {
  errTipoL2 = (uint8_t)e; errBitsL2 = rawBits; errL2 = true;
}
#endif

// ============================================================
// BUFFER SERIAL ENTRANTE (comandos CMD: del gateway)
// ============================================================
char    rxBuffer[80];
uint8_t rxIndex = 0;

// ============================================================
// EEPROM — funciones de acceso
// ============================================================
uint32_t leerTarjeta(int indice) {
  uint32_t t;
  EEPROM.get(EEPROM_START_ADDR + indice * sizeof(uint32_t), t);
  return t;
}

void escribirTarjeta(int indice, uint32_t t) {
  EEPROM.put(EEPROM_START_ADDR + indice * sizeof(uint32_t), t);
}

int leerContador() {
  int n;
  EEPROM.get(ADDR_CONTADOR, n);
  if (n < 0 || n > MAX_TARJETAS) n = 0;
  return n;
}

void guardarContador(int n) {
  EEPROM.put(ADDR_CONTADOR, n);
}

// Vacía la lista poniendo el contador en 0. Las celdas viejas no se
// reescriben: nunca se leen más allá del contador, y escribir 4000 bytes
// bloquearía el equipo ~13 s (3,3 ms por byte) y gastaría la EEPROM.
void borrarEEPROM() {
  guardarContador(0);
  numTarjetasLeidas = 0;
  DEBUG_PRINTLN(F("EEPROM borrada."));
}

void verificarEEPROM() {
  uint32_t firma;
  EEPROM.get(ADDR_FIRMA, firma);
  if (firma == EEPROM_FIRMA) return;   // layout actual: nada que hacer

#if defined(__AVR_ATmega2560__)
  // ¿Viene del layout viejo de 200 tarjetas? Migrar sin perder datos.
  // Las tarjetas ya están en las mismas posiciones (0..799);
  // solo se mueve el contador de 800 a 4000 y se graba la firma nueva.
  uint32_t firmaVieja;
  EEPROM.get(OLD_ADDR_FIRMA, firmaVieja);
  if (firmaVieja == OLD_EEPROM_FIRMA) {
    int n;
    EEPROM.get(OLD_ADDR_CONTADOR, n);
    if (n < 0 || n > OLD_MAX_TARJETAS) n = 0;
    guardarContador(n);
    EEPROM.put(ADDR_FIRMA, EEPROM_FIRMA);
    return;
  }
#endif

  // EEPROM virgen o desconocida: empezar con la lista vacía
  borrarEEPROM();
  EEPROM.put(ADDR_FIRMA, EEPROM_FIRMA);
}

// ============================================================
// EVENTOS SERIALES → GATEWAY MQTT
//   BOOT:L1:FW:2.3.1:WIEGAND:2.0.0:READERS:2:CAP:1000   (una vez, al arrancar)
//   ACCESS:L1:5033541:1:1
//   BUTTON:L1:1:1
//   DOOR:L1:OPEN / CLOSED
//   MODE:L1:PROGRAM / READ:MANUAL / READ:TIMEOUT
//   ENROLL:L1:5033541:OK / EXISTS / FULL
//   DELETE:L1:5033541:OK / NOT_FOUND / FIXED
//   LIST:L1:COUNT:12 / LIST:L1:5033541 / LIST:L1:END
//   RESET:L1:OK
//   ERROR:L1:WIEGAND:<tipo>:<bits>      (o L2)  ej. ERROR:L1:WIEGAND:DecodeFailed:26
//   ERROR:L1:WIEGAND:INVALID_UID:<uid>  (UID 0 o 16777215 filtrado)
//   STATS:L1:OK:120:ERR:2:INVALID:1:FILTERED:35:NOISE:12  (respuesta a CMD:STATS)
//     OK       tramas válidas (autorizadas o no)
//     ERR      tramas fallidas de RUIDO_MAX_BITS bits o más → problema real
//     INVALID  UID 0 o 16777215 descartados
//     FILTERED flancos descartados por el filtro de 200 µs
//     NOISE    tramas cortas descartadas como ruido (no generan ERROR)
//   STATS:RESET:OK                      (respuesta a CMD:STATS:RESET)
// ============================================================
void enviarEventoAccess(uint32_t tarjeta, bool autorizado, bool aperturaHabilitada, const char* lector) {
  Serial.print(F("ACCESS:"));
  Serial.print(lector);
  Serial.print(':');
  Serial.print(tarjeta);
  Serial.print(':');
  Serial.print(autorizado ? 1 : 0);
  Serial.print(':');
  Serial.println(aperturaHabilitada ? 1 : 0);
}

void enviarEventoButton(bool aperturaHabilitada) {
  Serial.print(F("BUTTON:"));
  Serial.print(LECTOR_ID);
  Serial.print(F(":1:"));
  Serial.println(aperturaHabilitada ? 1 : 0);
}

void enviarEventoDoor(bool abierta) {
  Serial.print(F("DOOR:"));
  Serial.print(LECTOR_ID);
  Serial.print(':');
  Serial.println(abierta ? F("OPEN") : F("CLOSED"));
}

void enviarEventoMode(const __FlashStringHelper* modo) {
  Serial.print(F("MODE:"));
  Serial.print(LECTOR_ID);
  Serial.print(':');
  Serial.println(modo);
}

void enviarEventoEnroll(uint32_t tarjeta, const __FlashStringHelper* resultado) {
  Serial.print(F("ENROLL:"));
  Serial.print(LECTOR_ID);
  Serial.print(':');
  Serial.print(tarjeta);
  Serial.print(':');
  Serial.println(resultado);
}

void enviarEventoDelete(uint32_t tarjeta, const __FlashStringHelper* resultado) {
  Serial.print(F("DELETE:"));
  Serial.print(LECTOR_ID);
  Serial.print(':');
  Serial.print(tarjeta);
  Serial.print(':');
  Serial.println(resultado);
}

// COUNT es siempre el total; se envían las posiciones desde..desde+cant-1
void enviarListaTarjetas(int desde, int cant) {
  int total = NUM_TARJETAS_FIJAS + numTarjetasLeidas;
  Serial.print(F("LIST:"));
  Serial.print(LECTOR_ID);
  Serial.print(F(":COUNT:"));
  Serial.println(total);

  int hasta = desde + cant;
  if (hasta > total) hasta = total;
  for (int i = desde; i < hasta; i++) {
    Serial.print(F("LIST:"));
    Serial.print(LECTOR_ID);
    Serial.print(':');
    Serial.println(tarjetaEnPosicion(i));
  }

  Serial.print(F("LIST:"));
  Serial.print(LECTOR_ID);
  Serial.println(F(":END"));
}

// SUM:L1:COUNT:<n>:HASH:<8 hex>  — XOR de mezclar32(uid) de fijas + EEPROM
void enviarSuma() {
  int total = NUM_TARJETAS_FIJAS + numTarjetasLeidas;
  uint32_t h = 0;
  for (int i = 0; i < total; i++) h ^= mezclar32(tarjetaEnPosicion(i));
  Serial.print(F("SUM:"));
  Serial.print(LECTOR_ID);
  Serial.print(F(":COUNT:"));
  Serial.print(total);
  Serial.print(F(":HASH:"));
  for (int8_t s = 28; s >= 0; s -= 4) Serial.print((h >> s) & 0xF, HEX);
  Serial.println();
}

void enviarResetOk() {
  Serial.print(F("RESET:"));
  Serial.print(LECTOR_ID);
  Serial.println(F(":OK"));
}

// Error de trama: ERROR:L1:WIEGAND:<tipo>:<bits>
//   ej. ERROR:L1:WIEGAND:SizeUnexpected:34
void enviarEventoErrorWiegand(const char* lector, uint8_t tipo, uint8_t bits) {
  Serial.print(F("ERROR:"));
  Serial.print(lector);
  Serial.print(F(":WIEGAND:"));
  Serial.print(Wiegand::DataErrorStr((Wiegand::DataError)tipo));
  Serial.print(':');
  Serial.println(bits);
}

// UID inválido filtrado: ERROR:L1:WIEGAND:INVALID_UID:<uid>
void enviarEventoUidInvalido(const char* lector, uint32_t uid) {
  Serial.print(F("ERROR:"));
  Serial.print(lector);
  Serial.print(F(":WIEGAND:INVALID_UID:"));
  Serial.println(uid);
}

// STATS:L1:OK:120:ERR:2:INVALID:1:FILTERED:35:NOISE:12
void enviarStatsLectora(const char* lector, const StatsLectora& s, volatile uint16_t& filtrados) {
  noInterrupts();
  uint16_t f = filtrados;   // 16 bits: leer de forma atómica
  interrupts();
  Serial.print(F("STATS:"));
  Serial.print(lector);
  Serial.print(F(":OK:"));
  Serial.print(s.ok);
  Serial.print(F(":ERR:"));
  Serial.print(s.errores);
  Serial.print(F(":INVALID:"));
  Serial.print(s.invalidos);
  Serial.print(F(":FILTERED:"));
  Serial.print(f);
  Serial.print(F(":NOISE:"));
  Serial.println(s.ruido);
}

void enviarStats() {
  enviarStatsLectora("L1", statsL1, filtradosL1);
#if NUM_LECTORAS == 2
  enviarStatsLectora("L2", statsL2, filtradosL2);
#endif
}

void resetStats() {
  statsL1 = {0, 0, 0, 0};
  statsL2 = {0, 0, 0, 0};
  noInterrupts();
  filtradosL1 = filtradosL2 = 0;
  interrupts();
  Serial.println(F("STATS:RESET:OK"));
}

// ============================================================
// TARJETAS — consultas
// ============================================================
bool esTarjetaMaestra(uint32_t n) {
  return n == NUMERO_MAESTRO_1 ||
         n == NUMERO_MAESTRO_2 ||
         n == NUMERO_MAESTRO_3;
}

bool esTarjetaFija(uint32_t n) {
  for (int i = 0; i < NUM_TARJETAS_FIJAS; i++) {
    if (pgm_read_dword(&TARJETAS_FIJAS[i]) == n) return true;
  }
  return false;
}

bool tarjetaYaAlmacenada(uint32_t n) {
  if (esTarjetaFija(n)) return true;
  for (int i = 0; i < numTarjetasLeidas; i++) {
    if (leerTarjeta(i) == n) return true;
  }
  return false;
}

void almacenarTarjetaEnEEPROM(uint32_t n) {
  escribirTarjeta(numTarjetasLeidas, n);
  numTarjetasLeidas++;
  guardarContador(numTarjetasLeidas);
}

// Borra moviendo la ÚLTIMA tarjeta al hueco (el orden no importa).
// Antes se corría todo el array una posición: con 1000 tarjetas eso
// podía significar ~4000 bytes escritos (~13 s bloqueado).
// Ahora es como máximo una escritura de 4 bytes.
bool borrarTarjetaDeEEPROM(uint32_t n) {
  if (esTarjetaFija(n)) return false;
  for (int i = 0; i < numTarjetasLeidas; i++) {
    if (leerTarjeta(i) == n) {
      int ultima = numTarjetasLeidas - 1;
      if (i != ultima) escribirTarjeta(i, leerTarjeta(ultima));
      numTarjetasLeidas--;
      guardarContador(numTarjetasLeidas);
      return true;
    }
  }
  return false;
}

// ============================================================
// UID recibido por comando: solo dígitos, 1..8 cifras, ≠ 0 y < 0xFFFFFF.
// Antes atol() convertía cualquier texto en 0 y lo grababa.
// ============================================================
bool parsearUid(const char* s, uint32_t& uid) {
  if (!s || !*s) return false;
  uint32_t n = 0;
  uint8_t  cifras = 0;
  for (; *s; s++) {
    if (*s < '0' || *s > '9') return false;
    if (++cifras > 8) return false;
    n = n * 10 + (uint32_t)(*s - '0');
  }
  if (n == UID_INVALIDO_CEROS || n >= UID_INVALIDO_UNOS) return false;
  uid = n;
  return true;
}

bool parsearEntero(const char* s, uint32_t& v) {
  if (!s || !*s) return false;
  uint32_t n = 0;
  uint8_t  cifras = 0;
  for (; *s; s++) {
    if (*s < '0' || *s > '9') return false;
    if (++cifras > 9) return false;
    n = n * 10 + (uint32_t)(*s - '0');
  }
  v = n;
  return true;
}

// ============================================================
// LISTA COMBINADA (fijas + EEPROM), en el mismo orden que CMD:LIST
// ============================================================
int totalTarjetas() { return NUM_TARJETAS_FIJAS + numTarjetasLeidas; }

uint32_t tarjetaEnPosicion(int i) {
  if (i < NUM_TARJETAS_FIJAS) return pgm_read_dword(&TARJETAS_FIJAS[i]);
  return leerTarjeta(i - NUM_TARJETAS_FIJAS);
}

// Mezcla de 32 bits (finalizador de MurmurHash3). La suma de control es el
// XOR de la mezcla de cada UID: no depende del orden (un DELETE reordena).
uint32_t mezclar32(uint32_t h) {
  h ^= h >> 16; h *= 0x85EBCA6BUL;
  h ^= h >> 13; h *= 0xC2B2AE35UL;
  h ^= h >> 16;
  return h;
}

// ============================================================
// LICENCIA — suspensión manual y vencimiento por tiempo de uso
// ============================================================
uint8_t       licModo         = LIC_MODO_LIBRE;
bool          licSuspendido   = false;
uint32_t      licSeq          = 0;
uint32_t      licLimite       = 0;
uint32_t      licMinutosUsados = 0;
uint8_t       licSlotSiguiente = 0;
uint8_t       licSinGuardar   = 0;
unsigned long licUltimoMinuto = 0;
unsigned long licUltimoFallo  = 0;
bool          licHuboFallo    = false;

const uint8_t BLOQUEO_NINGUNO    = 0;
const uint8_t BLOQUEO_SUSPENDIDO = 1;
const uint8_t BLOQUEO_VENCIDO    = 2;
uint8_t       bloqueoAnterior    = BLOQUEO_NINGUNO;

uint8_t estadoBloqueo() {
  if (licSuspendido) return BLOQUEO_SUSPENDIDO;
  if (licModo == LIC_MODO_CUOTAS && licMinutosUsados >= licLimite) return BLOQUEO_VENCIDO;
  return BLOQUEO_NINGUNO;
}

bool equipoBloqueado() { return estadoBloqueo() != BLOQUEO_NINGUNO; }

const __FlashStringHelper* textoBloqueo(uint8_t b) {
  if (b == BLOQUEO_SUSPENDIDO) return F("SUSPENDED");
  if (b == BLOQUEO_VENCIDO)    return F("EXPIRED");
  return F("NONE");
}

void guardarMinutosUsados() {
  EEPROM.put(ADDR_LIC_SLOTS + licSlotSiguiente * sizeof(uint32_t), licMinutosUsados);
  licSlotSiguiente = (licSlotSiguiente + 1) % LIC_NUM_SLOTS;
  licSinGuardar = 0;
}

void iniciarLicencia() {
  uint16_t marca;
  EEPROM.get(ADDR_LICENCIA, marca);
  if (marca != LIC_MARCA) {
    // Equipo nuevo o actualizado desde < 2.4.0: licencia libre, sin suspender.
    uint32_t cero = 0;
    EEPROM.put(ADDR_LIC_MODO,   LIC_MODO_LIBRE);
    EEPROM.put(ADDR_LIC_SUSP,   (uint8_t)0);
    EEPROM.put(ADDR_LIC_SEQ,    cero);
    EEPROM.put(ADDR_LIC_LIMITE, cero);
    for (uint8_t i = 0; i < LIC_NUM_SLOTS; i++)
      EEPROM.put(ADDR_LIC_SLOTS + i * sizeof(uint32_t), cero);
    EEPROM.put(ADDR_LICENCIA, LIC_MARCA);   // la marca al final: si se corta, se reinicia
  }

  EEPROM.get(ADDR_LIC_MODO,   licModo);
  uint8_t s;
  EEPROM.get(ADDR_LIC_SUSP,   s);
  licSuspendido = (s != 0);
  EEPROM.get(ADDR_LIC_SEQ,    licSeq);
  EEPROM.get(ADDR_LIC_LIMITE, licLimite);
  if (licModo != LIC_MODO_CUOTAS) licModo = LIC_MODO_LIBRE;

  uint8_t idxMax = 0;
  licMinutosUsados = 0;
  for (uint8_t i = 0; i < LIC_NUM_SLOTS; i++) {
    uint32_t v;
    EEPROM.get(ADDR_LIC_SLOTS + i * sizeof(uint32_t), v);
    if (v >= licMinutosUsados) { licMinutosUsados = v; idxMax = i; }
  }
  licSlotSiguiente = (idxMax + 1) % LIC_NUM_SLOTS;

  if (licModo == LIC_MODO_CUOTAS) {
    licMinutosUsados += LIC_GUARDAR_CADA;   // penalización por arranque
    guardarMinutosUsados();
  }
  licUltimoMinuto = millis();
  bloqueoAnterior = estadoBloqueo();
}

uint32_t minutosRestantes() {
  if (licModo != LIC_MODO_CUOTAS || licMinutosUsados >= licLimite) return 0;
  return licLimite - licMinutosUsados;
}

// LICENSE:L1:MODE:TERM:STATE:ACTIVE:LEFT:43185:SEQ:7
void enviarEstadoLicencia() {
  Serial.print(F("LICENSE:"));
  Serial.print(LECTOR_ID);
  Serial.print(F(":MODE:"));
  Serial.print(licModo == LIC_MODO_CUOTAS ? F("TERM") : F("FREE"));
  Serial.print(F(":STATE:"));
  uint8_t b = estadoBloqueo();
  Serial.print(b == BLOQUEO_NINGUNO ? F("ACTIVE") : textoBloqueo(b));
  Serial.print(F(":LEFT:"));
  Serial.print(minutosRestantes());
  Serial.print(F(":SEQ:"));
  Serial.println(licSeq);
}

void enviarEventoAuth(const __FlashStringHelper* resultado) {
  Serial.print(F("AUTH:"));
  Serial.print(LECTOR_ID);
  Serial.print(':');
  Serial.println(resultado);
}

// Avanza el reloj de uso y avisa cuando cambia el bloqueo.
void actualizarLicencia() {
  if (millis() - licUltimoMinuto >= LICENCIA_MINUTO_MS) {
    licUltimoMinuto += LICENCIA_MINUTO_MS;
    if (licModo == LIC_MODO_CUOTAS && licMinutosUsados < 0xFFFFFFF0UL) {
      licMinutosUsados++;
      if (++licSinGuardar >= LIC_GUARDAR_CADA) guardarMinutosUsados();
    }
  }
  uint8_t b = estadoBloqueo();
  if (b != bloqueoAnterior) {
    bloqueoAnterior = b;
    if (b != BLOQUEO_NINGUNO && modograbacion) {
      modograbacion = false;
      digitalWrite(PIN_LED, HIGH);
    }
    enviarEstadoLicencia();
  }
}

// ------------------------------------------------------------
// SipHash-2-4 (64 bits) para firmar comandos de licencia.
// Clave de 16 bytes en PROGMEM (clave_licencia.h).
// ------------------------------------------------------------
static inline uint64_t rotl64(uint64_t x, uint8_t b) { return (x << b) | (x >> (64 - b)); }

#define SIPROUND do { \
  v0 += v1; v1 = rotl64(v1, 13); v1 ^= v0; v0 = rotl64(v0, 32); \
  v2 += v3; v3 = rotl64(v3, 16); v3 ^= v2;                      \
  v0 += v3; v3 = rotl64(v3, 21); v3 ^= v0;                      \
  v2 += v1; v1 = rotl64(v1, 17); v1 ^= v2; v2 = rotl64(v2, 32); \
} while (0)

uint64_t siphash24(const uint8_t* m, uint8_t len) {
  uint64_t k0 = 0, k1 = 0;
  for (uint8_t i = 0; i < 8; i++) {
    k0 |= (uint64_t)pgm_read_byte(&CLAVE_LICENCIA[i])     << (8 * i);
    k1 |= (uint64_t)pgm_read_byte(&CLAVE_LICENCIA[i + 8]) << (8 * i);
  }
  uint64_t v0 = k0 ^ 0x736f6d6570736575ULL;
  uint64_t v1 = k1 ^ 0x646f72616e646f6dULL;
  uint64_t v2 = k0 ^ 0x6c7967656e657261ULL;
  uint64_t v3 = k1 ^ 0x7465646279746573ULL;

  uint8_t fin = len - (len % 8);
  for (uint8_t i = 0; i < fin; i += 8) {
    uint64_t w = 0;
    for (uint8_t j = 0; j < 8; j++) w |= (uint64_t)m[i + j] << (8 * j);
    v3 ^= w; SIPROUND; SIPROUND; v0 ^= w;
  }
  uint64_t b = ((uint64_t)len) << 56;
  for (uint8_t j = 0; j < len % 8; j++) b |= (uint64_t)m[fin + j] << (8 * j);
  v3 ^= b; SIPROUND; SIPROUND; v0 ^= b;
  v2 ^= 0xff;
  SIPROUND; SIPROUND; SIPROUND; SIPROUND;
  return v0 ^ v1 ^ v2 ^ v3;
}

bool parsearHex64(const char* s, uint64_t& v) {
  if (!s || strlen(s) != 16) return false;
  uint64_t n = 0;
  for (; *s; s++) {
    char c = *s;
    uint8_t d;
    if (c >= '0' && c <= '9')      d = c - '0';
    else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
    else return false;
    n = (n << 4) | d;
  }
  v = n;
  return true;
}

// Comandos firmados (el MAC va siempre al final):
//   CMD:SUSPEND:<seq>:<mac>     CMD:RESUME:<seq>:<mac>
//   CMD:RENEW:<dias>:<seq>:<mac>   CMD:FREE:<seq>:<mac>
// <mac> = SipHash-2-4(clave, "<texto entre 'CMD:' y ':<mac>'>") en 16 hex.
// <seq> tiene que ser mayor que el último aceptado (evita repetir órdenes viejas).
// Devuelve false si la línea no es un comando de licencia.
bool procesarComandoLicencia(char* line) {
  char* msg = line + 4;   // ya se verificó que empieza con "CMD:"
  bool esLic = strncmp(msg, "SUSPEND:", 8) == 0 || strncmp(msg, "RESUME:", 7) == 0 ||
               strncmp(msg, "RENEW:", 6) == 0   || strncmp(msg, "FREE:", 5) == 0;
  if (!esLic) return false;

  // Un intento por segundo después de un fallo (fuerza bruta inviable)
  if (licHuboFallo && millis() - licUltimoFallo < 1000) { enviarEventoAuth(F("WAIT")); return true; }

  char* sep = strrchr(msg, ':');
  uint64_t macRecibido;
  if (!sep || !parsearHex64(sep + 1, macRecibido)) {
    licHuboFallo = true; licUltimoFallo = millis();
    enviarEventoAuth(F("FAIL"));
    return true;
  }
  *sep = '\0';
  if (siphash24((const uint8_t*)msg, (uint8_t)strlen(msg)) != macRecibido) {
    licHuboFallo = true; licUltimoFallo = millis();
    enviarEventoAuth(F("FAIL"));
    return true;
  }
  licHuboFallo = false;

  // Firma válida: interpretar
  char* cmd   = strtok(msg, ":");
  char* arg1  = strtok(NULL, ":");
  char* arg2  = strtok(NULL, ":");
  bool  renew = strcmp(cmd, "RENEW") == 0;
  char* seqStr = renew ? arg2 : arg1;

  uint32_t seq;
  if (!parsearEntero(seqStr, seq) || (!renew && arg2)) { enviarEventoAuth(F("BAD")); return true; }
  if (seq <= licSeq) { enviarEventoAuth(F("OLD_SEQ")); return true; }

  uint32_t dias = 0;
  if (renew && (!parsearEntero(arg1, dias) || dias > LIC_MAX_DIAS)) { enviarEventoAuth(F("BAD")); return true; }

  if (strcmp(cmd, "SUSPEND") == 0) {
    licSuspendido = true;
    EEPROM.put(ADDR_LIC_SUSP, (uint8_t)1);
  } else if (strcmp(cmd, "RESUME") == 0) {
    licSuspendido = false;
    EEPROM.put(ADDR_LIC_SUSP, (uint8_t)0);
  } else if (renew) {
    licModo   = LIC_MODO_CUOTAS;
    licLimite = licMinutosUsados + dias * 1440UL;
    EEPROM.put(ADDR_LIC_LIMITE, licLimite);
    EEPROM.put(ADDR_LIC_MODO,   licModo);
    guardarMinutosUsados();
  } else {   // FREE: cuotas pagadas, no vence más
    licModo = LIC_MODO_LIBRE;
    EEPROM.put(ADDR_LIC_MODO, licModo);
  }
  licSeq = seq;
  EEPROM.put(ADDR_LIC_SEQ, licSeq);

  bloqueoAnterior = estadoBloqueo();
  if (bloqueoAnterior != BLOQUEO_NINGUNO && modograbacion) {
    modograbacion = false;
    digitalWrite(PIN_LED, HIGH);
  }
  enviarEstadoLicencia();
  return true;
}

// ============================================================
// COMANDOS REMOTOS
//   CMD:ENROLL:5033541 / CMD:DELETE:5033541 / CMD:LIST[:desde:cant]
//   CMD:SUM / CMD:LICENSE / comandos de licencia firmados
//   CMD:RESET ya no borra: responde RESET:L1:DENIED (solo reset local)
// ============================================================
// Respuesta a un UID mal formado: ENROLL:L1:<texto>:INVALID
void enviarUidInvalido(const __FlashStringHelper* tipo, const char* texto) {
  Serial.print(tipo);
  Serial.print(LECTOR_ID);
  Serial.print(':');
  Serial.print(texto);
  Serial.println(F(":INVALID"));
}

void procesarComando(char* line) {
  if (strncmp(line, "CMD:", 4) != 0) return;
  if (procesarComandoLicencia(line)) return;   // antes de strtok: necesita la línea entera

  char* token = strtok(line, ":");
  if (!token) return;

  char* cmd = strtok(NULL, ":");
  if (!cmd) return;

  if (strcmp(cmd, "ENROLL") == 0) {
    char* uidStr = strtok(NULL, ":");
    if (!uidStr) return;
    uint32_t uid;
    if (!parsearUid(uidStr, uid)) { enviarUidInvalido(F("ENROLL:"), uidStr); return; }

    if (tarjetaYaAlmacenada(uid))          { enviarEventoEnroll(uid, F("EXISTS")); return; }
    if (numTarjetasLeidas >= MAX_TARJETAS) { enviarEventoEnroll(uid, F("FULL"));   return; }
    almacenarTarjetaEnEEPROM(uid);
    enviarEventoEnroll(uid, F("OK"));
    return;
  }

  if (strcmp(cmd, "DELETE") == 0) {
    char* uidStr = strtok(NULL, ":");
    if (!uidStr) return;
    uint32_t uid;
    if (!parsearUid(uidStr, uid)) { enviarUidInvalido(F("DELETE:"), uidStr); return; }

    if (esTarjetaFija(uid)) { enviarEventoDelete(uid, F("FIXED")); return; }
    bool ok = borrarTarjetaDeEEPROM(uid);
    enviarEventoDelete(uid, ok ? F("OK") : F("NOT_FOUND"));
    return;
  }

  // CMD:STATS  /  CMD:STATS:RESET
  if (strcmp(cmd, "STATS") == 0) {
    char* sub = strtok(NULL, ":");
    if (sub && strcmp(sub, "RESET") == 0) resetStats();
    else                                  enviarStats();
    return;
  }

  // CMD:VERSION → BOOT:L1:FW:x.y.z:WIEGAND:x.y.z:READERS:n:CAP:n  (consultar sin resetear)
  if (strcmp(cmd, "VERSION") == 0) { enviarEventoBoot(); return; }

  // CMD:LIST            → todas (compatible con versiones anteriores)
  // CMD:LIST:<desde>:<cant> → una página (posiciones 0..COUNT-1, cant ≤ 50)
  if (strcmp(cmd, "LIST") == 0) {
    char* desdeStr = strtok(NULL, ":");
    char* cantStr  = strtok(NULL, ":");
    if (!desdeStr) { enviarListaTarjetas(0, totalTarjetas()); return; }
    uint32_t desde, cant;
    if (!parsearEntero(desdeStr, desde) || !parsearEntero(cantStr, cant) || cant == 0 || cant > 50) {
      Serial.print(F("LIST:")); Serial.print(LECTOR_ID); Serial.println(F(":BAD"));
      return;
    }
    enviarListaTarjetas((int)min(desde, (uint32_t)totalTarjetas()), (int)cant);
    return;
  }

  // CMD:SUM → SUM:L1:COUNT:650:HASH:1a2b3c4d   (detectar diferencias sin listar)
  if (strcmp(cmd, "SUM") == 0) { enviarSuma(); return; }

  // CMD:LICENSE → estado de la licencia (no requiere firma)
  if (strcmp(cmd, "LICENSE") == 0) { enviarEstadoLicencia(); return; }

  // El borrado total ya no se acepta por serie: solo localmente
  // (modo grabación + pulsador 10 s). Ver manejarPulsadorApertura().
  if (strcmp(cmd, "RESET") == 0) {
    Serial.print(F("RESET:")); Serial.print(LECTOR_ID); Serial.println(F(":DENIED"));
    return;
  }

  DEBUG_PRINTLN(F("[CMD] Comando desconocido"));
}

void leerSerialEntrante() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      rxBuffer[rxIndex] = '\0';
      if (rxIndex > 0) procesarComando(rxBuffer);
      rxIndex = 0;
      return;
    }
    if (rxIndex < sizeof(rxBuffer) - 1) rxBuffer[rxIndex++] = c;
    else rxIndex = 0;  // overflow: descartar
  }
}

// ============================================================
// ACCIONES
// ============================================================
void abrirPuertaLocal() {
  timerpuerta    = true;
  tiempoApertura = millis();
}

void desarmarAlarma() {
  timerAlarma        = true;
  tiempoInicioAlarma = millis();
}

// ============================================================
// MODOS
// ============================================================
void salirModoGrabacionManual() {
  modograbacion = false;
  digitalWrite(PIN_LED, HIGH);
  enviarEventoMode(F("READ:MANUAL"));
}

void salirModoGrabacionTimeout() {
  modograbacion = false;
  digitalWrite(PIN_LED, HIGH);
  enviarEventoMode(F("READ:TIMEOUT"));
}

void manejarModoGrabacion(uint32_t numeroTarjeta) {
  if (tarjetaYaAlmacenada(numeroTarjeta)) {
    enviarEventoEnroll(numeroTarjeta, F("EXISTS"));
    buzzerPlay(PAT_DENIED);
    return;
  }
  if (numTarjetasLeidas < MAX_TARJETAS) {
    almacenarTarjetaEnEEPROM(numeroTarjeta);
    tiempoInicioGrabacion = millis();
    enviarEventoEnroll(numeroTarjeta, F("OK"));
    buzzerPlay(PAT_OK);
  } else {
    enviarEventoEnroll(numeroTarjeta, F("FULL"));
    buzzerPlay(PAT_ERROR);
  }
}

void manejarModoLectura(uint32_t numeroTarjeta, const char* lector) {
  bool autorizada = tarjetaYaAlmacenada(numeroTarjeta);
  // Equipo suspendido o con licencia vencida: la tarjeta sigue siendo
  // válida (ACCESS ...:1:0 = autorizada sin apertura) pero no abre ni desarma.
  if (autorizada && equipoBloqueado()) {
    buzzerPlay(PAT_DENIED);
    enviarEventoAccess(numeroTarjeta, true, false, lector);
    return;
  }
  if (autorizada) {
    abrirPuertaLocal();
    desarmarAlarma();
    buzzerPlay(PAT_BUTTON);
    enviarEventoAccess(numeroTarjeta, true, true, lector);
  } else {
    buzzerPlay(PAT_DENIED);
    enviarEventoAccess(numeroTarjeta, false, false, lector);
  }
}

void procesarTarjeta(uint32_t numeroTarjeta, const char* lector) {
  if (esTarjetaMaestra(numeroTarjeta)) {
    // Bloqueado: la maestra no habilita grabar tarjetas nuevas
    if (equipoBloqueado()) {
      buzzerPlay(PAT_ERROR);
      enviarEventoMode(F("LOCKED"));
      return;
    }
    modograbacion = !modograbacion;
    if (modograbacion) {
      tiempoInicioGrabacion = millis();
      enviarEventoMode(F("PROGRAM"));
    } else {
      salirModoGrabacionManual();
    }
    return;
  }

  if (modograbacion) manejarModoGrabacion(numeroTarjeta);
  else               manejarModoLectura(numeroTarjeta, lector);
}

// ============================================================
// LECTORAS — toma resultados pendientes de forma atómica
// ============================================================
void atenderLectoras() {
  noInterrupts();
  wiegand.flush();        // descarta tramas incompletas por timeout
#if NUM_LECTORAS == 2
  wiegandSalida.flush();
#endif
  bool     p1 = pendL1, p2 = pendL2, e1 = errL1, e2 = errL2;
  uint32_t c1 = cardL1, c2 = cardL2;
  uint8_t  t1 = errTipoL1, t2 = errTipoL2;
  uint8_t  b1 = errBitsL1, b2 = errBitsL2;
  pendL1 = pendL2 = errL1 = errL2 = false;
  interrupts();

  if (p1) atenderTarjeta(c1, "L1", statsL1);
  if (p2) atenderTarjeta(c2, "L2", statsL2);
  if (e1) atenderError("L1", t1, b1, statsL1);
  if (e2) atenderError("L2", t2, b2, statsL2);
}

// Separa ruido (tramas cortas, solo contador) de errores reales (se informan).
void atenderError(const char* lector, uint8_t tipo, uint8_t bits, StatsLectora& stats) {
  if (bits < RUIDO_MAX_BITS) {
    stats.ruido++;
    return;
  }
  stats.errores++;
  enviarEventoErrorWiegand(lector, tipo, bits);
}

// Filtra UIDs inválidos antes de la lógica de acceso.
// Un UID inválido NO genera ACCESS ni buzzer: nadie pasó una tarjeta.
void atenderTarjeta(uint32_t uid, const char* lector, StatsLectora& stats) {
  if (uid == UID_INVALIDO_CEROS || uid == UID_INVALIDO_UNOS) {
    stats.invalidos++;
    enviarEventoUidInvalido(lector, uid);
    return;
  }
  stats.ok++;
  procesarTarjeta(uid, lector);
}

// ============================================================
// PULSADOR DE APERTURA — debounce por millis()
// ============================================================
// El pulsador abre SIEMPRE, aunque el equipo esté suspendido o vencido:
// nunca puede quedar alguien encerrado.
int           lecturaAnteriorPulsador1 = HIGH;
int           estadoEstablePulsador1   = HIGH;
unsigned long inicioPulsacion          = 0;
bool          resetLocalHecho          = false;

void manejarPulsadorApertura() {
  int lecturaActual = digitalRead(PIN_PULSADOR_1);

  if (lecturaActual != lecturaAnteriorPulsador1) {
    ultimoCambioPulsador     = millis();
    lecturaAnteriorPulsador1 = lecturaActual;
  }

  if ((millis() - ultimoCambioPulsador) > debouncePulsador) {
    if (lecturaActual != estadoEstablePulsador1) {
      estadoEstablePulsador1 = lecturaActual;
      if (estadoEstablePulsador1 == LOW) {
        inicioPulsacion = millis();
        resetLocalHecho = false;
        abrirPuertaLocal();
        buzzerPlay(PAT_BUTTON);
        enviarEventoButton(true);
      }
    }
  }

  // RESET LOCAL: en modo grabación (entrado con la maestra), mantener el
  // pulsador 10 s borra todas las tarjetas de la EEPROM. Las fijas y la
  // licencia no se tocan. Es la única forma de borrado total.
  if (modograbacion && estadoEstablePulsador1 == LOW && !resetLocalHecho &&
      millis() - inicioPulsacion >= tiempoResetLocal) {
    resetLocalHecho = true;
    borrarEEPROM();
    enviarResetOk();
    buzzerPlay(PAT_ERROR);
    salirModoGrabacionManual();
  }
}

// ============================================================
// SENSOR MAGNÉTICO — debounce por millis()
// ============================================================
unsigned long tiempoDebounceMG  = 0;
int           lecturaAnteriorMG = LOW;
bool          pendienteMG       = false;

void manejarSensorMagnetico() {
  int lectura = digitalRead(PIN_MG);

  if (lectura != lecturaAnteriorMG) {
    lecturaAnteriorMG = lectura;
    tiempoDebounceMG  = millis();
    pendienteMG       = true;
  }

  if (pendienteMG && (millis() - tiempoDebounceMG >= 30)) {
    pendienteMG = false;
    if (lectura != estadoAnteriorMG) {
      estadoAnteriorMG = lectura;
      enviarEventoDoor(lectura == PUERTA_ABIERTA);
    }
  }
}

// ============================================================
// TEMPORIZADORES
// ============================================================
void controlarTemporizadorPuerta() {
  if (!timerpuerta) return;

  digitalWrite(PIN_LED,       LOW);
  digitalWrite(PIN_CERRADURA, RELAY_INVERTIDO ? HIGH : LOW);

  if (millis() - tiempoApertura >= duracionApertura) {
    digitalWrite(PIN_LED,       HIGH);
    digitalWrite(PIN_CERRADURA, RELAY_INVERTIDO ? LOW : HIGH);
    timerpuerta = false;
  }
}

void controlarTemporizadorAlarma() {
  if (!timerAlarma) return;

  digitalWrite(PIN_DESARME_ALARMA, RELE_ALARMA_INVERTIDO ? HIGH : LOW);

  if (millis() - tiempoInicioAlarma >= duracionPulsoAlarma) {
    digitalWrite(PIN_DESARME_ALARMA, RELE_ALARMA_INVERTIDO ? LOW : HIGH);
    timerAlarma = false;
  }
}

void controlarLEDIntermitente() {
  static unsigned long tiempoPrevio = 0;
  static bool estadoLED = LOW;

  if (millis() - tiempoPrevio >= 500) {
    estadoLED = !estadoLED;
    digitalWrite(PIN_LED, estadoLED);
    tiempoPrevio = millis();
  }
}

// ============================================================
// DEBUG
// ============================================================
void leerEEPROM() {
  DEBUG_PRINT(F("--- Tarjetas fijas: "));
  DEBUG_PRINT(NUM_TARJETAS_FIJAS);
  DEBUG_PRINTLN(F(" ---"));
  for (int i = 0; i < NUM_TARJETAS_FIJAS; i++) {
    DEBUG_PRINTLN(pgm_read_dword(&TARJETAS_FIJAS[i]));
  }
  DEBUG_PRINT(F("--- Tarjetas EEPROM: "));
  DEBUG_PRINT(numTarjetasLeidas);
  DEBUG_PRINTLN(F(" ---"));
  for (int i = 0; i < numTarjetasLeidas; i++) {
    DEBUG_PRINTLN(leerTarjeta(i));
  }
}

// ============================================================
// SETUP
// ============================================================
void enviarEventoBoot() {
  Serial.print(F("BOOT:"));
  Serial.print(LECTOR_ID);
  Serial.print(F(":FW:"));
  Serial.print(F(FW_VERSION));
  Serial.print(F(":WIEGAND:"));
  Serial.print(F(WIEGAND_LIB_VER));
  Serial.print(F(":READERS:"));
  Serial.print(NUM_LECTORAS);
  Serial.print(F(":CAP:"));
  Serial.print(MAX_TARJETAS);
  Serial.print(F(":LOCK:"));               // campo nuevo al final: compatible
  Serial.println(textoBloqueo(estadoBloqueo()));
}

void setup() {
  Serial.begin(9600);
  delay(200);         // dejar que el monitor / gateway termine de abrir el puerto
  Serial.println();   // separa la basura del reset de la primera línea útil
  DEBUG_PRINTLN(F("https://github.com/huguitor/control-acceso-wiegand-v2"));

  uint8_t modoWiegand = WIEGAND_PULLUP ? INPUT_PULLUP : INPUT;
  pinMode(PIN_D0,             modoWiegand);
  pinMode(PIN_D1,             modoWiegand);
#if NUM_LECTORAS == 2
  pinMode(PIN_SALIDA_D0,      modoWiegand);
  pinMode(PIN_SALIDA_D1,      modoWiegand);
#endif
  pinMode(PIN_PULSADOR_1,     INPUT);
  pinMode(PIN_MG,             INPUT);
  pinMode(PIN_LED,            OUTPUT);
  pinMode(PIN_BUZZER,         OUTPUT);
  pinMode(PIN_CERRADURA,      OUTPUT);
  pinMode(PIN_DESARME_ALARMA, OUTPUT);

  // Estado seguro inicial
  digitalWrite(PIN_LED,            HIGH);
  digitalWrite(PIN_BUZZER,         HIGH);
  digitalWrite(PIN_CERRADURA,      RELAY_INVERTIDO ? LOW : HIGH);
  digitalWrite(PIN_DESARME_ALARMA, RELE_ALARMA_INVERTIDO ? LOW : HIGH);

  // Lectoras
  wiegand.onReceive(receivedData, "");
  wiegand.onReceiveError(receivedDataError, "");
  wiegand.begin(26, true);
  attachInterrupt(digitalPinToInterrupt(PIN_D0), isrL1_D0, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_D1), isrL1_D1, FALLING);
  wiegand.setPin0State(digitalRead(PIN_D0));   // estado inicial de las líneas
  wiegand.setPin1State(digitalRead(PIN_D1));

#if NUM_LECTORAS == 2
  wiegandSalida.onReceive(receivedDataSalida, "");
  wiegandSalida.onReceiveError(receivedDataErrorSalida, "");
  wiegandSalida.begin(26, true);
  attachInterrupt(digitalPinToInterrupt(PIN_SALIDA_D0), isrL2_D0, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_SALIDA_D1), isrL2_D1, FALLING);
  wiegandSalida.setPin0State(digitalRead(PIN_SALIDA_D0));
  wiegandSalida.setPin1State(digitalRead(PIN_SALIDA_D1));
#endif

  delay(500);

  verificarEEPROM();
  numTarjetasLeidas = leerContador();
  iniciarLicencia();
  estadoAnteriorMG  = digitalRead(PIN_MG);
  lecturaAnteriorMG = estadoAnteriorMG;

  enviarEventoBoot();
  enviarEventoDoor(estadoAnteriorMG == PUERTA_ABIERTA);
}

// ============================================================
// LOOP — todo non-blocking
// ============================================================
void loop() {
  atenderLectoras();
  manejarPulsadorApertura();
  manejarSensorMagnetico();
  controlarTemporizadorPuerta();
  controlarTemporizadorAlarma();
  buzzerUpdate();
  leerSerialEntrante();
  actualizarLicencia();

  if (modograbacion) {
    controlarLEDIntermitente();
    if (millis() - tiempoInicioGrabacion >= timeoutGrabacion) {
      salirModoGrabacionTimeout();
    }
  }

#if DEBUG_SERIAL
  if (Serial.available()) {
    char opcion = Serial.read();
    if (opcion == 'l') leerEEPROM();
    if (opcion == 'b') borrarEEPROM();
  }
#endif
}