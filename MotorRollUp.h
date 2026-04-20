#ifndef MOTORROLLUP_H
#define MOTORROLLUP_H

#include <Arduino.h>
#include <functional>

// ==========================================
//   MENSAJES DE ESTADO
// ==========================================
const char* MSG_RU_EMERGENCIA = "ru_status emergency_pressed";
const char* MSG_RU_ERROR_FC   = "ru_status error_finales_carrera";
const char* MSG_RU_SUBIENDO   = "ru_status subiendo";
const char* MSG_RU_BAJANDO    = "ru_status bajando";
const char* MSG_RU_ARRIBA     = "ru_status arriba";
const char* MSG_RU_ABAJO      = "ru_status abajo";
const char* MSG_RU_PARADO     = "ru_status parado";


class MotorRollUp {
  private:
    String id;

    // --- ENTRADAS ---
    int pinMarchaArriba;  // Botón subir (HIGH = pulsado)
    int pinMarchaAbajo;   // Botón bajar (HIGH = pulsado)
    int pinFCArriba;      // Final de carrera superior (NC: LOW = activado)
    int pinFCAbajo;       // Final de carrera inferior (NC: LOW = activado)
    int pinSeta;          // Seta de emergencia compartida (NC: LOW = pulsada)

    // --- SALIDAS ---
    int pinMotorArriba;   // Relé subir
    int pinMotorAbajo;    // Relé bajar
    int pinLedArriba;     // LED indicador arriba
    int pinLedAbajo;      // LED indicador abajo

    // --- ESTADO ---
    bool motorSubiendo = false;
    bool motorBajando  = false;
    bool estaArriba    = false;
    bool estaAbajo     = false;
    bool enEmergencia  = false;
    bool errorFC       = false;

    // --- COMANDOS (físico latched + software) ---
    // Un pulso en el botón activa el latch; el FC o la seta lo borran
    bool cmdSubir    = false;
    bool cmdBajar    = false;
    bool lastBtnArr  = false;   // Para detección de flanco en botón arriba
    bool lastBtnBaj  = false;   // Para detección de flanco en botón abajo

    // --- MQTT ---
    bool   requestMqttUpdate = false;
    String lastEstado        = "";

    // --- PARPADEO LEDS ---
    unsigned long lastBlinkTime = 0;
    bool blinkState = false;   // Tick base cada 300ms


    // ==========================================
    //   LOGGER (opcional, igual que Motor.h)
    // ==========================================
    typedef std::function<void(String)> LogCallback;
    LogCallback externalLog = nullptr;

    void debug(String msg) {
      if (externalLog) externalLog("[" + id + "] " + msg);
    }

    // Lectura directa de la seta (NC: LOW = pulsada o cable cortado).
    // Se usa en TODAS las entradas que podrían arrancar el motor.
    bool setaPulsada() { return digitalRead(pinSeta) == LOW; }


    // ==========================================
    //   LÓGICA PRINCIPAL DEL MOTOR
    // ==========================================
    void gestionarMotor() {

      // --- EMERGENCIA (prioridad absoluta) ---
      if (digitalRead(pinSeta) == LOW) {
        if (!enEmergencia) {
          debug("!!! EMERGENCIA !!!");
          digitalWrite(pinMotorArriba, LOW);
          digitalWrite(pinMotorAbajo,  LOW);
          motorSubiendo = false;
          motorBajando  = false;
          cmdSubir      = false;
          cmdBajar      = false;
          enEmergencia  = true;
          requestMqttUpdate = true;
        }
        return; // Bloqueamos todo mientras la seta esté pulsada
      } else {
        if (enEmergencia) {
          debug("Emergencia OFF.");
          enEmergencia = false;
          requestMqttUpdate = true;
        }
      }

      // --- FLANCO DE SUBIDA EN BOTONES FÍSICOS → activa latch ---
      bool btnArr = (digitalRead(pinMarchaArriba) == HIGH);
      bool btnBaj = (digitalRead(pinMarchaAbajo)  == HIGH);

      if (btnArr && !lastBtnArr) { cmdSubir = true;  cmdBajar = false; }
      if (btnBaj && !lastBtnBaj) { cmdBajar = true;  cmdSubir = false; }
      lastBtnArr = btnArr;
      lastBtnBaj = btnBaj;

      // --- FINALES DE CARRERA ---
      bool fcArr = (digitalRead(pinFCArriba) == LOW);  // NC
      bool fcBaj = (digitalRead(pinFCAbajo)  == LOW);  // NC

      // --- ERROR: ambos FC activos a la vez (imposible físicamente) ---
      if (fcArr && fcBaj) {
        if (!errorFC) {
          debug("!!! ERROR: Ambos finales de carrera activos simultaneamente !!!");
          digitalWrite(pinMotorArriba, LOW);
          digitalWrite(pinMotorAbajo,  LOW);
          motorSubiendo = false;
          motorBajando  = false;
          cmdSubir      = false;
          cmdBajar      = false;
          errorFC       = true;
          requestMqttUpdate = true;
        }
        return; // Bloqueamos todo hasta que se resuelva
      } else {
        if (errorFC) {
          debug("Error FC resuelto.");
          errorFC = false;
          requestMqttUpdate = true;
        }
      }

      estaArriba = fcArr;
      estaAbajo  = fcBaj;

      // FC alcanzado → cortar motor y limpiar latch
      if (fcArr && motorSubiendo) {
        digitalWrite(pinMotorArriba, LOW);
        motorSubiendo = false;
        cmdSubir      = false;
        debug("FC arriba: motor parado.");
        requestMqttUpdate = true;
      }
      if (fcBaj && motorBajando) {
        digitalWrite(pinMotorAbajo, LOW);
        motorBajando = false;
        cmdBajar     = false;
        debug("FC abajo: motor parado.");
        requestMqttUpdate = true;
      }

      // --- SEGURIDAD: no permitir ambas marchas a la vez ---
      if (cmdSubir && cmdBajar) {
        digitalWrite(pinMotorArriba, LOW);
        digitalWrite(pinMotorAbajo,  LOW);
        motorSubiendo = false;
        motorBajando  = false;
        cmdSubir = false;
        cmdBajar = false;
        return;
      }

      // --- MARCHA ARRIBA ---
      if (cmdSubir && !fcArr) {
        if (!motorSubiendo) {
          digitalWrite(pinMotorAbajo,  LOW);
          digitalWrite(pinMotorArriba, HIGH);
          motorSubiendo = true;
          motorBajando  = false;
          debug("Subiendo.");
          requestMqttUpdate = true;
        }
      } else {
        if (motorSubiendo) {
          digitalWrite(pinMotorArriba, LOW);
          motorSubiendo = false;
          requestMqttUpdate = true;
        }
      }

      // --- MARCHA ABAJO ---
      if (cmdBajar && !fcBaj) {
        if (!motorBajando) {
          digitalWrite(pinMotorArriba, LOW);
          digitalWrite(pinMotorAbajo,  HIGH);
          motorBajando  = true;
          motorSubiendo = false;
          debug("Bajando.");
          requestMqttUpdate = true;
        }
      } else {
        if (motorBajando) {
          digitalWrite(pinMotorAbajo, LOW);
          motorBajando = false;
          requestMqttUpdate = true;
        }
      }
    }


    // ==========================================
    //   GESTIÓN DE LEDS (Actualizada)
    //
    //   ARRIBA (quieto)    → Led arriba FIJO, led abajo APAGADO
    //   ABAJO  (quieto)    → Led abajo FIJO, led arriba APAGADO
    //   SUBIENDO           → Led arriba PARPADEA, led abajo APAGADO
    //   BAJANDO            → Led abajo PARPADEA, led arriba APAGADO
    //   PARADO EN MEDIO    → Parpadeo SINCRONIZADO (los dos a la vez)
    // ==========================================
    void gestionarLeds() {
      unsigned long now = millis();

      // Tick para el parpadeo (500ms)
      if (now - lastBlinkTime >= 500) {
        blinkState    = !blinkState;
        lastBlinkTime = now;
      }

      bool moviendo = motorSubiendo || motorBajando;

      if (estaArriba && !moviendo) {
        // Posición conocida: arriba del todo
        digitalWrite(pinLedArriba, HIGH);
        digitalWrite(pinLedAbajo,  LOW);
      }
      else if (estaAbajo && !moviendo) {
        // Posición conocida: abajo del todo
        digitalWrite(pinLedArriba, LOW);
        digitalWrite(pinLedAbajo,  HIGH);
      }
      else if (motorSubiendo) {
        // En movimiento hacia ARRIBA
        digitalWrite(pinLedArriba, blinkState);
        digitalWrite(pinLedAbajo,  LOW);
      }
      else if (motorBajando) {
        // En movimiento hacia ABAJO
        digitalWrite(pinLedArriba, LOW);
        digitalWrite(pinLedAbajo,  blinkState);
      }
      else {
        // Parado en posición intermedia desconocida
        digitalWrite(pinLedArriba, blinkState);
        digitalWrite(pinLedAbajo,  blinkState);
      }
    }

    /*
    // ==========================================
    //   GESTIÓN DE LEDS
    //
    //   ARRIBA (quieto)    → Led arriba FIJO, led abajo APAGADO
    //   ABAJO  (quieto)    → Led abajo FIJO, led arriba APAGADO
    //   MOVIENDOSE         → Parpadeo ALTERNADO (desfasado: uno ON mientras otro OFF)
    //   PARADO EN MEDIO    → Parpadeo SINCRONIZADO (los dos a la vez)
    // ==========================================
    void gestionarLeds() {
      unsigned long now = millis();

      // Tick cada 300ms
      if (now - lastBlinkTime >= 500) {
        blinkState    = !blinkState;
        lastBlinkTime = now;
      }

      bool moviendo = motorSubiendo || motorBajando;

      if (estaArriba && !moviendo) {
        // Posición conocida: arriba
        digitalWrite(pinLedArriba, HIGH);
        digitalWrite(pinLedAbajo,  LOW);
      }
      else if (estaAbajo && !moviendo) {
        // Posición conocida: abajo
        digitalWrite(pinLedArriba, LOW);
        digitalWrite(pinLedAbajo,  HIGH);
      }
      else if (moviendo) {
        // En movimiento → alternados (uno es el complementario del otro)
        digitalWrite(pinLedArriba,  blinkState);
        digitalWrite(pinLedAbajo,  !blinkState);
      }
      else {
        // Parado en posición intermedia desconocida → sincronizados
        digitalWrite(pinLedArriba, blinkState);
        digitalWrite(pinLedAbajo,  blinkState);
      }
    }
    */


  public:

    // ==========================================
    //   CONSTRUCTOR
    //
    //   id             → Nombre para logs ("ru1", "ru2"...)
    //   pMarchaArriba  → Botón subir            (HIGH = pulsado)
    //   pMarchaAbajo   → Botón bajar            (HIGH = pulsado)
    //   pFCArriba      → Final de carrera sup.  (NC: LOW = activado)
    //   pFCAbajo       → Final de carrera inf.  (NC: LOW = activado)
    //   pSeta          → Seta de emergencia     (NC: LOW = pulsada, pin compartido)
    //   pMotorArriba   → Salida relé subir
    //   pMotorAbajo    → Salida relé bajar
    //   pLedArriba     → Salida LED arriba
    //   pLedAbajo      → Salida LED abajo
    // ==========================================
    MotorRollUp(String id,
                int pMarchaArriba, int pMarchaAbajo,
                int pFCArriba,     int pFCAbajo,
                int pSeta,
                int pMotorArriba,  int pMotorAbajo,
                int pLedArriba,    int pLedAbajo)
    {
      this->id = id;

      pinMarchaArriba = pMarchaArriba;
      pinMarchaAbajo  = pMarchaAbajo;
      pinFCArriba     = pFCArriba;
      pinFCAbajo      = pFCAbajo;
      pinSeta         = pSeta;
      pinMotorArriba  = pMotorArriba;
      pinMotorAbajo   = pMotorAbajo;
      pinLedArriba    = pLedArriba;
      pinLedAbajo     = pLedAbajo;

      // Salidas → empezar apagadas
      pinMode(pinMotorArriba, OUTPUT); digitalWrite(pinMotorArriba, LOW);
      pinMode(pinMotorAbajo,  OUTPUT); digitalWrite(pinMotorAbajo,  LOW);
      pinMode(pinLedArriba,   OUTPUT); digitalWrite(pinLedArriba,   LOW);
      pinMode(pinLedAbajo,    OUTPUT); digitalWrite(pinLedAbajo,    LOW);

      // Entradas
      pinMode(pinMarchaArriba, INPUT);
      pinMode(pinMarchaAbajo,  INPUT);
      pinMode(pinFCArriba,     INPUT_PULLUP);
      pinMode(pinFCAbajo,      INPUT_PULLUP);
      pinMode(pinSeta,         INPUT); // Compartido, pero no pasa nada declararlo dos veces
    }

    void setLogger(LogCallback callback) { externalLog = callback; }

    // Leer estado inicial al arrancar
    void begin() {
      estaArriba = (digitalRead(pinFCArriba)      == LOW);
      estaAbajo  = (digitalRead(pinFCAbajo)       == LOW);
      lastBtnArr = (digitalRead(pinMarchaArriba)  == HIGH);
      lastBtnBaj = (digitalRead(pinMarchaAbajo)   == HIGH);
      debug("Iniciado. Arriba=" + String(estaArriba) + " Abajo=" + String(estaAbajo));
    }


    // ==========================================
    //   COMANDOS PÚBLICOS
    // ==========================================
    void subir() {
      if (setaPulsada()) { debug("Rechazado: SETA pulsada."); return; }
      if (estaArriba) { debug("Ya esta ARRIBA."); requestMqttUpdate = true; return; }
      debug("CMD: subir");
      cmdSubir = true;
      cmdBajar = false;
    }

    void bajar() {
      if (setaPulsada()) { debug("Rechazado: SETA pulsada."); return; }
      if (estaAbajo) { debug("Ya esta ABAJO."); requestMqttUpdate = true; return; }
      debug("CMD: bajar");
      cmdBajar = true;
      cmdSubir = false;
    }

    void parar() {
      debug("CMD: parar");
      cmdSubir = false;
      cmdBajar = false;
      // El motor se parará en el siguiente gestionarMotor() al no haber marcha activa
    }

    void abrir()  { subir(); }
    void cerrar() { bajar(); }


    // ==========================================
    //   LOOP PRINCIPAL
    // ==========================================
    void update() {
      // --- SEGURIDAD REDUNDANTE: la seta manda SIEMPRE ---
      // Si la seta está LOW (pulsada o cable cortado), forzamos relés a LOW
      // y limpiamos latches ANTES de cualquier otra lógica.
      if (setaPulsada()) {
        digitalWrite(pinMotorArriba, LOW);
        digitalWrite(pinMotorAbajo,  LOW);
        motorSubiendo = false;
        motorBajando  = false;
        cmdSubir      = false;
        cmdBajar      = false;
      }
      gestionarMotor();
      gestionarLeds();
    }


    // ==========================================
    //   ESTADO PARA MQTT
    // ==========================================
    String getEstadoString() {
      String prefijo = id + "_status "; // Esto generará "ru1_status ", "ru2_status "...
      
      if (enEmergencia)  return prefijo + "emergency_pressed";
      if (errorFC)       return prefijo + "error_finales_carrera";
      if (motorSubiendo) return prefijo + "subiendo";
      if (motorBajando)  return prefijo + "bajando";
      if (estaArriba)    return prefijo + "arriba";
      if (estaAbajo)     return prefijo + "abajo";
      return prefijo + "parado";
    }

    // 100 = arriba | 0 = abajo | 50 = intermedio desconocido
    int getPorcentajeEntero() {
      if (estaArriba) return 100;
      if (estaAbajo)  return 0;
      return 50;
    }

    bool checkMqttRequest() {
      if (requestMqttUpdate) { requestMqttUpdate = false; return true; }
      return false;
    }

    void imprimirEstado() {
      debug("Estado: " + getEstadoString() +
            " | Arriba=" + String(estaArriba) +
            " | Abajo="  + String(estaAbajo));
    }
};

#endif
