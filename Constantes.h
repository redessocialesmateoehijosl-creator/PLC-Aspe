#ifndef CONSTANTES_H
#define CONSTANTES_H

// ==========================================
//        CONFIGURACIÓN MQTT
// ==========================================
const char* MQTT_BROKER       = "85.208.22.48";
const int   MQTT_PORT         = 1883;
const char* MQTT_USER         = "admin";
const char* MQTT_PASS         = "admin";
const char* MQTT_ID_PLC       = "PLC_ASPE";

// Topics — datos de aplicación
const char* MQTT_TOPIC_IN     = "/34CDB000779C/toPLC";
const char* MQTT_TOPIC_OUT    = "/34CDB000779C/return";

// Topics — heartbeat
const char* MQTT_TOPIC_PING   = "/34CDB000779C/ping";   // ESP32 → Node-RED
const char* MQTT_TOPIC_PONG   = "/34CDB000779C/pong";   // Node-RED → ESP32

// --- CONFIGURACIÓN DE HEARTBEAT (Latido) ---
// Tiempos en modo REPOSO (Lento para no saturar la red)
#define HB_INTERVAL_IDLE    5000  // Enviar PING cada 5s
#define HB_TIMEOUT_IDLE     12000 // Perder conexión a los 12s

// Tiempos en modo MOVIMIENTO (Rápido por seguridad)
#define HB_INTERVAL_MOVE    1000  // Enviar PING cada 1s
#define HB_TIMEOUT_MOVE     3000  // Perder conexión a los 3s

// Mensajes de estado de conexión
const char* MSG_ONLINE              = "ONLINE";
const char* MSG_OFFLINE             = "OFFLINE";
const char* MSG_HEARTBEAT_LOST      = "status heartbeat_lost";
const char* MSG_HEARTBEAT_OK        = "status heartbeat_ok";

#endif