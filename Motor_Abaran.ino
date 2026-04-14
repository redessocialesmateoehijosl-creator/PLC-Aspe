#include <dummy.h>
#include <Arduino.h>
#include "Motor.h"
#include "MotorRollUp.h"
#include "WiFiWebServer.h"
#include "Mqtt.h"
#include "Constantes.h"
#include "Comandos.h"

// ==========================================
//           DEFINICIÓN DE PINES (PLC)
// ==========================================

// --- MOTOR TECHO ---
#define PIN_M1_MOT_A        Q0_0   // Relé abrir
#define PIN_M1_MOT_C        Q0_1   // Relé cerrar
#define PIN_M1_LUZ_VERDE    Q1_1   // LED verde
#define PIN_M1_LUZ_NARANJA  Q1_4   // LED naranja
#define PIN_M1_LUZ_ROJA     Q2_7   // LED rojo
#define PIN_M1_SETA         I0_0   // Emergencia (Compartida)
#define PIN_M1_BTN_ABRIR    I0_2   // Botón abrir
#define PIN_M1_BTN_CERRAR   I0_1   // Botón cerrar
#define PIN_M1_BTN_CALIB    I1_0   // Botón calibrar
#define PIN_M1_ENC_A        I0_6   // Encoder A
#define PIN_M1_ENC_B        I0_5   // Encoder B

// --- ROLL-UP 1 ---
#define PIN_RU1_MOT_ARR     Q0_2   // Relé subir
#define PIN_RU1_MOT_BAJ     Q0_3   // Relé bajar
#define PIN_RU1_LED_ARR     Q1_5   // LED arriba
#define PIN_RU1_LED_BAJ     Q1_0   // LED abajo
#define PIN_RU1_MARCHA_ARR  I0_3   // Botón subir
#define PIN_RU1_MARCHA_BAJ  I1_2   // Botón bajar
#define PIN_RU1_FC_ARR      I1_4   // Final carrera arriba
#define PIN_RU1_FC_BAJ      I2_2   // Final carrera abajo

// --- ROLL-UP 2 ---
#define PIN_RU2_MOT_ARR     Q0_4   // Relé subir
#define PIN_RU2_MOT_BAJ     Q0_5   // Relé bajar
#define PIN_RU2_LED_ARR     Q1_6   // LED arriba
#define PIN_RU2_LED_BAJ     Q1_2   // LED abajo
#define PIN_RU2_MARCHA_ARR  I0_4   // Botón subir
#define PIN_RU2_MARCHA_BAJ  I1_3   // Botón bajar
#define PIN_RU2_FC_ARR      I2_1   // Final carrera arriba
#define PIN_RU2_FC_BAJ      I2_3   // Final carrera abajo

// --- ROLL-UP 3 ---
#define PIN_RU3_MOT_ARR     Q0_6   // Relé subir
#define PIN_RU3_MOT_BAJ     Q0_7   // Relé bajar
#define PIN_RU3_LED_ARR     Q1_7   // LED arriba
#define PIN_RU3_LED_BAJ     Q1_3   // LED abajo
#define PIN_RU3_MARCHA_ARR  I1_1   // Botón subir
#define PIN_RU3_MARCHA_BAJ  I2_0   // Botón bajar
#define PIN_RU3_FC_ARR      I2_4   // Final carrera arriba
#define PIN_RU3_FC_BAJ      I1_5   // Final carrera abajo


// ==========================================
//             OBJETOS GLOBALES
// ==========================================

String webLogBuffer = "";
Motor* m1 = nullptr;
MotorRollUp* ru1 = nullptr;
MotorRollUp* ru2 = nullptr;
MotorRollUp* ru3 = nullptr;
GrupoMotores grupo;

WiFiWebServer wifiServer("Cudy-26A7", "35037910", "esp32");
MqttHandler   mqtt;
TaskHandle_t TareaRed;

// ==========================================
//      TAREA RED (NÚCLEO 0)
// ==========================================
void bucleRed(void * parameter) {
  for(;;) {
    wifiServer.loop();
    mqtt.loop();
    
    // Vigilancia del Techo
    mqtt.vigilarGrupo(); 

    // Vigilancia individual de cada Roll-Up (Prefijos ru1_, ru2_, ru3_)
    if (ru1) mqtt.vigilarRollUp(ru1);
    if (ru2) mqtt.vigilarRollUp(ru2);
    if (ru3) mqtt.vigilarRollUp(ru3);

    vTaskDelay(10 / portTICK_PERIOD_MS); 
  }
}

void printLog(String msg) {
  Serial.println(msg);
  wifiServer.log(msg);
}

// ==========================================
//                   SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  printLog("--- SISTEMA FULL: 1 TECHO + 3 ROLL-UPS ---");

  wifiServer.setup();
  //wifiServer.setCommandCallback([](String cmd) { ejecutarComando(cmd, &grupo, ru1, false); });
  wifiServer.setCommandCallback([](String cmd) { 
    ejecutarComando(cmd, &grupo, ru1, ru2, ru3, false); });

  // 1. Instancia Techo
  m1 = new Motor("techo", PIN_M1_MOT_A, PIN_M1_MOT_C, PIN_M1_ENC_A, PIN_M1_ENC_B,
                 PIN_M1_LUZ_ROJA, PIN_M1_LUZ_VERDE, PIN_M1_LUZ_NARANJA,
                 PIN_M1_BTN_ABRIR, PIN_M1_BTN_CERRAR, PIN_M1_BTN_CALIB, PIN_M1_SETA);
  m1->setLogger(printLog);
  m1->begin();
  grupo.agregarMotor(m1);
  grupo.setLogger(printLog);

  // 2. Instancias Roll-Ups
  ru1 = new MotorRollUp("ru1", PIN_RU1_MARCHA_ARR, PIN_RU1_MARCHA_BAJ, PIN_RU1_FC_ARR, PIN_RU1_FC_BAJ, PIN_M1_SETA, PIN_RU1_MOT_ARR, PIN_RU1_MOT_BAJ, PIN_RU1_LED_ARR, PIN_RU1_LED_BAJ);
  ru1->setLogger(printLog);
  ru1->begin();

  ru2 = new MotorRollUp("ru2", PIN_RU2_MARCHA_ARR, PIN_RU2_MARCHA_BAJ, PIN_RU2_FC_ARR, PIN_RU2_FC_BAJ, PIN_M1_SETA, PIN_RU2_MOT_ARR, PIN_RU2_MOT_BAJ, PIN_RU2_LED_ARR, PIN_RU2_LED_BAJ);
  ru2->setLogger(printLog);
  ru2->begin();

  ru3 = new MotorRollUp("ru3", PIN_RU3_MARCHA_ARR, PIN_RU3_MARCHA_BAJ, PIN_RU3_FC_ARR, PIN_RU3_FC_BAJ, PIN_M1_SETA, PIN_RU3_MOT_ARR, PIN_RU3_MOT_BAJ, PIN_RU3_LED_ARR, PIN_RU3_LED_BAJ);
  ru3->setLogger(printLog);
  ru3->begin();

  // 3. MQTT
  mqtt.setup(MQTT_BROKER, MQTT_PORT, MQTT_USER, MQTT_PASS, MQTT_TOPIC_IN, MQTT_TOPIC_OUT, MQTT_ID_PLC);
  mqtt.setLogger(printLog);
  mqtt.vincularGrupo(&grupo);
  /*mqtt.setCommandCallback([](String cmd) {
    printLog("MQTT cmd: " + cmd);
    // Nota: El comando se procesa pasando ru1 como base, 
    // pero Comandos.h debe decidir si el comando afecta a ru1, ru2 o ru3.
    ejecutarComando(cmd, &grupo, ru1, true); 
  });*/
  mqtt.setCommandCallback([](String cmd) {
    ejecutarComando(cmd, &grupo, ru1, ru2, ru3, true);
  });

  xTaskCreatePinnedToCore(bucleRed, "TareaRed", 8192, NULL, 1, &TareaRed, 0);
}

// ==========================================
//                   LOOP
// ==========================================
void loop() {
  unsigned long inicioLoop = millis();

  grupo.update();
  if (ru1) ru1->update();
  if (ru2) ru2->update();
  if (ru3) ru3->update();

  // --- COMANDOS POR SERIAL ---
  if (Serial.available()) {
    // Leemos toda la línea hasta el "Intro"
    String cmd = Serial.readStringUntil('\n');
    cmd.trim(); // Limpiamos espacios o restos de \r

    if (cmd.length() > 0) {
      printLog("Serial cmd: " + cmd);
      
      // Enviamos el string completo ("u", "u2", "m50", etc.)
      // Nota: esRemoto = false porque es por cable serie
      ejecutarComando(cmd, &grupo, ru1, ru2, ru3, false);
    }
  }
  
  unsigned long tiempoVuelta = millis() - inicioLoop;
  if (tiempoVuelta > 50) {
    Serial.println("!!! ATASCO DETECTADO !!! Loop: " + String(tiempoVuelta) + " ms");
  }
}
/*
#include <dummy.h>

#include <Arduino.h>
#include "Motor.h"
#include "MotorRollUp.h"
#include "WiFiWebServer.h"
#include "Mqtt.h"
#include "Constantes.h"
#include "Comandos.h"

// ==========================================
//           DEFINICIÓN DE PINES
//        (Todos confirmados con hardware)
//
//   NOTA: Configuración de PRUEBA.
//   Solo 1 motor techo + 1 roll-up.
// ==========================================

// --- MOTOR TECHO ---
#define PIN_M1_MOT_A        Q0_0   // Relé abrir
#define PIN_M1_MOT_C        Q0_1   // Relé cerrar
#define PIN_M1_LUZ_VERDE    Q1_1   // LED verde
#define PIN_M1_LUZ_NARANJA  Q1_4   // LED naranja
#define PIN_M1_LUZ_ROJA     Q2_7   // LED rojo
#define PIN_M1_SETA         I0_0   // Emergencia
#define PIN_M1_BTN_ABRIR    I0_2   // Botón abrir
#define PIN_M1_BTN_CERRAR   I0_1   // Botón cerrar
#define PIN_M1_BTN_CALIB    I1_0   // Botón calibrar
#define PIN_M1_ENC_A        I0_6   // Encoder A
#define PIN_M1_ENC_B        I0_5   // Encoder B

// --- ROLL-UP 1 ---
#define PIN_RU1_MOT_ARR     Q0_2   // Relé subir
#define PIN_RU1_MOT_BAJ     Q0_3   // Relé bajar
#define PIN_RU1_LED_ARR     Q1_5   // LED arribai
#define PIN_RU1_LED_BAJ     Q1_0   // LED abajo
#define PIN_RU1_MARCHA_ARR  I0_3   // Botón subir
#define PIN_RU1_MARCHA_BAJ  I1_2   // Botón bajar
#define PIN_RU1_FC_ARR      I1_4   // Final carrera arriba
#define PIN_RU1_FC_BAJ      I2_2   // Final carrera abajo




// ==========================================
//             OBJETOS GLOBALES
// ==========================================

String webLogBuffer = "";
Motor*       m1  = nullptr;
MotorRollUp* ru1 = nullptr;
GrupoMotores grupo;

WiFiWebServer wifiServer("MOVISTAR_D679", "7R8w77PGSkqcdK7", "esp32");
MqttHandler   mqtt;

TaskHandle_t TareaRed;


// ==========================================
//      TAREA RED (NÚCLEO 0)
// ==========================================
void bucleRed(void * parameter) {
  for(;;) {
    wifiServer.loop();
    mqtt.loop();
    
    // CRÍTICO: Dale 10ms de respiro para que el Watchdog del ESP32 no reinicie la placa
    vTaskDelay(10 / portTICK_PERIOD_MS); 
  }
}


// ==========================================
//               LOGGING
// ==========================================
void printLog(String msg) {
  Serial.println(msg);
  wifiServer.log(msg);
}


// ==========================================
//                  SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  printLog("--- PRUEBA: 1 TECHO + 1 ROLL-UP ---");

  // 1. Servidor Web
  wifiServer.setup();
  wifiServer.setCommandCallback([](String cmd) { ejecutarComando(cmd, &grupo, ru1, false); });

  // 2. Motor techo
  m1 = new Motor("techo",
                 PIN_M1_MOT_A,    PIN_M1_MOT_C,
                 PIN_M1_ENC_A,    PIN_M1_ENC_B,
                 PIN_M1_LUZ_ROJA, PIN_M1_LUZ_VERDE, PIN_M1_LUZ_NARANJA,
                 PIN_M1_BTN_ABRIR, PIN_M1_BTN_CERRAR, PIN_M1_BTN_CALIB, PIN_M1_SETA);
  m1->setLogger(printLog);
  m1->begin();

  // --- CONFIGURACIÓN DEL GRUPO ---
  grupo.agregarMotor(m1); // Metemos el motor de techo en el grupo
  grupo.setLogger(printLog);
  
  // Si tienes pines para botones que muevan TODO el grupo, los pones aquí:
  // grupo.setupBotonera(PIN_G_ABRIR, PIN_G_CERRAR, ...);

  // 3. Roll-up 1
  ru1 = new MotorRollUp("ru1",
                        PIN_RU1_MARCHA_ARR, PIN_RU1_MARCHA_BAJ,
                        PIN_RU1_FC_ARR,     PIN_RU1_FC_BAJ,
                        PIN_M1_SETA,
                        PIN_RU1_MOT_ARR,    PIN_RU1_MOT_BAJ,
                        PIN_RU1_LED_ARR,    PIN_RU1_LED_BAJ);
  ru1->setLogger(printLog);
  ru1->begin();

  // 4. MQTT
  mqtt.setup(MQTT_BROKER, MQTT_PORT, MQTT_USER, MQTT_PASS,
             MQTT_TOPIC_IN, MQTT_TOPIC_OUT, MQTT_ID_PLC);
  mqtt.vincularGrupo(&grupo);
  mqtt.vincularRollUp(ru1);
  mqtt.setCommandCallback([](String cmd) {
    printLog("MQTT cmd: " + cmd);
    ejecutarComando(cmd, &grupo, ru1, true);
  });


  printLog("Iniciando Tarea de Red en Núcleo 0...");
  xTaskCreatePinnedToCore(
    bucleRed,      // Función de la tarea
    "TareaRed",    // Nombre
    8192,          // Tamaño de pila (RAM)
    NULL,          // Parámetros
    1,             // Prioridad
    &TareaRed,     // Manejador
    0              // NÚCLEO 0
  );
}


// ==========================================
//                  LOOP
// ==========================================
void loop() {

  unsigned long inicioLoop = millis();


  //wifiServer.loop();
  //mqtt.loop();

  grupo.update();
  if (ru1) ru1->update();

  // Comandos manuales por Serial
  if (Serial.available()) {
    char c = Serial.read();
    if (c != '\n' && c != '\r') {
      if (c == 'm' || c == 'A') ejecutarComando(String(c) + Serial.readStringUntil('\n'), &grupo, ru1);
      else ejecutarComando(String(c), &grupo, ru1, false);
    }
  }
  
  // Paramos el cronómetro
  unsigned long finLoop = millis();
  unsigned long tiempoVuelta = finLoop - inicioLoop;

  // Si la vuelta tarda más de 50 milisegundos, lanzamos la alerta
  if (tiempoVuelta > 50) {
    Serial.println("!!! ATASCO DETECTADO !!! El loop tardó: " + String(tiempoVuelta) + " ms");
  }
}*/
