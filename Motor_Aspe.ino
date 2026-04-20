// ============================================================
//  Motor_Abaran.ino
//  Plataforma: Industrial Shields M-Duino 21+ (Arduino Mega)
//  Hardware:   1 Motor Techo (sin Roll-Ups)
//  Conexion:   Ethernet (cable) — sin WiFi
// ============================================================

#include <Arduino.h>
#include <RS485.h>
#include "Motor.h"   // incluye VFD_Control.h → VFD_Constantes.h → ModbusRTUMaster.h
#include "GrupoMotores.h"
#include "Red.h"
#include "Mqtt.h"
#include "Constantes.h"
#include "Comandos.h"
#include <IndustrialShields.h>

// ============================================================
//  MODBUS RS485 — Objeto RS485 nativo del M-Duino 21+
// ============================================================
ModbusRTUMaster modbusMaster(RS485);
VFDController   vfd(modbusMaster);

// ============================================================
//  DEFINICIÓN DE PINES  (misma nomenclatura Industrial Shields)
// ============================================================

// --- MOTOR TECHO ---
// Q0_0 y Q0_1 liberados — motor controlado por VFD Modbus (RS485)
#define PIN_M1_LUZ_VERDE    Q0_5   // LED verde
#define PIN_M1_LUZ_NARANJA  Q0_6   // LED naranja
#define PIN_M1_LUZ_ROJA     Q0_7   // LED rojo
#define PIN_M1_SETA         I0_3   // Seta emergencia (NC: LOW = pulsada)
#define PIN_M1_BTN_ABRIR    I0_1   // Botón abrir
#define PIN_M1_BTN_CERRAR   I0_0   // Botón cerrar
#define PIN_M1_BTN_CALIB    I0_2   // Botón calibrar
// Encoder — IMPORTANTE: I0_6 y I0_5 son los únicos pines con
// interrupción hardware en el M-Duino 21+ (pin 3 y pin 2 del Mega)
#define PIN_M1_ENC_A        I0_6   // Encoder canal A → INT1 (Mega pin 3)
#define PIN_M1_ENC_B        I0_5   // Encoder canal B → INT0 (Mega pin 2)

// EEPROM base address para este motor (0 = primer bloque disponible)
#define EEPROM_BASE_TECHO   0

// ============================================================
//  OBJETOS GLOBALES
// ============================================================

//String webLogBuffer = "";   // Buffer compartido con WiFiWebServer.h

Motor*       m1 = nullptr;
GrupoMotores grupo;

//WiFiWebServer wifiServer;   // Servidor web por Ethernet
MqttHandler   mqtt;

// ============================================================
//  LOGGING
// ============================================================
void printLog(String msg) {
  Serial.println(msg);
  //wifiServer.log(msg);
}

// ============================================================
//  CALLBACKS PARA COMANDOS EXTERNOS
//  (Necesitan ser funciones globales — no lambdas — en Arduino Mega)
// ============================================================
void onComandoWeb(String cmd) {
  ejecutarComando(cmd, &grupo, false);
}

void onComandoMqtt(String cmd) {
  printLog("MQTT cmd: " + cmd);
  ejecutarComando(cmd, &grupo, true);
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  printLog(F("--- SISTEMA: MOTOR TECHO - M-DUINO 21+ ---"));

  // 1. Servidor Web Ethernet
  //wifiServer.setup();
  //wifiServer.setCommandCallback(onComandoWeb);
  Red::setup();

  // 2. Modbus RS485 — inicializar ANTES del motor
  RS485.begin(BAUDRATE, HALFDUPLEX, SERIAL_CONFIG);  // 9600 baud, half-duplex, 8E1
  modbusMaster.begin(BAUDRATE);
  modbusMaster.setTimeout(300);
  vfd.setLogger(printLog);
  printLog(F("[VFD] Modbus RS485 iniciado."));

  // 3. Motor Techo
  //    Sin pines de relé — control por VFD Modbus
  m1 = new Motor(F("techo"),
                 PIN_M1_ENC_A,     PIN_M1_ENC_B,
                 PIN_M1_LUZ_ROJA,  PIN_M1_LUZ_VERDE, PIN_M1_LUZ_NARANJA,
                 PIN_M1_BTN_ABRIR, PIN_M1_BTN_CERRAR,
                 PIN_M1_BTN_CALIB, PIN_M1_SETA,
                 EEPROM_BASE_TECHO);
  m1->setVFD(&vfd);
  m1->setLogger(printLog);
  m1->begin();
  grupo.agregarMotor(m1);
  grupo.setLogger(printLog);

  // 3. MQTT
  mqtt.setup(MQTT_BROKER, MQTT_PORT, MQTT_USER, MQTT_PASS,
             MQTT_TOPIC_IN, MQTT_TOPIC_OUT, MQTT_ID_PLC);
  mqtt.setLogger(printLog);
  mqtt.vincularGrupo(&grupo);
  mqtt.setCommandCallback(onComandoMqtt);

  printLog(F("Inicializacion completa."));
}

// ============================================================
//  LOOP  (todo en un solo núcleo — no hay FreeRTOS en Mega)
// ============================================================

// Buffer para lectura serie NO BLOQUEANTE.
// Serial.readStringUntil() usa un timeout de 1 s por defecto,
// lo que paralizaría el loop durante un movimiento.
// Este acumulador carácter a carácter no bloquea nunca.
static String _serialBuf = "";

void loop() {
  unsigned long t0 = millis();

  // --- Red: inhibir operaciones DHCP mientras el motor se mueve ---
  // Ethernet.begin() y Ethernet.maintain() pueden bloquear el loop.
  // Cuando el motor está en movimiento se difieren hasta que pare.
  Red::inhibirDHCP(grupo.estaMoviendo());
  Red::loop();
  mqtt.loop();

  // --- VFD Modbus (heartbeat + respuestas) ---
  vfd.update();

  // --- Motor ---
  grupo.update();

  // --- Comandos por puerto serie (no bloqueante) ---
  // Acumula caracteres hasta recibir '\n' o '\r'; nunca bloquea.
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      _serialBuf.trim();
      if (_serialBuf.length() > 0) {
        String cmd = _serialBuf;
        _serialBuf = "";
        printLog("Serial cmd: " + cmd);
        if (cmd == "M") {
          mqtt.toggleVerbose();
        } else if (cmd == "V") {
          vfd.toggleVerbose();
        } else if (cmd == "F") {
          vfd.forzarFallaExterna();   // TEST: inyecta falla externa en el variador
        } else {
          ejecutarComando(cmd, &grupo, false);
        }
      }
    } else {
      _serialBuf += c;
      // Protección contra desbordamiento (línea sin '\n' muy larga)
      if (_serialBuf.length() > 64) _serialBuf = "";
    }
  }

  // --- Detector de atasco en el loop (>100 ms = problema) ---
  unsigned long dt = millis() - t0;
  if (dt > 100) {
    Serial.print(F("!!! ATASCO LOOP: "));
    Serial.print(dt);
    Serial.println(F(" ms"));
  }
}
