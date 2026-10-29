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
// VERSIÓN — se informa al arrancar como:
//   BOOT:L1:FW:2.3.0:WIEGAND:2.0.0:READERS:2:CAP:1000
// Subir FW_VERSION en cada cambio que se cargue al equipo.
// ============================================================
#define FW_VERSION      "2.3.0"
#define WIEGAND_LIB_VER "2.0.0"

// ============================================================
// CANTIDAD DE LECTORAS — ÚNICO valor a cambiar por equipo
//   1 = solo L1 (pines 2/3) — funciona en UNO y Mega
//   2 = L1 + L2 (pines 2/3 y 18/19) — requiere Mega
// ============================================================
#define NUM_LECTORAS 2

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

Wiegand wiegand;          // L1
#if NUM_LECTORAS == 2
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

int numTarjetasLeidas = 0;

const uint32_t NUMERO_MAESTRO_1 = 13170128;
const uint32_t NUMERO_MAESTRO_2 = 13139469;
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
StatsLectora statsL1 = {0, 0, 0};
StatsLectora statsL2 = {0, 0, 0};

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
void isrL1_D0() { wiegand.receivedBit(false); }
void isrL1_D1() { wiegand.receivedBit(true); }
#if NUM_LECTORAS == 2
void isrL2_D0() { wiegandSalida.receivedBit(false); }
void isrL2_D1() { wiegandSalida.receivedBit(true); }
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
//   BOOT:L1:FW:2.3.0:WIEGAND:2.0.0:READERS:2:CAP:1000   (una vez, al arrancar)
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
//   STATS:L1:OK:120:ERR:2:INVALID:1     (respuesta a CMD:STATS)
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

void enviarListaTarjetas() {
  Serial.print(F("LIST:"));
  Serial.print(LECTOR_ID);
  Serial.print(F(":COUNT:"));
  Serial.println(NUM_TARJETAS_FIJAS + numTarjetasLeidas);

  for (int i = 0; i < NUM_TARJETAS_FIJAS; i++) {
    Serial.print(F("LIST:"));
    Serial.print(LECTOR_ID);
    Serial.print(':');
    Serial.println(pgm_read_dword(&TARJETAS_FIJAS[i]));
  }
  for (int i = 0; i < numTarjetasLeidas; i++) {
    Serial.print(F("LIST:"));
    Serial.print(LECTOR_ID);
    Serial.print(':');
    Serial.println(leerTarjeta(i));
  }

  Serial.print(F("LIST:"));
  Serial.print(LECTOR_ID);
  Serial.println(F(":END"));
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

// STATS:L1:OK:120:ERR:2:INVALID:1
void enviarStatsLectora(const char* lector, const StatsLectora& s) {
  Serial.print(F("STATS:"));
  Serial.print(lector);
  Serial.print(F(":OK:"));
  Serial.print(s.ok);
  Serial.print(F(":ERR:"));
  Serial.print(s.errores);
  Serial.print(F(":INVALID:"));
  Serial.println(s.invalidos);
}

void enviarStats() {
  enviarStatsLectora("L1", statsL1);
#if NUM_LECTORAS == 2
  enviarStatsLectora("L2", statsL2);
#endif
}

void resetStats() {
  statsL1 = {0, 0, 0};
  statsL2 = {0, 0, 0};
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
// COMANDOS REMOTOS
//   CMD:ENROLL:5033541 / CMD:DELETE:5033541 / CMD:LIST / CMD:RESET
// ============================================================
void procesarComando(char* line) {
  char* token = strtok(line, ":");
  if (!token || strcmp(token, "CMD") != 0) return;

  char* cmd = strtok(NULL, ":");
  if (!cmd) return;

  if (strcmp(cmd, "ENROLL") == 0) {
    char* uidStr = strtok(NULL, ":");
    if (!uidStr) return;
    uint32_t uid = (uint32_t)atol(uidStr);

    if (tarjetaYaAlmacenada(uid))          { enviarEventoEnroll(uid, F("EXISTS")); return; }
    if (numTarjetasLeidas >= MAX_TARJETAS) { enviarEventoEnroll(uid, F("FULL"));   return; }
    almacenarTarjetaEnEEPROM(uid);
    enviarEventoEnroll(uid, F("OK"));
    return;
  }

  if (strcmp(cmd, "DELETE") == 0) {
    char* uidStr = strtok(NULL, ":");
    if (!uidStr) return;
    uint32_t uid = (uint32_t)atol(uidStr);

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

  if (strcmp(cmd, "LIST") == 0)  { enviarListaTarjetas(); return; }
  if (strcmp(cmd, "RESET") == 0) { borrarEEPROM(); enviarResetOk(); return; }

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
  if (tarjetaYaAlmacenada(numeroTarjeta)) {
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
  if (e1) { statsL1.errores++; enviarEventoErrorWiegand("L1", t1, b1); }
  if (e2) { statsL2.errores++; enviarEventoErrorWiegand("L2", t2, b2); }
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
int lecturaAnteriorPulsador1 = HIGH;
int estadoEstablePulsador1   = HIGH;

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
        abrirPuertaLocal();
        buzzerPlay(PAT_BUTTON);
        enviarEventoButton(true);
      }
    }
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
  Serial.println(MAX_TARJETAS);
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
