// ============================================================
//  VFD_Test.ino — Diagnóstico variador de frecuencia
//  Plataforma : Industrial Shields M-Duino 21+ (Arduino Mega)
//  VERSION: 5 — mapa de registros confirmado
//
//  MAPA CONFIRMADO (FC03, bloque desde 0x2100):
//    2100H → Código de falla activa (0 = sin falla)
//    2101H → Estado operativo (bits Run/Stop/Dir)
//    2102H → Frecuencia configurada (÷100 → Hz)
//    2103H → Frecuencia de salida   (÷100 → Hz)
//    2104H → Corriente de salida    (÷10  → A)
//    2105H → Voltaje de bus DC      (÷10  → V)
//    2106H → Voltaje de salida      (÷10  → V)
// ============================================================

#include <Arduino.h>
#include <RS485.h>
#include <ModbusRTUMaster.h>

// ── Conexión ─────────────────────────────────────────────────
const uint8_t  SLAVE_ID      = 1;
const uint32_t BAUDRATE      = 9600;
const uint16_t SERIAL_CONFIG = SERIAL_8E1;

// ── Registros de control (escritura) ────────────────────────
const uint16_t REG_CONTROL   = 0x2000;
const uint16_t REG_FREQ_SET  = 0x2001;
const uint16_t REG_FAULT_CMD = 0x2002;

const uint16_t CMD_STOP      = 0x0001;
const uint16_t CMD_RUN_FWD   = 0x0012;
const uint16_t CMD_RUN_REV   = 0x0022;
const uint16_t CMD_EXT_FAULT = 0x0001;
const uint16_t CMD_RESET_FLT = 0x0002;

// ── Bloque de lectura ────────────────────────────────────────
const uint16_t REG_READ_BASE = 0x2100;  // ← empieza en 2100H
const uint8_t  READ_COUNT    = 7;       // 2100H … 2106H

// Índices dentro del bloque
#define IDX_FALLA   0   // 2100H — código de falla (0 = OK)
#define IDX_ESTADO  1   // 2101H — bits Run/Stop/Dir
#define IDX_FREQ_CFG 2  // 2102H — frecuencia configurada
#define IDX_FREQ_SAL 3  // 2103H — frecuencia de salida
#define IDX_CORR    4   // 2104H — corriente
#define IDX_VBUS    5   // 2105H — voltaje bus DC
#define IDX_VSAL    6   // 2106H — voltaje salida

// ── Polling ──────────────────────────────────────────────────
const unsigned long POLL_MS = 500;

// ── Modbus ───────────────────────────────────────────────────
ModbusRTUMaster modbus(RS485);

// ── Estado ───────────────────────────────────────────────────
unsigned long lastPoll      = 0;
bool          esperandoResp = false;
bool          esperandoLect = false;
String        pendingLabel  = "";
bool          verbose       = true;
bool          pausado       = false;


// ============================================================
//  Nombre del código de falla (registro 2100H)
// ============================================================
const char* nombreFallo(uint16_t code) {
  switch (code) {
    case 0:  return "Sin falla";
    case 1:  return "Falla modulo IGBT";
    case 2:  return "Sobretension";
    case 3:  return "Sobrecalentamiento";
    case 4:  return "Sobrecarga inversor";
    case 5:  return "Sobrecarga motor (EOL2)";
    case 6:  return "Falla externa E-EF";
    case 10: return "Sobrecorriente ACEL";
    case 11: return "Sobrecorriente DECE";
    case 12: return "Sobrecorriente VEL_CONST";
    case 14: return "Subtension";
    case 15: return "Perdida fase entrada";
    case 16: return "Perdida fase salida";
    case 17: return "Error comunicacion E485";
    default: return "Cod desconocido";
  }
}

// ============================================================
//  Decodificación de bits de 2101H
// ============================================================
String decodificarBits(uint16_t reg) {
  String s = "";
  if (reg & 0x0002) s += "[Apagado]";
  if (reg & 0x0004) s += "[MarchaImpulsos]";
  if (reg & 0x0008) s += "[Avance]";
  if (reg & 0x0010) s += "[Reversa]";
  if (reg & 0x0100) s += "[CfgCom]";
  if (reg & 0x0400) s += "[CanalModbus]";
  if (reg & 0x1000) s += "[EnFuncionamiento]";
  return (s == "") ? "[SIN_BITS]" : s;
}

// ============================================================
//  Envío de escritura FC06
// ============================================================
void enviarCmd(uint16_t reg, uint16_t val, const String& label) {
  if (modbus.isWaitingResponse()) {
    Serial.println(F("[!] Bus ocupado, ignorando."));
    return;
  }
  Serial.print(F(">> WRITE ")); Serial.print(label);
  Serial.print(F(" reg=0x")); Serial.print(reg, HEX);
  Serial.print(F(" val=0x")); Serial.println(val, HEX);
  modbus.writeSingleRegister(SLAVE_ID, reg, val);
  esperandoResp = true;
  esperandoLect = false;
  pendingLabel  = label;
}

// ============================================================
//  Lanzar lectura del bloque 2100H-2106H
// ============================================================
void lanzarLectura() {
  if (modbus.isWaitingResponse()) return;
  modbus.readHoldingRegisters(SLAVE_ID, REG_READ_BASE, READ_COUNT);
  esperandoResp = true;
  esperandoLect = true;
  lastPoll      = millis();
}

// ============================================================
//  Procesar respuesta Modbus
// ============================================================
void procesarRespuesta() {
  if (!modbus.isWaitingResponse()) return;
  ModbusResponse res = modbus.available();
  if (!res) return;

  esperandoResp = false;

  // ── Escritura FC06 ────────────────────────────────────────
  if (!esperandoLect) {
    Serial.println();
    if (res.hasError()) {
      Serial.print(F("!!! CMD ERROR: ")); Serial.print(pendingLabel);
      Serial.print(F(" cod:")); Serial.println(res.getErrorCode());
    } else {
      Serial.print(F(">>> CMD OK: ")); Serial.println(pendingLabel);
    }
    Serial.println();
    return;
  }

  // ── Lectura del bloque ────────────────────────────────────
  if (res.hasError()) {
    Serial.print(F("[ERR RS485 cod:")); Serial.print(res.getErrorCode()); Serial.println(F("]"));
    return;
  }
  if (pausado) return;

  uint16_t r[READ_COUNT];
  for (int i = 0; i < READ_COUNT; i++) r[i] = res.getRegister(i);

  uint16_t faultCode = r[IDX_FALLA];
  uint16_t status    = r[IDX_ESTADO];

  // ── Estado operativo desde 2101H ─────────────────────────
  bool enMarcha = (status & 0x1000);
  bool avance   = (status & 0x0008);
  bool reversa  = (status & 0x0010);
  bool apagado  = (status & 0x0002);

  const char* est;
  if      (faultCode > 0)           est = "!!! FALLA !!!";
  else if (enMarcha && avance)       est = "MARCHA_FWD   ";
  else if (enMarcha && reversa)      est = "MARCHA_REV   ";
  else if (apagado)                  est = "PARADO       ";
  else                               est = "STANDBY      ";

  float freqSal = r[IDX_FREQ_SAL] / 100.0f;
  float corr    = r[IDX_CORR]     / 10.0f;
  float vBus    = r[IDX_VBUS]     / 10.0f;

  // ── Línea principal ───────────────────────────────────────
  Serial.print(F("["));
  Serial.print(est);
  Serial.print(F("] Fs:"));   Serial.print(freqSal, 1);
  Serial.print(F("Hz I:"));   Serial.print(corr, 1);
  Serial.print(F("A Vbus:")); Serial.print(vBus, 0);
  Serial.print(F("V"));

  if (faultCode > 0) {
    Serial.print(F("  *** ERR 2100h="));
    Serial.print(faultCode);
    Serial.print(F(" ["));
    Serial.print(nombreFallo(faultCode));
    Serial.print(F("] ***"));
  }
  Serial.println();

  // ── Verbose: hex crudo + bits ─────────────────────────────
  if (verbose) {
    Serial.print(F("  HEX> "));
    for (int i = 0; i < READ_COUNT; i++) {
      Serial.print(0x2100 + i, HEX);
      Serial.print(F(":"));
      Serial.print(r[i], HEX);
      if (i < READ_COUNT - 1) Serial.print(F(" | "));
    }
    Serial.println();
    Serial.print(F("  BITS 2101h> "));
    Serial.println(decodificarBits(status));
  }
}

// ============================================================
//  Menú
// ============================================================
void mostrarMenu() {
  Serial.println(F(""));
  Serial.println(F("=== COMANDOS ==="));
  Serial.println(F("  r  → Marcha FWD"));
  Serial.println(F("  b  → Marcha REV"));
  Serial.println(F("  s  → STOP"));
  Serial.println(F("  f  → Forzar Falla Externa"));
  Serial.println(F("  x  → RESET Falla"));
  Serial.println(F("  5 / 3 / 1  → Frecuencia 50 / 30 / 10 Hz"));
  Serial.println(F("  p  → Poll manual"));
  Serial.println(F("  v  → Toggle verbose"));
  Serial.println(F("  q  → Pausar / reanudar prints"));
  Serial.println(F("  ?  → Este menú"));
  Serial.println(F("================"));
  Serial.println(F("Bloque lectura: 2100H-2106H (7 regs)"));
  Serial.println(F("  2100H = cod falla | 2101H = estado bits"));
}

void procesarComandoSerie(String cmd) {
  cmd.trim();
  cmd.toLowerCase();

  if      (cmd == "r") { Serial.println(F("\n===== MARCHA FWD =====")); enviarCmd(REG_CONTROL, CMD_RUN_FWD, "RUN_FWD"); }
  else if (cmd == "b") { Serial.println(F("\n===== MARCHA REV =====")); enviarCmd(REG_CONTROL, CMD_RUN_REV, "RUN_REV"); }
  else if (cmd == "s") { Serial.println(F("\n===== STOP =====")); enviarCmd(REG_CONTROL, CMD_STOP, "STOP"); }
  else if (cmd == "f") { Serial.println(F("\n!!! FALLA EXTERNA !!!")); enviarCmd(REG_FAULT_CMD, CMD_EXT_FAULT, "EXT_FAULT"); }
  else if (cmd == "x") { Serial.println(F("\n===== RESET FALLA =====")); enviarCmd(REG_FAULT_CMD, CMD_RESET_FLT, "RESET_FAULT"); }
  else if (cmd == "5") { enviarCmd(REG_FREQ_SET, 10000, "FREQ_50Hz"); }
  else if (cmd == "3") { enviarCmd(REG_FREQ_SET, 6000,  "FREQ_30Hz"); }
  else if (cmd == "1") { enviarCmd(REG_FREQ_SET, 2000,  "FREQ_10Hz"); }
  else if (cmd == "p") { lanzarLectura(); }
  else if (cmd == "v") { verbose = !verbose; Serial.println(verbose ? F("Verbose ON") : F("Verbose OFF")); }
  else if (cmd == "q") { pausado = !pausado; Serial.println(pausado ? F("PAUSADO") : F("REANUDADO")); }
  else if (cmd == "?" || cmd == "h") { mostrarMenu(); }
  else { Serial.print(F("Desconocido: '")); Serial.print(cmd); Serial.println(F("'. Usa '?'")); }
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println(F("\n============================================"));
  Serial.println(F("  VFD_Test v5 — mapa de registros confirmado"));
  Serial.println(F("  2100H=falla | 2101H=estado | 2102-2106H=telemetria"));
  Serial.println(F("============================================"));
  RS485.begin(BAUDRATE, HALFDUPLEX, SERIAL_CONFIG);
  modbus.begin(BAUDRATE);
  modbus.setTimeout(300);
  Serial.println(F("Modbus RS485 listo (9600 8E1)."));
  mostrarMenu();
}

// ============================================================
//  LOOP
// ============================================================
static String _serialBuf = "";

void loop() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      _serialBuf.trim();
      if (_serialBuf.length() > 0) {
        procesarComandoSerie(_serialBuf);
        _serialBuf = "";
      }
    } else {
      _serialBuf += c;
      if (_serialBuf.length() > 32) _serialBuf = "";
    }
  }

  procesarRespuesta();

  if (!modbus.isWaitingResponse() && (millis() - lastPoll >= POLL_MS)) {
    lanzarLectura();
  }
}
