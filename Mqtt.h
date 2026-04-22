#ifndef MQTT_H
#define MQTT_H

// ============================================================
//  Mqtt.h — Adaptado para M-Duino 21+ (Arduino Mega)
//
//  Cambios respecto a la versión ESP32:
//   • WiFiClient    → EthernetClient
//   • std::function → punteros de función simples
//   • Lambda [this] en setCallback → patrón instancia estática
//   • MotorRollUp   → eliminado (esta finca solo tiene techo)
//   • WiFi.status() → Ethernet.linkStatus()
// ============================================================

#include <Ethernet.h>
#include <PubSubClient.h>   // Instalar desde Gestor de Librerías
#include <utility/w5100.h>  // Acceso directo al chip W5500: Sock_CLOSE instantáneo
#include "Constantes.h"
#include "GrupoMotores.h"
#include "Red.h"

// ============================================================
//  VARIABLE ESTÁTICA GLOBAL
//  PubSubClient necesita una función C pura como callback.
//  Usamos un puntero estático a la instancia activa para
//  redirigir la llamada al método de clase.
// ============================================================
class MqttHandler;
static MqttHandler* _mqttInstance = nullptr;

static void _mqttStaticCallback(char* topic, byte* payload, unsigned int length) {
  // Implementado más abajo, fuera de la clase, para evitar dependencia circular
}

class MqttHandler {
  private:
    EthernetClient ethClient;
    PubSubClient   client;

    GrupoMotores*  _grupoVigilado = nullptr;
    VFDController* _vfdVigilado   = nullptr;
    Motor*         _motorVigilado = nullptr;
    String         _vfdMotorId    = "motor_1";  // identificador del motor en el JSON

    // Configuración
    const char* _server   = nullptr;  int _port = 1883;
    const char* _user     = nullptr;  const char* _pass     = nullptr;
    const char* _topicIn  = nullptr;  const char* _topicOut = nullptr;
    const char* _deviceId = nullptr;

    // Control interno
    unsigned long lastReconnectAttempt = 0;
    unsigned long _reconnectInterval   = 5000;   // Backoff: empieza en 5s, sube hasta 60s
    unsigned long lastCheckTime        = 0;
    String        lastEstadoStr        = "";
    int           lastPorcentaje       = -1;
    unsigned long lastPingTime         = 0;
    unsigned long lastPongTime         = 0;

    // Tracking telemetría VFD (detección de cambio para publicación)
    float         _vfdLastFreqSal  = -1.0;
    float         _vfdLastFreqCfg  = -1.0;
    float         _vfdLastCorr     = -1.0;
    float         _vfdLastVBus     = -1.0;
    float         _vfdLastVSal     = -1.0;
    String        _vfdLastEstado   = "";
    uint16_t      _vfdLastFalla    = 0xFFFF;  // valor imposible → fuerza primera publicación
    unsigned long _vfdLastPublish  = 0;

    // Modo verbose (activar con comando 'M')
    bool _verbose = false;

    // Callback de comandos — puntero de función simple
    typedef void (*CommandCallback)(String);
    CommandCallback commandCallback = nullptr;

    // Logger
    typedef void (*LogCallback)(String);
    LogCallback externalLog = nullptr;

    void debug(String msg) {
      if (externalLog) externalLog("[MQTT] " + msg);
    }
    void vlog(String msg) {   // solo imprime si verbose está activo
      if (_verbose && externalLog) externalLog("[MQTT] " + msg);
    }

    // --------------------------------------------------------
    //  Cierre de socket seguro (sin bloqueo aunque no haya cable)
    //
    //  client.disconnect() envía un paquete TCP DISCONNECT y luego
    //  llama a ethClient.stop(), que espera el cierre de la conexión
    //  TCP. Sin cable, el W5500 reintenta hasta agotar RTR×RCR, lo
    //  que puede bloquear el loop varios segundos.
    //
    //  Solución: si no hay cable físico, cerramos el socket W5500
    //  directamente con ethClient.stop() sin pasar por la librería
    //  MQTT (que intentaría escribir TCP primero).
    //  PubSubClient detectará la desconexión en el siguiente loop()
    //  porque ethClient.connected() devolverá false.
    // --------------------------------------------------------
    void _safeDisconnect() {
      if (!Red::linkOK()) {
        // Sin cable: ethClient.stop() tiene un bucle hardcodeado de 1000ms:
        //   while (status() != SnSR::CLOSED && millis()-start < 1000)
        // El socket se queda en FIN_WAIT (no pasa a CLOSED) porque el ACK
        // del FIN nunca llega → espera 1 segundo completo.
        //
        // Solución: usar Sock_CLOSE directamente vía W5100.execCmdSn().
        // Sock_CLOSE ≠ Sock_DISCON: cierra el socket de forma instantánea
        // sin enviar ningún paquete TCP, sin esperar ACKs, sin bucle.
        // PubSubClient detectará la desconexión en el siguiente ciclo porque
        // ethClient.connected() consultará el estado del socket (CLOSED) y
        // devolverá false.
        for (int s = 0; s < MAX_SOCK_NUM; s++) {
          if (W5100.readSnSR(s) != SnSR::CLOSED) {
            W5100.execCmdSn(s, Sock_CLOSE);
          }
        }
        debug(F("Socket cerrado (Sock_CLOSE directo). Bloqueo: 0 ms."));
      } else {
        // Con cable: desconexión MQTT limpia (envía paquete DISCONNECT)
        client.disconnect();
      }
    }

    // --------------------------------------------------------
    //  Heartbeat / Watchdog de conexión
    // --------------------------------------------------------
    void enviarPing() {
      bool estaMoviendo  = (_grupoVigilado &&
                           (_grupoVigilado->getEstadoString() == MSG_ABRIENDO ||
                            _grupoVigilado->getEstadoString() == MSG_CERRANDO));
      bool esOrdenRemota = (_grupoVigilado && _grupoVigilado->hayMovimientoPorRed());

      static bool estabaMoviendo = false;
      if (estaMoviendo && !estabaMoviendo) {
        lastPongTime = millis();
        lastPingTime = 0;
      }
      estabaMoviendo = estaMoviendo;

      unsigned long intervalo = estaMoviendo ? (unsigned long)HB_INTERVAL_MOVE : (unsigned long)HB_INTERVAL_IDLE;
      unsigned long timeout   = estaMoviendo ? (unsigned long)HB_TIMEOUT_MOVE  : (unsigned long)HB_TIMEOUT_IDLE;
      unsigned long now       = millis();

      if (now - lastPingTime > intervalo) {
        lastPingTime = now;
        // Guardia de cable: client.publish() llama a EthernetClient::write(),
        // que puede bloquear el loop varios segundos si el cable está caído
        // (el W5500 intenta enviar TCP y espera ACKs que nunca llegan).
        // Si no hay cable físico, no tiene sentido intentar publicar.
        if (client.connected() && Red::linkOK()) {
          client.publish(MQTT_TOPIC_PING, "1");
          vlog("PING >> " + String(MQTT_TOPIC_PING));
        } else {
          vlog(F("PING omitido: sin conexion o sin cable"));
        }
      }

      if (now - lastPongTime > timeout) {
        if (esOrdenRemota) {
          _grupoVigilado->parar();
          debug(F("!!! SEGURIDAD: Timeout Heartbeat. PARADA DE EMERGENCIA !!!"));
        }
        if (client.connected()) {
          debug(F("Timeout PONG. Desconectando."));
          // NOTA: setRetransmissionTimeout(5) usaba la fórmula 5*10/100 = 0
          // (división entera), lo que ponía RTR=0 en el W5500 → timeout
          // infinito. Eliminado por completo: usamos _safeDisconnect() que
          // evita cualquier write TCP si no hay cable físico.
          _safeDisconnect();
        }
        lastPongTime = now;
      }
    }

  public:
    MqttHandler() : client(ethClient) {
      _mqttInstance = this;   // Registrar instancia para el callback estático
    }

    void setLogger(LogCallback callback)       { externalLog    = callback; }
    void setCommandCallback(CommandCallback cb){ commandCallback = cb; }
    void vincularGrupo(GrupoMotores* g)        { _grupoVigilado = g; }
    void vincularMotor(Motor* m)               { _motorVigilado = m; }
    void vincularVFD(VFDController* v, const char* motorId = "motor_1") {
      _vfdVigilado = v;
      _vfdMotorId  = motorId;
    }

    void toggleVerbose() {
      _verbose = !_verbose;
      debug(_verbose ? F("Verbose ON") : F("Verbose OFF"));
      if (_verbose) imprimirDiagnostico();
    }

    void imprimirDiagnostico() {
      debug("--- DIAGNOSTICO MQTT ---");
      debug("Broker   : " + String(_server) + ":" + String(_port));
      debug("Device ID: " + String(_deviceId));
      debug("Topic IN : " + String(_topicIn));
      debug("Topic OUT: " + String(_topicOut));
      debug("Ping     : " + String(MQTT_TOPIC_PING));
      debug("Pong     : " + String(MQTT_TOPIC_PONG));
      debug("Conectado: " + String(client.connected() ? "SI" : "NO"));
      if (!client.connected()) {
        debug("Estado PubSubClient: " + String(client.state()));
        // Códigos: -4=timeout -3=lost -2=failed -1=disconnected 0=connected 1=bad_proto 2=bad_id 3=unavailable 4=bad_creds 5=unauthorized
      }
      debug("Ultimo PONG hace: " + String((millis() - lastPongTime) / 1000) + " s");
      debug("Ethernet link   : " + String(Ethernet.linkStatus() == LinkON ? "OK" : "SIN CABLE"));
      IPAddress ip = Ethernet.localIP();
      debug("IP local        : " + String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]));
      debug("------------------------");
    }

    // --------------------------------------------------------
    //  Método interno llamado desde el callback estático
    // --------------------------------------------------------
    void _handleMessage(char* topic, byte* payload, unsigned int length) {
      String topicStr = String(topic);
      String msg      = "";
      for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
      msg.trim();

      // Pong del heartbeat
      if (topicStr == String(MQTT_TOPIC_PONG)) {
        lastPongTime = millis();
        vlog("<< PONG recibido");
        return;
      }

      if (topicStr == String(_topicIn)) {
        lastPongTime = millis();
        debug("<< RX [" + topicStr + "] \"" + msg + "\"");
        if (msg == "?") {
          lastEstadoStr  = "";
          lastPorcentaje = -1;
          _vfdLastEstado = "";   // fuerza reenvío del JSON VFD también
          _vfdLastFalla  = 0xFFFF;
          debug(F("Peticion '?' recibida. Forzando actualizacion."));
        }
        if (commandCallback) commandCallback(msg);
      } else {
        vlog("<< RX desconocido [" + topicStr + "] \"" + msg + "\"");
      }
    }

    // --------------------------------------------------------
    //  Telemetría VFD — publicar JSON al topic /vfd
    //
    //  Se publica cuando cualquier valor cambia más allá del
    //  umbral mínimo, o como máximo cada VFD_PUBLISH_INTERVAL ms
    //  aunque no haya cambio (heartbeat de datos).
    //
    //  Umbrales: frecuencia ≥ 0.1 Hz · corriente ≥ 0.1 A
    //            voltajes ≥ 1 V · estado o falla cualquier cambio
    // --------------------------------------------------------
    // Intervalo máximo de publicación forzada (aunque no haya cambio)
    static const unsigned long VFD_INTERVAL_MARCHA = 300UL;   // 0.3s — motor en movimiento
    static const unsigned long VFD_INTERVAL_REPOSO = 2000UL;  // 2s   — motor parado

    void publicarVFD() {
      if (!_vfdVigilado) return;
      if (!client.connected() || !Red::linkOK()) return;
      // Solo publicar si hay datos frescos del variador (<10s)
      if (!_vfdVigilado->estadoFresco(10000)) return;

      float    freqSal = _vfdVigilado->getFreqSalida();
      float    freqCfg = _vfdVigilado->getFreqConfigurada();
      float    corr    = _vfdVigilado->getCorriente();
      float    vBus    = _vfdVigilado->getVBus();
      float    vSal    = _vfdVigilado->getVSalida();
      String   estado  = _vfdVigilado->getEstadoStr();
      uint16_t falla   = _vfdVigilado->getCodigoFalla();

      bool enMarcha = _vfdVigilado->motorGirando();
      unsigned long intervalo = enMarcha ? VFD_INTERVAL_MARCHA : VFD_INTERVAL_REPOSO;

      unsigned long now = millis();
      bool cambio =
        (fabs(freqSal - _vfdLastFreqSal) >= 0.1f) ||
        (fabs(freqCfg - _vfdLastFreqCfg) >= 0.1f) ||
        (fabs(corr    - _vfdLastCorr   ) >= 0.1f) ||
        (fabs(vBus    - _vfdLastVBus   ) >= 1.0f) ||
        (fabs(vSal    - _vfdLastVSal   ) >= 1.0f) ||
        (estado != _vfdLastEstado)                  ||
        (falla  != _vfdLastFalla);
      bool forzar = (now - _vfdLastPublish) >= intervalo;

      if (!cambio && !forzar) return;

      // Construir JSON sin ArduinoJson (ahorra RAM en Mega)
      // Formato: {"motor":"techo_1","freq_sal":45.50,"freq_cfg":50.00,"corriente":3.2,
      //           "v_bus":540.0,"v_sal":380.0,
      //           "estado":"EN MARCHA FWD","falla":0,"falla_txt":"OK"}
      String nombre_falla = (falla == 0) ? "OK" : _vfdVigilado->diagnosticoCompleto(falla).nombre;
      String json = "{";
      json += "\"motor\":\""    + _vfdMotorId + "\",";
      json += "\"freq_sal\":"   + String(freqSal, 2) + ",";
      json += "\"freq_cfg\":"   + String(freqCfg, 2) + ",";
      json += "\"corriente\":"  + String(corr,    1) + ",";
      json += "\"v_bus\":"      + String(vBus,    1) + ",";
      json += "\"v_sal\":"      + String(vSal,    1) + ",";
      json += "\"estado\":\""   + estado + "\",";
      json += "\"falla\":"      + String(falla)      + ",";
      json += "\"falla_txt\":\"" + nombre_falla + "\"";
      if (_motorVigilado) {
        json += ",\"anticipacion\":" + String(_motorVigilado->getAnticipacion());
      }
      json += "}";

      client.publish(MQTT_TOPIC_VFD, json.c_str());
      vlog(">> VFD [" + String(MQTT_TOPIC_VFD) + "] " + json);

      _vfdLastFreqSal = freqSal;
      _vfdLastFreqCfg = freqCfg;
      _vfdLastCorr    = corr;
      _vfdLastVBus    = vBus;
      _vfdLastVSal    = vSal;
      _vfdLastEstado  = estado;
      _vfdLastFalla   = falla;
      _vfdLastPublish = now;
    }

    // --------------------------------------------------------
    //  Vigilancia del motor techo (publicación por cambio)
    // --------------------------------------------------------
    void vigilarGrupo() {
      if (_grupoVigilado == nullptr) return;
      if (millis() - lastCheckTime > 30) {
        lastCheckTime = millis();
        bool   forzar      = _grupoVigilado->checkMqttRequest();
        String estadoActual= _grupoVigilado->getEstadoString();

        if (forzar || estadoActual != lastEstadoStr) {
          publish(estadoActual);
          lastEstadoStr  = estadoActual;
          lastPorcentaje = -1;
        }
        int porcActual = _grupoVigilado->getPorcentajeEntero();
        if (porcActual != lastPorcentaje) {
          publish(String(PREFIJO_POS) + String(porcActual));
          lastPorcentaje = porcActual;
        }

        // Telemetría del variador: publicar si hay cambio o cada 5s
        publicarVFD();
      }
    }

    // --------------------------------------------------------
    //  SETUP
    // --------------------------------------------------------
    void setup(const char* server, int port,
               const char* user,   const char* pass,
               const char* topicIn, const char* topicOut,
               const char* deviceId)
    {
      _server   = server;   _port     = port;
      _user     = user;     _pass     = pass;
      _topicIn  = topicIn;  _topicOut = topicOut;
      _deviceId = deviceId;

      client.setServer(_server, _port);
      client.setCallback(_mqttStaticCallbackImpl);

      // ---- Ajustes críticos de robustez (requiere PubSubClient >= 2.8) ----
      // Sin setSocketTimeout, connect() puede bloquear el loop 15 s si el
      // broker no responde → el VFD pierde heartbeat y el encoder pierde pulsos.
      // Reducido de 4 s a 2 s: limita el peor caso a la mitad. El reconexión
      // está protegida para no ejecutarse durante movimiento, así que 2 s son
      // suficientes y seguros.
      client.setSocketTimeout(2);    // Máx. 2 s bloqueando en connect/read
      client.setKeepAlive(10);       // MQTT keepalive 10 s (default 15 s)
      client.setBufferSize(512);     // Buffer RX/TX (default 256 bytes)

      // Iniciar lastPongTime en el presente para que el watchdog no dispare
      // antes de tener siquiera oportunidad de conectarse
      lastPongTime = millis();

      debug("Configurado. Broker: " + String(_server) + ":" + String(_port));
    }

    // --------------------------------------------------------
    //  LOOP  (llamar desde loop() del .ino)
    // --------------------------------------------------------
    /*void loop() {
      if (!client.connected()) {
        unsigned long now = millis();

        // No reintentar si el motor está en movimiento crítico
        bool motorMoviendo = false;
        if (_grupoVigilado) {
          String estado = _grupoVigilado->getEstadoString();
          if (estado == MSG_ABRIENDO || estado == MSG_CERRANDO || estado == MSG_CALIBRANDO) {
            motorMoviendo = true;
          }
        }

        if (now - lastReconnectAttempt > 15000) {
          lastReconnectAttempt = now;
          if (!motorMoviendo) reconnect();
          else debug(F("Reconexion pospuesta: motor en movimiento."));
        }
      } else {
        client.loop();
        vigilarGrupo();
      }
      enviarPing();
    }*/

    void loop() {
      if (!client.connected()) {
        unsigned long now = millis();

        // Reconexión inmediata si el cable acaba de volver
        bool cableVuelto = Red::linkJustRecovered();
        if (cableVuelto) {
          debug(F("Cable recuperado. Forzando intento de reconexion."));
          lastReconnectAttempt = 0;   // Fuerza intento en este ciclo
          _reconnectInterval   = 5000;
        }

        if (now - lastReconnectAttempt >= _reconnectInterval) {
          lastReconnectAttempt = now;

          // No reconectar si el motor está moviéndose: connect() puede
          // bloquear varios segundos aunque haya setSocketTimeout.
          bool motorMoviendo = false;
          if (_grupoVigilado) {
            String est = _grupoVigilado->getEstadoString();
            motorMoviendo = (est == MSG_ABRIENDO || est == MSG_CERRANDO || est == MSG_CALIBRANDO);
          }

          if (!motorMoviendo) {
            vlog("Sin conexion. Intento en " + String(_reconnectInterval / 1000) + "s backoff...");
            if (reconnect()) {
              _reconnectInterval = 5000;   // Éxito: resetear backoff
            } else {
              // Backoff exponencial: 5 → 10 → 20 → 40 → 60 s (máximo)
              _reconnectInterval = min((unsigned long)60000, _reconnectInterval * 2);
              vlog("Proximo intento en " + String(_reconnectInterval / 1000) + "s");
            }
          } else {
            vlog(F("Reconexion pospuesta: motor en movimiento."));
            // No consumir el intervalo — reintentar pronto cuando pare
            lastReconnectAttempt = now - _reconnectInterval + 3000;
          }
        }
      } else {
        // client.loop() envía MQTT PINGREQ cada keepalive segundos.
        // Si el cable se ha ido pero TCP aún cree que está conectado,
        // ese write bloquea el loop hasta agotar los reintentos W5500.
        // Solución: si no hay cable físico, forzar cierre sin bloqueo
        // en lugar de llamar a client.loop().
        if (!Red::linkOK()) {
          debug(F("Cable ausente con sesion activa. Forzando cierre de socket."));
          _safeDisconnect();
        } else {
          client.loop();
        }
        vigilarGrupo();
      }
      enviarPing();
    }

    // --------------------------------------------------------
    //  Reconexión al broker MQTT
    // --------------------------------------------------------
    bool reconnect() {
      if (Ethernet.linkStatus() != LinkON) {
        debug(F("Sin cable. Reconexion omitida."));
        return false;
      }
      debug(F("Intentando conectar al Broker MQTT..."));

      // LWT: QoS=1, retain=true → si el PLC muere sin desconectarse limpio,
      // el broker publica MSG_OFFLINE automáticamente.
      if (client.connect(_deviceId, _user, _pass, _topicOut, 1, true, MSG_OFFLINE)) {
        client.subscribe(_topicIn);
        client.subscribe(MQTT_TOPIC_PONG);
        // retain=true → Node-RED siempre sabe que el PLC está ONLINE aunque
        // se suscriba después de que el PLC arrancara.
        client.publish(_topicOut, MSG_ONLINE, true);
        lastPongTime = millis();
        debug(F("Conectado. ONLINE publicado."));
        return true;
      }

      // Loguear el código de error para diagnóstico
      // -4=TIMEOUT -3=CONN_LOST -2=CONN_FAILED -1=DISCONNECTED
      //  1=BAD_PROTOCOL 2=BAD_CLIENT_ID 3=SERVER_UNAVAILABLE
      //  4=BAD_CREDENTIALS 5=UNAUTHORIZED
      debug("Fallo. Estado PubSubClient: " + String(client.state()));
      return false;
    }

    void publish(String msg) {
      // Doble guarda: client.connected() NO es suficiente cuando el cable
      // acaba de caerse — TCP puede creer que sigue conectado mientras el
      // W5500 todavía no ha detectado el timeout. Red::linkOK() comprueba
      // el pin físico de link del W5500 (Ethernet.linkStatus()) y evita
      // que client.publish() intente un write TCP que bloquearía el loop.
      if (client.connected() && Red::linkOK()) {
        client.publish(_topicOut, msg.c_str());
        vlog(">> TX [" + String(_topicOut) + "] \"" + msg + "\"");
      } else {
        vlog(">> TX omitido (sin conexion o sin cable): \"" + msg + "\"");
      }
    }

    // --------------------------------------------------------
    //  Función C pura para PubSubClient::setCallback
    //  No puede ser lambda con captura en AVR/Mega.
    // --------------------------------------------------------
    static void _mqttStaticCallbackImpl(char* topic, byte* payload, unsigned int length) {
      if (_mqttInstance) _mqttInstance->_handleMessage(topic, payload, length);
    }
};

#endif
