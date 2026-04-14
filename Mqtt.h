/*#ifndef MQTT_H
#define MQTT_H

#include <Ethernet.h>
#include <PubSubClient.h>
#include <functional>
#include "Constantes.h"
#include "GrupoMotores.h"
#include "MotorRollUp.h"

class MqttHandler {
  private:
    EthernetClient ethClient;
    PubSubClient client;
    
    GrupoMotores* _grupoVigilado = nullptr;
    MotorRollUp* _rollUpVigilado = nullptr;

    // Configuración
    const char* _server; int _port;
    const char* _user; const char* _pass;
    const char* _topicIn; const char* _topicOut;
    const char* _deviceId;

    // Variables de control interno
    unsigned long lastReconnectAttempt = 0;
    unsigned long lastCheckTime = 0;
    String lastEstadoStr = "";
    int lastPorcentaje = -1;
    unsigned long lastCheckTimeRU = 0;
    String lastEstadoRU  = "";
    int lastPorcentajeRU = -1;
    unsigned long lastPingTime = 0;
    unsigned long lastPongTime = 0;

    std::function<void(String)> commandCallback;

    // --- LOGGER PERSONALIZADO (WiFi + Serial) ---
    typedef std::function<void(String)> LogCallback;
    LogCallback externalLog = nullptr;

    void debug(String msg) {
      if (externalLog) {
          externalLog("[MQTT] " + msg); 
      }
    }

    void enviarPing() {
      bool estaMoviendo = (_grupoVigilado && (_grupoVigilado->getEstadoString() == MSG_ABRIENDO || _grupoVigilado->getEstadoString() == MSG_CERRANDO));
      bool esOrdenRemota = (_grupoVigilado && _grupoVigilado->hayMovimientoPorRed());

      static bool estabaMoviendo = false;
      if (estaMoviendo && !estabaMoviendo) {
          lastPongTime = millis(); 
          lastPingTime = 0;        
      }
      estabaMoviendo = estaMoviendo;
      
      unsigned long intervalo = estaMoviendo ? HB_INTERVAL_MOVE : HB_INTERVAL_IDLE;
      unsigned long timeout   = estaMoviendo ? HB_TIMEOUT_MOVE  : HB_TIMEOUT_IDLE;
      unsigned long now = millis();

      if (now - lastPingTime > intervalo) {
        lastPingTime = now;
        if (client.connected()) {
            client.publish(MQTT_TOPIC_PING, "1");
        }
      }

      if (now - lastPongTime > timeout) {
          if (esOrdenRemota) {
              _grupoVigilado->parar();
              debug("!!! SEGURIDAD: Timeout Heartbeat. PARADA DE EMERGENCIA !!!");
          }
          if (client.connected()) {
              debug("Cerrando conexion por falta de PONG.");
              client.disconnect();
          }
          lastPongTime = now;
      }
    }

  public:
    MqttHandler() : client(ethClient) {}

    // Función para conectar el log desde el .ino
    void setLogger(LogCallback callback) { externalLog = callback; }

    void vigilarGrupo() { 
      if (_grupoVigilado == nullptr) return;
      if (millis() - lastCheckTime > 100) {
         lastCheckTime = millis();
         bool forzarUpdate = _grupoVigilado->checkMqttRequest();
         String estadoActual = _grupoVigilado->getEstadoString();
         if (forzarUpdate || estadoActual != lastEstadoStr) {
            publish(estadoActual); 
            lastEstadoStr = estadoActual;
            lastPorcentaje = -1;
         }
         int porcActual = _grupoVigilado->getPorcentajeEntero();
         if (porcActual != lastPorcentaje) {
            publish(String(PREFIJO_POS) + String(porcActual));
            lastPorcentaje = porcActual;
         }
      }
    }

    void vigilarRollUp(MotorRollUp* ru) {
      if (ru == nullptr) return;
      String estadoActual = ru->getEstadoString();
      if (ru->checkMqttRequest()) {
          publish(estadoActual);
          debug("Publicado RollUp: " + estadoActual);
      }
    }

    void vigilarRollUp() {
      if (_rollUpVigilado) vigilarRollUp(_rollUpVigilado);
    }
  
    void setup(const char* server, int port, const char* user, const char* pass, 
               const char* topicIn, const char* topicOut, const char* deviceId) {
      _server = server; _port = port; _user = user; _pass = pass;
      _topicIn = topicIn; _topicOut = topicOut; _deviceId = deviceId;

      client.setServer(_server, _port);
      
      client.setCallback([this](char* topic, byte* payload, unsigned int length) {
        String topicStr = String(topic);
        String msg = "";
        for (int i = 0; i < length; i++) msg += (char)payload[i];
        msg.trim();

        if (topicStr == String(MQTT_TOPIC_PONG)) {
          lastPongTime = millis();
          return; 
        } 

        if (topicStr == String(_topicIn)) {
          lastPongTime = millis();
          if (msg == "?") {
            lastEstadoStr = ""; lastPorcentaje = -1;
            lastEstadoRU = ""; lastPorcentajeRU = -1;
            debug("Peticion de estado (?) recibida. Forzando actualizacion...");
          } 
          if (commandCallback) commandCallback(msg);
        }
      });
      debug("Configurado. Broker: " + String(_server));
    }

    void vincularGrupo(GrupoMotores* grupo) { _grupoVigilado = grupo; }
    void vincularRollUp(MotorRollUp* ru) { _rollUpVigilado = ru; }
    void setCommandCallback(std::function<void(String)> callback) { commandCallback = callback; }

    void loop() {
      if (!client.connected()) {
        unsigned long now = millis();
        bool motorMoviendo = false;
        if (_grupoVigilado != nullptr) {
            String estado = _grupoVigilado->getEstadoString();
            if (estado == MSG_ABRIENDO || estado == MSG_CERRANDO || estado == MSG_CALIBRANDO) {
                motorMoviendo = true;
            }
        }

        if (now - lastReconnectAttempt > 5000) {
          lastReconnectAttempt = now;
          if (!motorMoviendo) {
              reconnect(); 
          } else {
              debug("Reconexión pospuesta: Motor en movimiento.");
          }
        }
      } else {
        client.loop();
        vigilarGrupo();
        vigilarRollUp();
      }
      enviarPing();
    }

    bool reconnect() {
      if (Ethernet.linkStatus() != LinkON) {
          debug("Error: Cable Ethernet desconectado.");
          return false;
      }

      debug("Intentando conectar al Broker...");
      if (client.connect(_deviceId, _user, _pass, _topicOut, 1, true, MSG_OFFLINE)) {
          client.subscribe(_topicIn);
          client.subscribe(MQTT_TOPIC_PONG);
          client.publish(_topicOut, MSG_ONLINE);
          lastPongTime = millis();
          debug("¡Conectado! Sistema Online.");
          return true;
      }
      debug("Fallo en la conexión MQTT.");
      return false;
    }

    void publish(String msg) {
      if (client.connected()) {
        client.publish(_topicOut, msg.c_str());
      }
    }
};

#endif*/





#ifndef MQTT_H
#define MQTT_H

#include <WiFi.h>          // CAMBIADO: Usamos WiFi en lugar de Ethernet
#include <PubSubClient.h>
#include <functional>
#include "Constantes.h"
#include "GrupoMotores.h"
#include "MotorRollUp.h"

class MqttHandler {
  private:
    WiFiClient espClient;  // CAMBIADO: Cliente WiFi
    PubSubClient client;
    
    GrupoMotores* _grupoVigilado = nullptr;
    MotorRollUp* _rollUpVigilado = nullptr;

    // Configuración
    const char* _server; int _port;
    const char* _user; const char* _pass;
    const char* _topicIn; const char* _topicOut;
    const char* _deviceId;

    // Variables de control interno
    unsigned long lastReconnectAttempt = 0;
    unsigned long lastCheckTime = 0;
    String lastEstadoStr = "";
    int lastPorcentaje = -1;
    unsigned long lastPingTime = 0;
    unsigned long lastPongTime = 0;

    std::function<void(String)> commandCallback;

    typedef std::function<void(String)> LogCallback;
    LogCallback externalLog = nullptr;

    void debug(String msg) {
      if (externalLog) externalLog("[MQTT] " + msg); 
    }

    void enviarPing() {
      bool estaMoviendo = (_grupoVigilado && (_grupoVigilado->getEstadoString() == MSG_ABRIENDO || _grupoVigilado->getEstadoString() == MSG_CERRANDO));
      bool esOrdenRemota = (_grupoVigilado && _grupoVigilado->hayMovimientoPorRed());

      static bool estabaMoviendo = false;
      if (estaMoviendo && !estabaMoviendo) {
          lastPongTime = millis(); 
          lastPingTime = 0;        
      }
      estabaMoviendo = estaMoviendo;
      
      unsigned long intervalo = estaMoviendo ? HB_INTERVAL_MOVE : HB_INTERVAL_IDLE;
      unsigned long timeout   = estaMoviendo ? HB_TIMEOUT_MOVE  : HB_TIMEOUT_IDLE;
      unsigned long now = millis();

      if (now - lastPingTime > intervalo) {
        lastPingTime = now;
        if (client.connected()) {
            client.publish(MQTT_TOPIC_PING, "1");
        }
      }

      if (now - lastPongTime > timeout) {
          if (esOrdenRemota) {
              _grupoVigilado->parar();
              debug("!!! SEGURIDAD: Timeout Heartbeat. PARADA DE EMERGENCIA !!!");
          }
          if (client.connected()) {
              debug("Cerrando conexion por falta de PONG.");
              client.disconnect();
          }
          lastPongTime = now;
      }
    }

  public:
    // CAMBIADO: El constructor ahora usa el cliente WiFi
    MqttHandler() : client(espClient) {}

    void setLogger(LogCallback callback) { externalLog = callback; }

    void vigilarGrupo() { 
      if (_grupoVigilado == nullptr) return;
      if (millis() - lastCheckTime > 100) {
         lastCheckTime = millis();
         bool forzarUpdate = _grupoVigilado->checkMqttRequest();
         String estadoActual = _grupoVigilado->getEstadoString();
         if (forzarUpdate || estadoActual != lastEstadoStr) {
            publish(estadoActual); 
            lastEstadoStr = estadoActual;
            lastPorcentaje = -1;
         }
         int porcActual = _grupoVigilado->getPorcentajeEntero();
         if (porcActual != lastPorcentaje) {
            publish(String(PREFIJO_POS) + String(porcActual));
            lastPorcentaje = porcActual;
         }
      }
    }

    void vigilarRollUp(MotorRollUp* ru) {
      if (ru == nullptr) return;
      String estadoActual = ru->getEstadoString();
      if (ru->checkMqttRequest()) {
          publish(estadoActual);
      }
    }

    void vigilarRollUp() {
      if (_rollUpVigilado) vigilarRollUp(_rollUpVigilado);
    }
  
    void setup(const char* server, int port, const char* user, const char* pass, 
               const char* topicIn, const char* topicOut, const char* deviceId) {
      _server = server; _port = port; _user = user; _pass = pass;
      _topicIn = topicIn; _topicOut = topicOut; _deviceId = deviceId;

      client.setServer(_server, _port);
      
      client.setCallback([this](char* topic, byte* payload, unsigned int length) {
        String topicStr = String(topic);
        String msg = "";
        for (int i = 0; i < length; i++) msg += (char)payload[i];
        msg.trim();

        if (topicStr == String(MQTT_TOPIC_PONG)) {
          lastPongTime = millis();
          return; 
        } 

        if (topicStr == String(_topicIn)) {
          lastPongTime = millis();
          if (msg == "?") {
            lastEstadoStr = ""; lastPorcentaje = -1;
            debug("Peticion de estado (?) recibida. Forzando actualizacion...");
          } 
          if (commandCallback) commandCallback(msg);
        }
      });
      debug("Configurado para WiFi. Broker: " + String(_server));
    }

    void vincularGrupo(GrupoMotores* grupo) { _grupoVigilado = grupo; }
    void vincularRollUp(MotorRollUp* ru) { _rollUpVigilado = ru; }
    void setCommandCallback(std::function<void(String)> callback) { commandCallback = callback; }

    void loop() {
      if (!client.connected()) {
        unsigned long now = millis();
        bool motorMoviendo = false;
        if (_grupoVigilado != nullptr) {
            String estado = _grupoVigilado->getEstadoString();
            if (estado == MSG_ABRIENDO || estado == MSG_CERRANDO || estado == MSG_CALIBRANDO) {
                motorMoviendo = true;
            }
        }

        if (now - lastReconnectAttempt > 5000) {
          lastReconnectAttempt = now;
          if (!motorMoviendo) {
              reconnect(); 
          } else {
              debug("Reconexión pospuesta: Motor en movimiento.");
          }
        }
      } else {
        client.loop();
        vigilarGrupo();
        vigilarRollUp();
      }
      enviarPing();
    }

    bool reconnect() {
      // CAMBIADO: Ahora chequeamos el estado del WiFi, no del cable Ethernet
      if (WiFi.status() != WL_CONNECTED) {
          debug("Error: WiFi no conectado.");
          return false;
      }

      debug("Intentando conectar al Broker MQTT...");
      if (client.connect(_deviceId, _user, _pass, _topicOut, 1, true, MSG_OFFLINE)) {
          client.subscribe(_topicIn);
          client.subscribe(MQTT_TOPIC_PONG);
          client.publish(_topicOut, MSG_ONLINE);
          lastPongTime = millis();
          debug("¡Conectado por WiFi! Sistema Online.");
          return true;
      }
      debug("Fallo en la conexión MQTT.");
      return false;
    }

    void publish(String msg) {
      if (client.connected()) {
        client.publish(_topicOut, msg.c_str());
      }
    }
};

#endif










/*#ifndef MQTT_H
#define MQTT_H

#include <Ethernet.h>//#include <WiFi.h>
#include <PubSubClient.h>
#include <functional>
#include "Constantes.h"
#include "GrupoMotores.h"
#include "MotorRollUp.h"

class MqttHandler {
  private:
    EthernetClient ethClient;//WiFiClient espClient;
    PubSubClient client;
    
    GrupoMotores* _grupoVigilado = nullptr;
    MotorRollUp*  _rollUpVigilado = nullptr;

    // Configuración
    const char* _server; int _port;
    const char* _user; const char* _pass;
    const char* _topicIn; const char* _topicOut;
    const char* _deviceId;

    // Variables de control interno — Grupo
    unsigned long lastReconnectAttempt = 0;
    unsigned long lastCheckTime = 0;
    String lastEstadoStr = "";
    int lastPorcentaje = -1;

    // Variables de control interno — Roll-up
    unsigned long lastCheckTimeRU = 0;
    String lastEstadoRU  = "";
    int lastPorcentajeRU = -1;

    // Variables de control interno — Heartbeat
    unsigned long lastPingTime = 0;
    unsigned long lastPongTime = 0;

    std::function<void(String)> commandCallback;


  void enviarPing() {
      // 1. Determinar el estado del sistema
      bool estaMoviendo = (_grupoVigilado && (_grupoVigilado->getEstadoString() == MSG_ABRIENDO || _grupoVigilado->getEstadoString() == MSG_CERRANDO));
      bool esOrdenRemota = (_grupoVigilado && _grupoVigilado->hayMovimientoPorRed());

      // --- Sincronización al arrancar ---
      static bool estabaMoviendo = false;
      if (estaMoviendo && !estabaMoviendo) {
          lastPongTime = millis(); // Reiniciamos la cuenta atrás del peligro
          lastPingTime = 0;        // Forzamos a que dispare un ping AHORA MISMO
      }
      estabaMoviendo = estaMoviendo;
      // ------------------------------------------
      
      
      // 2. Asignar tiempos basados en constantes
      unsigned long intervalo = estaMoviendo ? HB_INTERVAL_MOVE : HB_INTERVAL_IDLE;
      unsigned long timeout   = estaMoviendo ? HB_TIMEOUT_MOVE  : HB_TIMEOUT_IDLE;

      unsigned long now = millis();

      // 3. Lógica de Envío
      if (now - lastPingTime > intervalo) {
        lastPingTime = now;
        if (client.connected()) {
            client.publish(MQTT_TOPIC_PING, "1");
        }
      }

      // 4. Lógica de Vigilancia
      if (now - lastPongTime > timeout) {
          // Si el movimiento es por RED, ejecutamos parada de seguridad
          if (esOrdenRemota) {
              _grupoVigilado->parar();
              Serial.println(F("!!! SEGURIDAD: Timeout MQTT durante movimiento remoto. PARADA !!!"));
          }

          // Solo desconectamos si hay conexión activa.
          // Llamar a disconnect() sin conexión bloquea EthernetClient::stop() hasta 1s.
          if (client.connected()) {
              client.disconnect();
          }
          lastPongTime = now;  // Reset para el siguiente intento
      }
}

    // La detección de pérdida de heartbeat se hace en Node-RED,
    // que es quien tiene visibilidad de si los pings llegan o no.
    // El ESP32 solo envía pings y recibe pongs.

  public:
    MqttHandler() : client(ethClient) {}//MqttHandler() : client(espClient) {}

    void vigilarGrupo() { 
      if (_grupoVigilado == nullptr) return;
      if (millis() - lastCheckTime > 100) {
         lastCheckTime = millis();
         bool forzarUpdate = _grupoVigilado->checkMqttRequest();
         String estadoActual = _grupoVigilado->getEstadoString();
         if (forzarUpdate || estadoActual != lastEstadoStr) {
            publish(estadoActual); 
            lastEstadoStr = estadoActual;
            lastPorcentaje = -1;
         }
         int porcActual = _grupoVigilado->getPorcentajeEntero();
         if (porcActual != lastPorcentaje) {
            publish(String(PREFIJO_POS) + String(porcActual));
            lastPorcentaje = porcActual;
         }
      }
    }

    void vigilarRollUp(MotorRollUp* ru) {
      if (ru == nullptr) return;

      // 1. Obtenemos el estado firmado (ej: "ru1_status subiendo")
      String estadoActual = ru->getEstadoString();
      
      // 2. Usamos el método checkMqttRequest() que ya tiene cada objeto MotorRollUp
      // Este método devuelve 'true' si el motor ha cambiado de estado 
      // o si ha pasado el tiempo de cortesía.
      if (ru->checkMqttRequest()) {
          publish(estadoActual);
          Serial.println("[MQTT] Publicado: " + estadoActual);
      }
  }
 

    // Mantenemos esta para que el loop() interno de la clase no dé error
    void vigilarRollUp() {
      if (_rollUpVigilado) vigilarRollUp(_rollUpVigilado);
    }
  
    void setup(const char* server, int port, const char* user, const char* pass, 
               const char* topicIn, const char* topicOut, const char* deviceId) {
      _server = server; _port = port; _user = user; _pass = pass;
      _topicIn = topicIn; _topicOut = topicOut; _deviceId = deviceId;

      client.setServer(_server, _port);
      
      client.setCallback([this](char* topic, byte* payload, unsigned int length) {
        String topicStr = String(topic);
        String msg = "";
        for (int i = 0; i < length; i++) msg += (char)payload[i];
        msg.trim();

        // 1. Filtramos: ¿Es el mensaje de vuelta (Pong) de Node-RED?
        if (topicStr == String(MQTT_TOPIC_PONG)) {
          // No hacemos nada, el ESP32 solo cumple con recibirlo. 
          // La lógica de "está vivo" la lleva Node-RED.
          lastPongTime = millis();
          return; 
        } 

        // 2. Filtramos: ¿Es un comando dirigido a MI topic de entrada?
        if (topicStr == String(_topicIn)) {
          lastPongTime = millis();

          // --- Interceptar petición de estado ---
          if (msg == "?") {
            // Al borrar estos valores, engañamos a vigilarGrupo() y vigilarRollUp() 
            // para que detecten un "cambio" y publiquen todo inmediatamente.
            lastEstadoStr = "";
            lastPorcentaje = -1;
            lastEstadoRU = "";
            lastPorcentajeRU = -1;
            Serial.println("[MQTT] Peticion de estado recibida desde Node-RED. Forzando actualizacion...");
          } 
          // ---------------------------------------------

          if (commandCallback) {
            commandCallback(msg);
          }
        }
      });
    }

    void vincularGrupo(GrupoMotores* grupo) {
      _grupoVigilado = grupo;
    }

    void vincularRollUp(MotorRollUp* ru) {
      _rollUpVigilado = ru;
    }

    void setCommandCallback(std::function<void(String)> callback) {
      commandCallback = callback;
    }

    // --- LOOP CON VIGILANCIA INTEGRADA ---
  void loop() {
    if (!client.connected()) {
      unsigned long now = millis();
      
      // 1. ¿El motor se está moviendo físicamente?
      bool motorMoviendo = false;
      if (_grupoVigilado != nullptr) {
          String estado = _grupoVigilado->getEstadoString();
          if (estado == MSG_ABRIENDO || estado == MSG_CERRANDO || estado == MSG_CALIBRANDO) {
              motorMoviendo = true;
          }
      }

      // 2. Solo intentamos reconectar si han pasado 5s Y EL MOTOR ESTÁ QUIETO
      if (now - lastReconnectAttempt > 5000) {
        if (!motorMoviendo) {
            // El motor está parado, es seguro bloquear unos segundos para buscar red
            lastReconnectAttempt = now;
            reconnect(); 
        } else {
            // Peligro: El motor se mueve. Pospongamos la conexión.
            lastReconnectAttempt = now; // Reinicia el reloj para probar dentro de 5s
            Serial.println("[MQTT] Reconexión pospuesta: Esperando a que el motor pare para evitar bloqueos.");
        }
      }
    } else {
      client.loop();
      vigilarGrupo();
      vigilarRollUp();
    }
    
    // El latido siempre se evalúa para emergencias
    enviarPing();
  }

    bool reconnect() {
      // Bail out si no hay enlace físico confirmado (LinkOFF O Unknown = W5500 no init).
      // Usar != LinkON evita el bloqueo de client.connect() cuando el cable no está.
      if (Ethernet.linkStatus() != LinkON) {
          return false;
      }

      // Intentamos conectar a MQTT
      if (client.connect(_deviceId, _user, _pass, _topicOut, 1, true, MSG_OFFLINE)) {
          client.subscribe(_topicIn);
          client.subscribe(MQTT_TOPIC_PONG);
          client.publish(_topicOut, MSG_ONLINE);
          lastPongTime = millis();
          return true;
        }
        return false;
  }


    void publish(String msg) {
      if (client.connected()) {
        client.publish(_topicOut, msg.c_str());
      }
    }
};

#endif
*/
