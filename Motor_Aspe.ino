// ============================================================
//  Motor_Aspe.ino  —  FreeRTOS edition
//  Plataforma: Industrial Shields M-Duino 21+ (Arduino Mega / ATmega2560)
//
//  Arquitectura FreeRTOS:
//
//   setup() → crea taskStartup → scheduler arranca → taskStartup inicializa
//             todo el hardware (aquí delay() ya funciona) → crea taskMotor
//             y taskNetwork → se borra a sí misma.
//
//   taskMotor   (prioridad 2, cada 5 ms)
//     • emergencyStopRequest, cmdPendiente, vfd, grupo, serie
//
//   taskNetwork (prioridad 1, continua)
//     • Red::loop(), mqtt.loop()
//     • Bloqueos TCP aquí NO afectan al motor
//
//  NOTA sobre delay() y FreeRTOS en AVR:
//   Cuando se incluye Arduino_FreeRTOS, delay() llama internamente a
//   vTaskDelay(). Si delay() se invoca ANTES de que el scheduler arranque
//   (p.ej. en setup()), el MCU se cuelga sin output.
//   Solución: setup() NO llama a delay() — lo hace taskStartup, donde el
//   scheduler ya está en marcha y delay() funciona correctamente.
// ============================================================

#include <Arduino.h>
#include <Arduino_FreeRTOS.h>  // Gestor de Librerías → "FreeRTOS" (Richard Barry)
#include <semphr.h>
#include <RS485.h>
#include "Motor.h"
#include "GrupoMotores.h"
#include "Red.h"
#include "Mqtt.h"
#include "Constantes.h"
#include "Comandos.h"
#include "SharedState.h"
#include <IndustrialShields.h>

// ============================================================
//  MODBUS RS485
// ============================================================
ModbusRTUMaster modbusMaster(RS485);
VFDController   vfd(modbusMaster);

// ============================================================
//  PINES
// ============================================================
#define PIN_M1_LUZ_VERDE    Q0_5
#define PIN_M1_LUZ_NARANJA  Q0_6
#define PIN_M1_LUZ_ROJA     Q0_7
#define PIN_M1_SETA         I0_3
#define PIN_M1_BTN_ABRIR    I0_1
#define PIN_M1_BTN_CERRAR   I0_0
#define PIN_M1_BTN_CALIB    I0_2
#define PIN_M1_ENC_A        I0_6
#define PIN_M1_ENC_B        I0_5
#define EEPROM_BASE_TECHO   0

// ============================================================
//  OBJETOS GLOBALES
// ============================================================
Motor*       m1 = nullptr;
GrupoMotores grupo;
MqttHandler  mqtt;

// ============================================================
//  LOGGING
//  Sin mutex: con el tick rate bajo de la librería Richard Barry,
//  xSemaphoreTake puede congelar el sistema antes del primer task.
//  En AVR el TX serie usa un buffer de interrupción — dos tasks
//  concurrentes solo intercalan líneas, sin corrupción de datos.
// ============================================================
void printLog(String msg) {
  Serial.println(msg);
}

// ============================================================
//  CALLBACKS
// ============================================================
void onComandoMqtt(String cmd) {
  printLog("MQTT cmd: " + cmd);
  if (!cmdPendiente) {
    cmd.toCharArray((char*)cmdBuffer, SHARED_CMD_MAX);
    cmdEsRemoto  = true;
    cmdPendiente = true;
  } else {
    printLog(F("[WARN] Cmd MQTT descartado (slot ocupado)"));
  }
}

// ============================================================
//  TASK: ARRANQUE  (prioridad 3 — máxima, se borra al acabar)
//
//  Aquí se ejecuta toda la inicialización que usa delay()
//  internamente (Ethernet DHCP, RS485, EEPROM…).
//  Cuando el scheduler está activo, delay() → vTaskDelay()
//  y cede correctamente la CPU en lugar de colgar.
// ============================================================
void taskStartup(void* pvParameters) {

  vTaskDelay(pdMS_TO_TICKS(200));   // pequeña pausa para estabilizar el serial

  Serial.println(F("--- SISTEMA: MOTOR TECHO - M-DUINO 21+ (FreeRTOS) ---"));

  // 1. Red Ethernet (usa delay() internamente en DHCP — OK aquí)
  Red::setup();

  // 2. Modbus RS485
  RS485.begin(BAUDRATE, HALFDUPLEX, SERIAL_CONFIG);
  modbusMaster.begin(BAUDRATE);
  modbusMaster.setTimeout(300);
  vfd.setLogger(printLog);
  Serial.println(F("[VFD] Modbus RS485 iniciado."));

  // 3. Motor Techo
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

  // 4. MQTT
  mqtt.setup(MQTT_BROKER, MQTT_PORT, MQTT_USER, MQTT_PASS,
             MQTT_TOPIC_IN, MQTT_TOPIC_OUT, MQTT_ID_PLC);
  mqtt.setLogger(printLog);
  mqtt.vincularGrupo(&grupo);
  mqtt.setCommandCallback(onComandoMqtt);

  // 5. Crear tasks operacionales
  //    Stack en bytes (StackType_t=uint8_t en AVR FreeRTOS feilipu).
  //    taskMotor  512 bytes — chain ejecutarComando→debug→printLog + VFD.
  //    taskNetwork 420 bytes — mqtt.loop() procesa mensajes MQTT entrantes
  //    creando objetos String (_handleMessage, vigilarGrupo), más
  //    Red::loop() → Ethernet.maintain(). 300 era insuficiente y causaba
  //    desbordamiento silencioso que reseteaba el sistema ~15 s después
  //    de la primera conexión MQTT.
  BaseType_t okMotor = xTaskCreate(taskMotor,   "Motor", 512, NULL, 1, NULL);
  BaseType_t okNet   = xTaskCreate(taskNetwork, "Net",   420, NULL, 1, NULL);

  if (okMotor != pdPASS || okNet != pdPASS) {
    Serial.println(F("!!! ERROR FATAL: FreeRTOS heap insuficiente para tasks."));
    Serial.println(F("    Aumentar configTOTAL_HEAP_SIZE en FreeRTOSConfig.h"));
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
  }

  Serial.println(F("[FREERTOS] Tasks Motor y Net activos. Startup finalizado."));

  // Borrar esta tarea — ya no es necesaria
  vTaskDelete(NULL);
}

// ============================================================
//  TASK: MOTOR  (prioridad 1 — igual que taskNetwork)
//
//  Misma prioridad que taskNetwork → round-robin garantizado:
//  el scheduler alterna entre ambas tasks en cada tick,
//  independientemente del tick rate de la librería.
//
//  vTaskDelay(1) cede la CPU al menos 1 tick en cada ciclo.
//  Con tick rate 62 Hz → 1 tick = 16 ms (motor update cada ~16 ms).
//  Con tick rate 1000 Hz → 1 tick =  1 ms (motor update cada ~1 ms).
//  En ambos casos el encoder captura pulsos por ISR (no se pierden),
//  y la respuesta de botones/VFD a 16 ms es más que suficiente.
// ============================================================
static String _serialBuf = "";

void taskMotor(void* pvParameters) {
  // Delay de estabilización: el pin de la seta (NC, INPUT) puede leer
  // LOW durante los primeros ms de boot por transitorios en el bus I/O.
  // 500 ms es suficiente para que cualquier glitch se disipe antes de
  // que gestionarEntradas() haga la primera lectura real.
  vTaskDelay(pdMS_TO_TICKS(500));

  for (;;) {

    // Parada de emergencia desde taskNetwork (heartbeat expirado)
    if (emergencyStopRequest) {
      grupo.parar();
      emergencyStopRequest = false;
    }

    // Comando MQTT pendiente
    if (cmdPendiente) {
      String cmd  = String((const char*)cmdBuffer);
      bool remoto = (bool)cmdEsRemoto;
      cmdPendiente = false;
      ejecutarComando(cmd, &grupo, remoto);
    }

    // VFD Modbus
    vfd.update();

    // Motores (encoder, botones, LEDs, watchdog)
    grupo.update();

    // Comandos serie (no bloqueante)
    while (Serial.available()) {
      char c = (char)Serial.read();
      if (c == '\n' || c == '\r') {
        _serialBuf.trim();
        if (_serialBuf.length() > 0) {
          String cmd = _serialBuf;
          _serialBuf = "";
          printLog("Serial cmd: " + cmd);
          if      (cmd == "M") mqtt.toggleVerbose();
          else if (cmd == "R") mqtt.forzarReconexion();
          else if (cmd == "V") vfd.toggleVerbose();
          else if (cmd == "F") vfd.forzarFallaExterna();
          else                 ejecutarComando(cmd, &grupo, false);
        }
      } else {
        _serialBuf += c;
        if (_serialBuf.length() > 64) _serialBuf = "";
      }
    }

    // Cede la CPU al menos 1 tick en cada ciclo.
    // Con round-robin (misma prioridad), taskNetwork alternará aquí.
    vTaskDelay(1);
  }
}

// ============================================================
//  TASK: RED / MQTT  (prioridad 1, continua)
// ============================================================
void taskNetwork(void* pvParameters) {
  for (;;) {
    Red::inhibirDHCP(grupo.estaMoviendo());
    Red::loop();
    mqtt.loop();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

// ============================================================
//  SETUP  — mínimo, SIN delay(), SIN hardware init
//  Solo crea taskStartup y deja que el scheduler tome el control.
// ============================================================
void setup() {
  Serial.begin(115200);
  // NO delay() aquí — el scheduler aún no corre y delay() colgaría.
  // Todo lo que necesite delay() va dentro de taskStartup.

  // Stack en bytes (StackType_t=uint8_t en AVR FreeRTOS feilipu).
  // 640 bytes para taskStartup: más margen que 512 para cubrir la
  // cadena profunda Red::setup()→Ethernet.begin()→DHCP sin overflow.
  // taskStartup corre con prioridad máxima: será lo primero que
  // ejecute el scheduler al arrancar.
  xTaskCreate(taskStartup, "Boot", 640, NULL, 3, NULL);

  // El scheduler arranca automáticamente al salir de setup().
  // loop() nunca se ejecuta.
}

// ============================================================
//  LOOP — no se usa con FreeRTOS
// ============================================================
void loop() {}
