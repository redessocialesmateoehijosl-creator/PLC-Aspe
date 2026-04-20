#ifndef MOTOR_H
#define MOTOR_H

// ============================================================
//  Motor.h — Adaptado para M-Duino 21+ (Arduino Mega 2560)
//
//  Cambios respecto a la versión ESP32:
//   • ESP32Encoder  → Encoder (PJRC, usa INT hardware en pins 2/3)
//   • Preferences   → EEPROM (EEPROM.put / EEPROM.get)
//   • std::function → puntero de función simple
//   • String literal → F() macro donde es directo (ahorra RAM)
// ============================================================

#include <Arduino.h>
#include <Encoder.h>        // PJRC — instalar desde Gestor de Librerías
#include <EEPROM.h>
#include "Constantes.h"
#include "VFD_Control.h"

// ============================================================
//  MENSAJES DE ESTADO (publicados por MQTT)
// ============================================================
const char* MSG_EMERGENCIA    = "status emergency_pressed";
const char* MSG_NO_CALIBRADO  = "status no_calibrado";
const char* MSG_CALIBRANDO    = "status calibrando";
const char* MSG_ABRIENDO      = "status abriendo";
const char* MSG_CERRANDO      = "status cerrando";
const char* MSG_ABIERTO       = "status opened";
const char* MSG_CERRADO       = "status closed";
const char* MSG_PARADO        = "status stopped";
const char* MSG_ERROR_LIMITE  = "status error_limite";
const char* MSG_ERROR_ATASCO  = "status error_encoder";
const char* PREFIJO_POS       = "pos ";

// ============================================================
//  MAPA DE EEPROM  (22 bytes por motor, a partir de eepBase)
//
//  Offset  0 : magic byte (0xA5 = bloque inicializado)
//  Offset  1 : isTimeMode (byte: 1=tiempo, 0=encoder)
//  Offset  2 : pulsos100  (long,  4 bytes)
//  Offset  6 : posEnc     (long,  4 bytes)
//  Offset 10 : timeTotal  (ulong, 4 bytes)
//  Offset 14 : posTime    (ulong, 4 bytes)
//  Offset 18 : antiEnc    (long,  4 bytes)
// ============================================================
#define EEP_MAGIC       0
#define EEP_TIMEMODE    1
#define EEP_PULSOS100   2
#define EEP_POSENC      6
#define EEP_TIMETOTAL   10
#define EEP_POSTIME     14
#define EEP_ANTIENC     18
#define EEP_BLOCK_SIZE  22
#define EEP_MAGIC_VAL   ((byte)0xA5)

struct DatosMotor {
  long  valor;
  float porcentaje;
};

class Motor {
  private:
    String id;

    // --- PINES ---
    // pinAbrir / pinCerrar eliminados: ahora se usa VFD por Modbus
    int pinRojo, pinVerde, pinNaranja;
    int pinBtnAbrir, pinBtnCerrar, pinBtnCalib, pinSeta;

    // --- VFD MODBUS ---
    VFDController* vfd = nullptr;

    // Helpers internos para no repetir null-check en cada llamada
    void vfdAdelante() { if (vfd) vfd->marchaAdelante(); }
    void vfdAtras()    { if (vfd) vfd->marchaAtras();    }
    void vfdParar()    { if (vfd) vfd->parar();          }

    // Lectura directa de la seta (NC: LOW = pulsada o cable cortado)
    // Se usa en TODAS las funciones que podrían arrancar el motor, para
    // garantizar que ninguna orden llegue al VFD si la seta no da señal.
    bool setaPulsada() { return digitalRead(pinSeta) == LOW; }

    // --- ENTRADAS EXTERNAS (botonera maestra del grupo) ---
    bool extBtnAbrir  = false;
    bool extBtnCerrar = false;
    bool extBtnCalib  = false;

    // --- ENCODER (PJRC) ---
    Encoder* enc;           // Reservado en heap en el constructor

    // --- EEPROM ---
    int eepBase;            // Dirección base del bloque de este motor

    // --- ESTADO INTERNO ---
    bool modoTiempo        = false;
    bool motorEnMovimiento = false;
    int  sentidoGiro       = 0;
    bool requestMqttUpdate = false;

    // Variables Encoder / Tiempo
    long          pulsos100            = 0;
    long          pulsosObjetivo       = 0;
    bool          moviendoAutomatico   = false;
    unsigned long tiempoTotalRecorrido = 0;
    unsigned long posicionActualTiempo = 0;
    unsigned long tiempoInicioMovimiento = 0;

    // Último objetivo de un movimiento AUTOMÁTICO (0..100) o -1 si la última
    // orden fue manual. Se usa en getEstadoString() para decidir si, al
    // parar, estamos "en el extremo" aunque el encoder no haya tocado
    // exactamente pulsos100 (por freno anticipado + inercia).
    int porcUltimoObjetivoAuto = -1;
    int  ultimoObjetivoPctDebug = -1;
    long ultimoObjetivoPulsosDebug = 0;

    // Anticipación (freno anticipado)
    long pulsosAnticipacion = 50;

    // Calibración
    bool          calibrando            = false;
    bool          esperandoInicio       = false;
    bool          midiendoTiempo        = false;
    bool          tiempoInvertido       = false;
    bool          buscandoBajar         = false;
    unsigned long tiempoCalibAcumulado  = 0;   // suma de segmentos reales de movimiento

    // Movimiento manual iniciado por comando externo (serial/red)
    // Evita que gestionarEntradas() lo detenga automáticamente
    bool manualLatch      = false;

    // Botones
    bool lastBtnAbrir  = false;
    bool lastBtnCerrar = false;
    bool lastBtnCalib  = false;
    int  pasoCalibracion = 0;

    // Long-press / cancelación de calibración
    unsigned long tiempoBtnCalibPressed = 0;
    bool          cancelPendiente       = false;
    static const unsigned long UMBRAL_LONG_PRESS = 2000; // 2 s para cancelar

    // Luces
    unsigned long lastBlinkTime     = 0;      // parpadeo normal / lento
    bool          blinkState        = false;
    unsigned long lastFastBlinkTime = 0;      // parpadeo rápido (paso 3 calib)
    bool          fastBlinkState    = false;
    bool          enEmergencia    = false;
    bool          errorLimite     = false;
    unsigned long finCalibracionTime = 0;
    bool          mostrandoExito  = false;
    // Indicación visual breve al cancelar calibración (LED rojo parpadeando)
    bool          mostrandoCancel = false;
    unsigned long tiempoCancel    = 0;

    // Cadencias de parpadeo (ms entre flancos)
    static const unsigned long BLINK_LENTO  = 400;
    static const unsigned long BLINK_RAPIDO = 120;

    bool movimientoPorRed = false;

    // Inercia / parada real (confirmación por encoder — solo modo encoder)
    bool          esperandoParadaReal = false;
    long          lastEncValStop      = 0;
    unsigned long lastEncTimeStop     = 0;
    const int TIEMPO_ESTABILIZACION   = 300;
    long          pulsosAlMandarStop  = 0;

    // Confirmación de parada REAL (encoder + VFD).
    // Entre parar() y la confirmación, getEstadoString() sigue devolviendo
    // "abriendo"/"cerrando" y el requestMqttUpdate queda pendiente,
    // para que MQTT no publique "stopped" durante la deceleración.
    bool          confirmandoParada        = false;
    unsigned long inicioConfirmacionParada = 0;
    unsigned long ultimaSolicitudVFD       = 0;
    bool          vfdReporteParado         = false;
    static const unsigned long INTERVALO_POLL_VFD     = 250;   // ms entre lecturas 2101H
    static const unsigned long TIMEOUT_CONFIRM_PARADA = 5000;  // ms máx. esperando confirmación

    // Watchdog encoder (atasco)
    bool          errorAtasco       = false;
    long          lastPosCheck      = 0;
    unsigned long lastMoveTime      = 0;
    const int TIEMPO_GRACIA_ARRANQUE = 1000;
    const int TIEMPO_MAX_SIN_PULSOS  = 1500;

    // Detector de ruido encoder (ESD / glitch eléctrico)
    // Cuando el motor está completamente parado, el encoder no debería
    // cambiar. Si salta más de MAX_SALTO_PARADO pulsos en un solo ciclo
    // de loop, es un glitch (ESD al conectar/desconectar cables, ruido
    // en las líneas de interrupción). En ese caso se restaura la última
    // posición válida y se loguea, evitando que el sistema intente mover
    // el motor hacia una posición absurda.
    long          _encPosValida     = 0;    // última posición confirmada con motor parado
    bool          _encPosValidaOK   = false; // false hasta el primer ciclo de reposo
    static const long MAX_SALTO_PARADO = 30; // pulsos; imposible mecánicamente en reposo

    // --- LOGGER ---
    // En Mega no hay std::function — usamos puntero de función simple
    typedef void (*LogCallback)(String);
    LogCallback externalLog = nullptr;

    void debug(String msg) {
      if (externalLog) externalLog("[" + id + "] " + msg);
    }

    // ============================================================
    //  HELPERS EEPROM
    // ============================================================
    bool eepInitializado() {
      byte m; EEPROM.get(eepBase + EEP_MAGIC, m); return (m == EEP_MAGIC_VAL);
    }
    void eepMarcarInit() { EEPROM.put(eepBase + EEP_MAGIC, EEP_MAGIC_VAL); }

    bool eepGetBool(int offset, bool def) {
      if (!eepInitializado()) return def;
      byte v; EEPROM.get(eepBase + offset, v); return (v == 1);
    }
    void eepPutBool(int offset, bool val) {
      eepMarcarInit();
      EEPROM.put(eepBase + offset, (byte)(val ? 1 : 0));
    }
    long eepGetLong(int offset, long def) {
      if (!eepInitializado()) return def;
      long v; EEPROM.get(eepBase + offset, v); return v;
    }
    void eepPutLong(int offset, long val) {
      eepMarcarInit(); EEPROM.put(eepBase + offset, val);
    }
    unsigned long eepGetULong(int offset, unsigned long def) {
      if (!eepInitializado()) return def;
      unsigned long v; EEPROM.get(eepBase + offset, v); return v;
    }
    void eepPutULong(int offset, unsigned long val) {
      eepMarcarInit(); EEPROM.put(eepBase + offset, val);
    }

    // ============================================================
    //  VERIFICACIÓN DE LÍMITES
    // ============================================================
    void verificarLimitesSeguridad() {
      if (calibrando || modoTiempo || !motorEnMovimiento) return;
      long pos = enc->read();

      if (pulsos100 > 0) {
        if (pos > pulsos100 && sentidoGiro == 1)  { debug(F("!!! ERROR: Limite Superior (+) !!!")); parar(); errorLimite = true; }
        if (pos < 0        && sentidoGiro == -1)  { debug(F("!!! ERROR: Limite Inferior (+) !!!")); parar(); errorLimite = true; }
      } else {
        if (pos < pulsos100 && sentidoGiro == -1) { debug(F("!!! ERROR: Limite Superior (-) !!!")); parar(); errorLimite = true; }
        if (pos > 0         && sentidoGiro == 1)  { debug(F("!!! ERROR: Limite Inferior (-) !!!")); parar(); errorLimite = true; }
      }
    }

    // ============================================================
    //  DETECTOR DE RUIDO EN ENCODER  (ESD / glitch eléctrico)
    //
    //  Llamar SOLO cuando el motor está completamente parado y no
    //  hay ninguna fase de frenado activa. Si el encoder salta más de
    //  MAX_SALTO_PARADO pulsos en un ciclo, es imposible mecánicamente
    //  → descarga ESD o ruido en el cableado. Se restaura la posición.
    // ============================================================
    void detectarRuidoEncoder() {
      // Solo actúa cuando el motor está en reposo total
      if (motorEnMovimiento || calibrando || esperandoParadaReal || confirmandoParada) {
        // Motor en movimiento o frenando: actualizar referencia continuamente
        _encPosValida  = enc->read();
        _encPosValidaOK = true;
        return;
      }
      if (modoTiempo) return;   // en modo tiempo no se usa el encoder para posición

      long posActual = enc->read();

      if (!_encPosValidaOK) {
        // Primera vez que entramos en reposo: fijar la referencia
        _encPosValida  = posActual;
        _encPosValidaOK = true;
        return;
      }

      long salto = abs(posActual - _encPosValida);
      if (salto > MAX_SALTO_PARADO) {
        debug("!! RUIDO ENCODER (ESD?): salto de " + String(salto) +
              " pulsos en reposo. Restaurando pos " + String(_encPosValida) + ".");
        enc->write(_encPosValida);   // restaurar última posición válida
        // NO ponemos errorAtasco: el sistema sigue operativo con la posición corregida
      } else {
        _encPosValida = posActual;   // posición estable → actualizar referencia
      }
    }

    // ============================================================
    //  WATCHDOG DE ENCODER  (atasco / rotura)
    // ============================================================
    void verificarAtasco() {
      if (!motorEnMovimiento || modoTiempo || calibrando) return;

      if (millis() - tiempoInicioMovimiento < (unsigned long)TIEMPO_GRACIA_ARRANQUE) {
        lastMoveTime = millis();
        lastPosCheck = enc->read();
        return;
      }

      long posActual = enc->read();

      if (abs(posActual - lastPosCheck) > 2) {
        lastMoveTime = millis();
        lastPosCheck = posActual;
      } else {
        if (millis() - lastMoveTime > (unsigned long)TIEMPO_MAX_SIN_PULSOS) {
          debug(F("!!! ERROR FATAL: ENCODER NO RESPONDE !!!"));
          parar();
          errorAtasco = true;
          // Descalibración forzosa por seguridad
          pulsos100 = 0;
          eepPutLong(EEP_PULSOS100, 0L);
          debug(F(">> SEGURIDAD: Sistema marcado como NO CALIBRADO."));
        }
      }
    }

    // ============================================================
    //  GESTIÓN DE LUCES
    // ============================================================
    void gestionarLuces() {
      unsigned long now = millis();
      if (now - lastBlinkTime     > BLINK_LENTO)  { blinkState     = !blinkState;     lastBlinkTime     = now; }
      if (now - lastFastBlinkTime > BLINK_RAPIDO) { fastBlinkState = !fastBlinkState; lastFastBlinkTime = now; }

      if (enEmergencia || errorLimite || errorAtasco) {
        digitalWrite(pinRojo, blinkState); digitalWrite(pinVerde, LOW); digitalWrite(pinNaranja, LOW); return;
      }
      if (mostrandoExito) {
        if (now - finCalibracionTime < 3000) {
          digitalWrite(pinRojo, blinkState); digitalWrite(pinVerde, blinkState); digitalWrite(pinNaranja, blinkState);
        } else { mostrandoExito = false; }
        return;
      }
      if (calibrando) {
        // Paso 1 → parpadeo lento  (buscando el 0%, "cerrado")
        // Paso 2 → fijo            (0% marcado, buscando el 100%, "abierto")
        // Paso 3 → parpadeo rápido (100% marcado, esperando salir de calibración)
        if      (pasoCalibracion == 2) digitalWrite(pinNaranja, HIGH);
        else if (pasoCalibracion == 3) digitalWrite(pinNaranja, fastBlinkState);
        else                           digitalWrite(pinNaranja, blinkState);
        digitalWrite(pinRojo, LOW); digitalWrite(pinVerde, LOW); return;
      }
      if (motorEnMovimiento) {
        digitalWrite(pinVerde, blinkState); digitalWrite(pinRojo, LOW); digitalWrite(pinNaranja, LOW); return;
      }

      bool sistemaCalibrado = (!modoTiempo && pulsos100 != 0) || (modoTiempo && tiempoTotalRecorrido > 0);
      if (sistemaCalibrado) {
        digitalWrite(pinVerde, HIGH); digitalWrite(pinRojo, HIGH); digitalWrite(pinNaranja, LOW);
      } else {
        digitalWrite(pinVerde, LOW); digitalWrite(pinRojo, HIGH); digitalWrite(pinNaranja, LOW);
      }
    }

    // ============================================================
    //  GESTIÓN DE ENTRADAS
    // ============================================================
    void gestionarEntradas() {
      // Seta de emergencia — prioridad absoluta
      if (digitalRead(pinSeta) == LOW) {
        if (!enEmergencia) { debug(F("!!! EMERGENCIA !!!")); parar(); enEmergencia = true; }
        return;
      } else {
        if (enEmergencia) {
          debug(F("Emergencia OFF."));
          enEmergencia = false;
          if (errorLimite || errorAtasco) {
            errorLimite = false; errorAtasco = false;
            debug(F(">> REARME: Errores limpiados."));
          }
        }
      }

      // Combinar botón local con entrada externa del grupo
      bool btnA   = digitalRead(pinBtnAbrir)  || extBtnAbrir;
      bool btnC   = digitalRead(pinBtnCerrar) || extBtnCerrar;
      bool btnCal = digitalRead(pinBtnCalib)  || extBtnCalib;

      // Botón calibrar (flanco ascendente) — flujo de 4 pulsaciones:
      //   1ª pulsación (paso 0): entrar en calibración  → paso 1
      //   2ª pulsación (paso 1): marcar 0% ("cerrado")  → paso 2
      //   3ª pulsación (paso 2): marcar 100% ("abierto")→ paso 3
      //   4ª pulsación (paso 3): salir y guardar        → paso 0
      if (btnCal && !lastBtnCalib) {
        if      (pasoCalibracion == 0) { debug(F(">> Calibrar..."));    calibrar();     }
        else if (pasoCalibracion == 1) { debug(F(">> Set 0..."));       setZero();      }
        else if (pasoCalibracion == 2) { debug(F(">> Set 100..."));     set100();       }
        else if (pasoCalibracion == 3) { debug(F(">> Fin calibracion"));finCalibrado(); }
      }
      lastBtnCalib = btnCal;

      if (calibrando || modoTiempo || errorAtasco) {
        if (btnA) {
          manualLatch = false;   // botón toma el control: desactiva latch externo
          abrirManual();
        } else if (btnC) {
          manualLatch = false;
          cerrarManual();
        } else {
          // Solo parar si NO fue un comando externo (serial/red)
          if (motorEnMovimiento && !moviendoAutomatico && !manualLatch) parar();
        }
      } else {
        if ( btnA && !lastBtnAbrir)  { debug(F("Btn: Auto Abrir"));  moverA(0);   }
        if (!btnA &&  lastBtnAbrir)  { debug(F("Btn: Stop"));        parar();     }
        if ( btnC && !lastBtnCerrar) { debug(F("Btn: Auto Cerrar")); moverA(100); }
        if (!btnC &&  lastBtnCerrar) { debug(F("Btn: Stop"));        parar();     }
      }
      lastBtnAbrir  = btnA;
      lastBtnCerrar = btnC;
    }

    bool debeSumarTiempo() {
      if (calibrando && midiendoTiempo) return true;
      return (sentidoGiro == 1);
    }

    unsigned long getPosicionTiempoEstimada() {
      if (!modoTiempo) return 0;
      if (!motorEnMovimiento) return posicionActualTiempo;
      unsigned long delta = millis() - tiempoInicioMovimiento;
      long estimada;
      if (debeSumarTiempo()) estimada = (long)posicionActualTiempo + (long)delta;
      else                   estimada = (long)posicionActualTiempo - (long)delta;
      if (estimada < 0) return 0;
      if ((unsigned long)estimada > tiempoTotalRecorrido) return tiempoTotalRecorrido;
      return (unsigned long)estimada;
    }

    void cargarDatosDeMemoria() {
      pulsosAnticipacion = eepGetLong(EEP_ANTIENC, 50L);

      if (modoTiempo) {
        tiempoTotalRecorrido = eepGetULong(EEP_TIMETOTAL, 0UL);
        posicionActualTiempo = eepGetULong(EEP_POSTIME,   0UL);
        debug("Restaurado TIEMPO (Pos:" + String(posicionActualTiempo) + "/Total:" + String(tiempoTotalRecorrido) + ")");
      } else {
        pulsos100 = eepGetLong(EEP_PULSOS100, 0L);
        long savedPos = eepGetLong(EEP_POSENC, 0L);
        enc->write(savedPos);
        debug("Restaurado ENCODER (Pos:" + String(savedPos) + "/Total:" + String(pulsos100) + ")");
        debug("Anticipacion: " + String(pulsosAnticipacion) + " pulsos");
      }
    }

  public:
    // ============================================================
    //  CONSTRUCTOR
    //  eepromBase: dirección base en EEPROM (0, 22, 44... según motor)
    // ============================================================
    Motor(const __FlashStringHelper* nombreUnico,
          int encA, int encB,
          int pR, int pV, int pN,
          int bA, int bC, int bCal, int bSeta,
          int eepromBase = 0)
    {
      id      = String(nombreUnico);
      eepBase = eepromBase;

      pinRojo    = pR;  pinVerde   = pV;  pinNaranja = pN;
      pinBtnAbrir  = bA;  pinBtnCerrar = bC;
      pinBtnCalib  = bCal; pinSeta = bSeta;

      // Salidas (solo LEDs — motor controlado por VFD Modbus)
      pinMode(pinRojo,    OUTPUT); pinMode(pinVerde,    OUTPUT); pinMode(pinNaranja, OUTPUT);

      // Entradas
      pinMode(pinBtnAbrir,  INPUT); pinMode(pinBtnCerrar, INPUT);
      pinMode(pinBtnCalib,  INPUT); pinMode(pinSeta,       INPUT);

      // Encoder PJRC — reserva en heap, usa interrupciones hardware de INT0/INT1
      enc = new Encoder(encA, encB);
    }

    void setVFD(VFDController* v) { vfd = v; }

    void setLogger(LogCallback callback) { externalLog = callback; }

    void resetErroresVFD() {
      if (vfd) { vfd->resetErrores(); debug(F(">> VFD: Reset de errores enviado.")); }
      else      { debug(F(">> VFD: no asignado.")); }
    }

    void begin() {
      modoTiempo = eepGetBool(EEP_TIMEMODE, false);
      cargarDatosDeMemoria();
    }

    void setAnticipacion(long pulsos) {
      pulsosAnticipacion = pulsos;
      eepPutLong(EEP_ANTIENC, pulsosAnticipacion);
      debug("Anticipacion: " + String(pulsosAnticipacion) + " pulsos");
    }

    void setModo(bool esTiempo) {
      if (modoTiempo == esTiempo) return;
      parar();
      modoTiempo = esTiempo;
      eepPutBool(EEP_TIMEMODE, modoTiempo);
      // Reset completo de calibración
      pulsos100 = 0;             eepPutLong(EEP_PULSOS100,  0L);
      tiempoTotalRecorrido = 0;  eepPutULong(EEP_TIMETOTAL, 0UL);
      posicionActualTiempo = 0;  eepPutULong(EEP_POSTIME,   0UL);
      enc->write(0L);            eepPutLong(EEP_POSENC,     0L);
      porcUltimoObjetivoAuto = -1;
      ultimoObjetivoPctDebug = -1;
      ultimoObjetivoPulsosDebug = 0;
      debug(F("CAMBIO DE MODO: Memoria reseteada. Sistema NO CALIBRADO."));
    }

    void setEntradasExternas(bool ab, bool ce, bool cal) {
      extBtnAbrir  = ab;
      extBtnCerrar = ce;
      extBtnCalib  = cal;
    }

    // ============================================================
    //  UPDATE  (llamar cada ciclo de loop)
    // ============================================================
    void update() {
      // --- SEGURIDAD REDUNDANTE: la seta manda SIEMPRE ---
      // Si la seta está LOW (pulsada o cable cortado), cortamos el VFD
      // inmediatamente en cada iteración, sin depender del flag software.
      if (setaPulsada()) {
        vfdParar();
        if (motorEnMovimiento) {
          motorEnMovimiento  = false;
          moviendoAutomatico = false;
          manualLatch        = false;
        }
      }

      gestionarLuces();
      gestionarEntradas();
      detectarRuidoEncoder();   // filtra glitches ESD antes de que lleguen a la lógica
      verificarAtasco();

      // Esperar a que el motor se frene de verdad antes de guardar posición
      if (esperandoParadaReal && !modoTiempo) {
        long lecturaActual = enc->read();
        if (lecturaActual != lastEncValStop) {
          // El motor SIGUE moviéndose tras haber mandado parar.
          // Causa típica: la 1ª orden vfdParar() se perdió porque el bus
          // Modbus estaba ocupado (heartbeat o lectura de estado).
          // Reinsistimos en cada iteración: isBusy() filtra; el comando
          // acaba llegando al variador en cuanto se libere el bus.
          vfdParar();
          lastEncValStop   = lecturaActual;
          lastEncTimeStop  = millis();
        } else {
          if (millis() - lastEncTimeStop > (unsigned long)TIEMPO_ESTABILIZACION) {
            eepPutLong(EEP_POSENC, lecturaActual);
            esperandoParadaReal = false;
            debug(">> Motor FRENADO. Pos guardada: " + String(lecturaActual));
            // Fijar referencia antiruido: a partir de aquí el motor está parado
            // y cualquier cambio brusco en el encoder será un glitch ESD.
            _encPosValida  = lecturaActual;
            _encPosValidaOK = true;

            // --- CÁLCULO ---
            long inercia = abs(lecturaActual - pulsosAlMandarStop);
            debug(">> [DEBUG INERCIA] Motor FRENADO REAL. Pos final: " + String(lecturaActual) + " | Inercia total: " + String(inercia) + " pulsos.");
            // ----------------------------------

             // --- DEBUG OBJETIVO vs REAL ---
            if (ultimoObjetivoPctDebug >= 0) {
              long errorPulsos = lecturaActual - ultimoObjetivoPulsosDebug;
              debug(">> [DEBUG OBJETIVO] Objetivo: " + String(ultimoObjetivoPctDebug) +
                    "% | Pulsos teoricos: " + String(ultimoObjetivoPulsosDebug) +
                    " | Pulsos reales: " + String(lecturaActual) +
                    " | Error: " + String(errorPulsos) + " pulsos");
            }

            if (ultimoObjetivoPctDebug >= 0 && pulsos100 != 0) {
              float pctReal = (100.0f * (float)lecturaActual) / (float)pulsos100;
              float pctError = pctReal - (float)ultimoObjetivoPctDebug;

              debug(">> [DEBUG PORCENTAJE] Objetivo: " + String(ultimoObjetivoPctDebug) +
                    "% | Real: " + String(pctReal, 2) +
                    "% | Error: " + String(pctError, 2) + "%");
            }


          }
        }
      }

      // --- Confirmación de parada REAL ---------------------------------
      // Tras parar(), no publicamos "stopped" hasta que encoder Y VFD
      // confirman que el motor ya no gira. Sin esto, MQTT publica el
      // estado final mientras el motor aún está decelerando (resultado:
      // "stopped" en vez de "opened"/"closed" si pararon en el extremo).
      if (confirmandoParada) {
        unsigned long now = millis();

        // Polling activo al VFD (2101H) — más agresivo que el heartbeat
        if (vfd && (now - ultimaSolicitudVFD > INTERVALO_POLL_VFD)) {
          if (vfd->actualizar()) ultimaSolicitudVFD = now;
          // Si isBusy, lo reintentaremos en la siguiente iteración
        }

        // El variador confirma parada vía 2101H (Bit12="En funcionamiento"=0)
        if (vfd && !vfd->motorGirando()) vfdReporteParado = true;

        // Encoder: en modo tiempo no hay feedback de encoder → damos OK.
        bool encoderOK = modoTiempo || !esperandoParadaReal;
        // VFD: si no hay VFD asignado, no podemos comprobar → damos OK.
        bool vfdOK     = (vfd == nullptr) || vfdReporteParado;

        if (encoderOK && vfdOK) {
          confirmandoParada = false;
          requestMqttUpdate = true;
          debug(F(">> Parada CONFIRMADA (encoder + VFD). Publicando estado."));
        } else if (now - inicioConfirmacionParada > TIMEOUT_CONFIRM_PARADA) {
          confirmandoParada = false;
          requestMqttUpdate = true;
          debug("!! TIMEOUT confirmacion parada (" +
                String(encoderOK ? "enc OK" : "enc NO") + ", " +
                String(vfdOK     ? "vfd OK" : "vfd NO") + "). Publicando igualmente.");
        }
      }

      if (!motorEnMovimiento || calibrando || enEmergencia || errorLimite || errorAtasco) return;

      // Fin de carrera automático — modo TIEMPO
      if (modoTiempo && moviendoAutomatico) {
        unsigned long delta    = millis() - tiempoInicioMovimiento;
        long          posEstim = debeSumarTiempo()
                                   ? (long)posicionActualTiempo + (long)delta
                                   : (long)posicionActualTiempo - (long)delta;
        if ( debeSumarTiempo() && posEstim >= (long)tiempoTotalRecorrido) { debug(F("Fin carrera (Abierto).")); parar(); }
        if (!debeSumarTiempo() && posEstim <= 0)                          { debug(F("Fin carrera (Cerrado).")); parar(); }
      }
      // Fin de carrera automático — modo ENCODER
      else if (!modoTiempo && moviendoAutomatico) {
        long pulsosActuales = enc->read();
        if ( buscandoBajar && pulsosActuales <= (pulsosObjetivo + pulsosAnticipacion)) { debug(F("Destino alcanzado.")); parar(); }
        if (!buscandoBajar && pulsosActuales >= (pulsosObjetivo - pulsosAnticipacion)) { debug(F("Destino alcanzado.")); parar(); }
      }
    }

    // ============================================================
    //  ACCIONES  (con seguridad incorporada)
    // ============================================================
    void abrir() {
      movimientoPorRed = true;
      esperandoParadaReal = false;
      confirmandoParada = false;
      if (setaPulsada()) { if (motorEnMovimiento) parar(); return; }
      if (enEmergencia || errorLimite) return;

      if (calibrando) {
        if (modoTiempo && esperandoInicio) {
          tiempoInicioMovimiento = millis(); esperandoInicio = false;
          midiendoTiempo = true; tiempoInvertido = false;
          debug(F(">> CAL: Midiendo (Abriendo)..."));
        }
        moviendoAutomatico = true;
      } else if (!modoTiempo) { moverA(100); return; }
      else {
        if (posicionActualTiempo >= tiempoTotalRecorrido) {
          debug(F(">> Ignorado: Ya ABIERTO.")); requestMqttUpdate = true; return;
        }
        tiempoInicioMovimiento = millis(); moviendoAutomatico = true;
        porcUltimoObjetivoAuto = 100;
      }

      // Abrir: adelante normal, atrás si invertido
      if (!modoTiempo || !tiempoInvertido) vfdAdelante(); else vfdAtras();
      motorEnMovimiento = true; sentidoGiro = 1;
    }

    void cerrar() {
      movimientoPorRed = true;
      esperandoParadaReal = false;
      confirmandoParada = false;
      if (setaPulsada()) { if (motorEnMovimiento) parar(); return; }
      if (enEmergencia || errorLimite) return;

      if (calibrando) {
        if (modoTiempo && esperandoInicio) {
          tiempoInicioMovimiento = millis(); esperandoInicio = false;
          midiendoTiempo = true; tiempoInvertido = true;
          debug(F(">> CAL: Midiendo (Cerrando)..."));
        }
        moviendoAutomatico = true;
      } else if (!modoTiempo) { moverA(0); return; }
      else {
        if (posicionActualTiempo == 0) {
          debug(F(">> Ignorado: Ya CERRADO.")); requestMqttUpdate = true; return;
        }
        tiempoInicioMovimiento = millis(); moviendoAutomatico = true;
        porcUltimoObjetivoAuto = 0;
      }

      // Cerrar: atrás normal, adelante si invertido
      if (!modoTiempo || !tiempoInvertido) vfdAtras(); else vfdAdelante();
      motorEnMovimiento = true; sentidoGiro = -1;
    }

    // esRemoto=true  → orden venida de la red (MQTT/m<N>): activa watchdog heartbeat
    // esRemoto=false → orden local (botón físico):          NO activa watchdog
    void moverA(int porcentaje, bool esRemoto = false) {
      esperandoParadaReal = false;
      confirmandoParada = false;
      if (setaPulsada()) {
        if (motorEnMovimiento) parar();
        debug(F("moverA rechazado (SETA pulsada).")); return;
      }
      if (modoTiempo || calibrando || enEmergencia || errorLimite || errorAtasco) {
        debug(F("moverA rechazado (Error o Estado).")); return;
      }
      long porcentajeLim = constrain((long)porcentaje, 0L, 100L);
      pulsosObjetivo = (pulsos100 * porcentajeLim) / 100L;
      porcUltimoObjetivoAuto = (int)porcentajeLim;

      // Guardar referencia teórica para debug final
      ultimoObjetivoPctDebug = (int)porcentajeLim;
      ultimoObjetivoPulsosDebug = pulsosObjetivo;

      long pulsosActuales = enc->read();
      debug("Auto a " + String(porcentajeLim) + "% | Objetivo teorico: " + String(pulsosObjetivo) + " pulsos");
      if (pulsosActuales < pulsosObjetivo) {
        buscandoBajar = false;
        vfdAtras();
        motorEnMovimiento = true; sentidoGiro = -1; moviendoAutomatico = true;
      } else if (pulsosActuales > pulsosObjetivo) {
        buscandoBajar = true;
        vfdAdelante();
        motorEnMovimiento = true; sentidoGiro = 1; moviendoAutomatico = true;
      } else {
        debug(F("Ya en posicion destino."));
        ultimoObjetivoPctDebug = -1;
        ultimoObjetivoPulsosDebug = 0;
        requestMqttUpdate = true;
      }
      if (esRemoto) movimientoPorRed = true;   // solo activa watchdog si la orden es remota
    }

    void abrirManual() {
      if (setaPulsada()) { if (motorEnMovimiento) parar(); return; }
      if (enEmergencia) return;
      if (!motorEnMovimiento || sentidoGiro != 1) {
        moviendoAutomatico = false;
        porcUltimoObjetivoAuto = -1;        // manual: no hay "destino auto"

        ultimoObjetivoPctDebug = -1;
        ultimoObjetivoPulsosDebug = 0;

        tiempoInicioMovimiento = millis();
        vfdAdelante();
        motorEnMovimiento = true; sentidoGiro = 1;
      }
    }

    void cerrarManual() {
      if (setaPulsada()) { if (motorEnMovimiento) parar(); return; }
      if (enEmergencia) return;
      if (!motorEnMovimiento || sentidoGiro != -1) {
        moviendoAutomatico = false;
        porcUltimoObjetivoAuto = -1;        // manual: no hay "destino auto"

        ultimoObjetivoPctDebug = -1;
        ultimoObjetivoPulsosDebug = 0;

        tiempoInicioMovimiento = millis();
        vfdAtras();
        motorEnMovimiento = true; sentidoGiro = -1;
      }
    }

    // Versiones para comandos externos (serial / red):
    // activan manualLatch para que gestionarEntradas() no pare el motor.
    // En modo TIEMPO + calibración (paso 2), arrancan/reanudan el cronómetro.
    void abrirManualCmd() {
      manualLatch = true;
      if (calibrando && modoTiempo && pasoCalibracion >= 2 && !midiendoTiempo) {
        tiempoInicioMovimiento = millis();
        midiendoTiempo  = true;
        tiempoInvertido = false;
        debug(F(">> CAL: Cronometro ON (Abriendo)..."));
      }
      abrirManual();
    }
    void cerrarManualCmd() {
      manualLatch = true;
      if (calibrando && modoTiempo && pasoCalibracion >= 2 && !midiendoTiempo) {
        tiempoInicioMovimiento = millis();
        midiendoTiempo  = true;
        tiempoInvertido = true;
        debug(F(">> CAL: Cronometro ON (Cerrando)..."));
      }
      cerrarManual();
    }

    void parar() {
      // SIEMPRE reenviamos STOP al variador. Es idempotente y nos protege
      // del caso: parar() ya se ejecutó antes pero vfdParar() se perdió
      // por bus ocupado, y el motor seguía rampando. Así el usuario puede
      // forzar un nuevo intento pulsando Stop aunque el flag interno
      // diga que ya estaba parado.
      vfdParar();

      if (!motorEnMovimiento && !calibrando) {
        // Si además estábamos esperando a que frenara de verdad, forzamos
        // que la espera de estabilización se reevalúe desde cero.
        if (esperandoParadaReal) {
          lastEncValStop  = enc->read();
          lastEncTimeStop = millis();
        }
        return;
      }

      // Guardar posición en modos automáticos
      if (modoTiempo && moviendoAutomatico) {
        unsigned long delta = millis() - tiempoInicioMovimiento;
        if (debeSumarTiempo()) posicionActualTiempo += delta;
        else {
          if (delta > posicionActualTiempo) posicionActualTiempo = 0;
          else                              posicionActualTiempo -= delta;
        }
        eepPutULong(EEP_POSTIME, posicionActualTiempo);
        debug("Pos guardada (Tiempo): " + String(posicionActualTiempo));
      } else if (!modoTiempo) {
        // Esperamos a que el motor se frene de verdad (inercia)
        esperandoParadaReal = true;
        lastEncValStop  = enc->read();
        pulsosAlMandarStop = lastEncValStop;
        lastEncTimeStop = millis();
        debug(">> [DEBUG INERCIA] STOP mandado. Pulsos iniciales: " + String(pulsosAlMandarStop)); // <--- AÑADE ESTE LOG
      }

      // En calibración modo tiempo: acumular segmento y congelar cronómetro
      if (calibrando && modoTiempo && midiendoTiempo) {
        tiempoCalibAcumulado += millis() - tiempoInicioMovimiento;
        midiendoTiempo = false;
        debug(">> CAL pausa. Acumulado: " + String(tiempoCalibAcumulado) + " ms");
      }

      vfdParar();
      bool estabaMoviendo = motorEnMovimiento && (sentidoGiro != 0);
      motorEnMovimiento  = false;
      moviendoAutomatico = false;
      movimientoPorRed   = false;
      manualLatch        = false;

      // Si el motor estaba girando, NO publicamos "stopped" todavía: hay
      // deceleración física. Iniciamos confirmación por encoder + VFD;
      // update() publicará el estado final cuando ambos confirmen la parada.
      // Durante calibración la publicación intermedia tampoco aporta valor:
      // getEstadoString() ya devolverá MSG_CALIBRANDO mientras calibrando==true.
      if (estabaMoviendo && !calibrando) {
        confirmandoParada        = true;
        inicioConfirmacionParada = millis();
        vfdReporteParado         = false;
        ultimaSolicitudVFD       = 0;        // forzar primer poll inmediato
        debug(F(">> Parada solicitada. Esperando confirmacion (encoder + VFD)..."));
      } else {
        requestMqttUpdate = true;
      }
    }

    // ============================================================
    //  CALIBRACIÓN
    // ============================================================
    void calibrar() {
      if (motorEnMovimiento) parar();
      enc->write(0L);
      calibrando              = true;
      pasoCalibracion         = 1;
      esperandoInicio         = true;
      midiendoTiempo          = false;
      tiempoInvertido         = false;
      tiempoCalibAcumulado    = 0;
      debug(F(">> CALIBRACION iniciada. Mueve a CERRADO y pulsa boton (o '0'). LED naranja: parpadeo lento."));
    }

    void setZero() {
      if (!calibrando) return;
      enc->write(0L);
      tiempoInicioMovimiento  = 0;
      posicionActualTiempo    = 0;
      pasoCalibracion         = 2;
      midiendoTiempo          = false;
      tiempoCalibAcumulado    = 0;   // reinicia el acumulador desde el punto cero
      debug(F(">> Set ZERO OK. Mueve a ABIERTO y pulsa boton (o '1'). LED naranja: fijo."));
    }

    void set100() {
      if (!calibrando || pasoCalibracion < 2) return;
      if (modoTiempo) {
        // Si el motor aún está en marcha, cerrar el último segmento
        if (midiendoTiempo) {
          tiempoCalibAcumulado += millis() - tiempoInicioMovimiento;
          midiendoTiempo = false;
        }
        tiempoTotalRecorrido = tiempoCalibAcumulado;
        debug(">> Set 100% (Tiempo): " + String(tiempoTotalRecorrido) + " ms");
      } else {
        pulsos100 = enc->read();
        debug(">> Set 100% (Encoder): " + String(pulsos100) + " pulsos");
      }
      pasoCalibracion = 3;   // esperando confirmación final (LED naranja parpadeo rápido)
      debug(F(">> 100% marcado. Pulsa boton (o 'f') para SALIR. LED naranja: parpadeo rapido."));
    }

    void finCalibrado() {
      if (!calibrando) return;

      // En modo tiempo: si el usuario olvidó pulsar '1' pero hay tiempo acumulado, lo aplicamos ahora
      if (modoTiempo && tiempoTotalRecorrido == 0) {
        if (tiempoCalibAcumulado > 0) {
          set100();
          debug(F(">> (set100 aplicado automaticamente en finCalibrado)"));
        } else {
          debug(F(">> ERROR: Sin tiempo medido. Mueve el motor y pulsa '1' (o 's' + 'f') primero."));
          return;
        }
      }

      parar();
      calibrando = false;
      pasoCalibracion = 0;

      if (modoTiempo) {
        posicionActualTiempo = tiempoTotalRecorrido;       // en memoria: estamos al 100%
        eepPutULong(EEP_TIMETOTAL, tiempoTotalRecorrido);
        eepPutULong(EEP_POSTIME,   tiempoTotalRecorrido); // en EEPROM: estamos al 100%
      } else {
        eepPutLong(EEP_PULSOS100, pulsos100);
        eepPutLong(EEP_POSENC,    pulsos100);
      }
      // Tras calibrar quedamos en el extremo ABIERTO; que el siguiente
      // getEstadoString() lo reporte como "opened" aunque el encoder haya
      // derivado 1-2 pulsos por ruido.
      porcUltimoObjetivoAuto = 100;

      mostrandoExito     = true;
      finCalibracionTime = millis();
      requestMqttUpdate  = true;
      debug(F(">> CALIBRACIÓN completada."));
    }

    // ============================================================
    //  CONSULTAS DE ESTADO
    // ============================================================
    bool checkMqttRequest() {
      if (requestMqttUpdate) { requestMqttUpdate = false; return true; }
      return false;
    }

    bool esMovimientoRed() { return movimientoPorRed; }

    /*String getEstadoString() {
      if (enEmergencia)                               return String(MSG_EMERGENCIA);
      if (errorAtasco)                                return String(MSG_ERROR_ATASCO);
      if (errorLimite)                                return String(MSG_ERROR_LIMITE);
      if (calibrando)                                 return String(MSG_CALIBRANDO);
      bool sinCalibrar = (!modoTiempo && pulsos100 == 0) || (modoTiempo && tiempoTotalRecorrido == 0);
      if (sinCalibrar)                                return String(MSG_NO_CALIBRADO);
      if (motorEnMovimiento && sentidoGiro == 1)      return String(MSG_ABRIENDO);
      if (motorEnMovimiento && sentidoGiro == -1)     return String(MSG_CERRANDO);

      // --- Motor parado: detectar si estamos en un extremo ---
      // Problema que resuelve este bloque: al llegar en modo AUTO a 0% o 100%,
      // el freno anticipado (pulsosAnticipacion) + la inercia hacen que la
      // posición casi nunca toque exactamente el tope. Resultado: la
      // comparación estricta fallaba y se publicaba "stopped" en vez de
      // "opened"/"closed".
      //
      // Estrategia: usar el porcentaje entero (ya está acotado 0..100 y
      // funciona igual en modo encoder y tiempo):
      //   - Si el último movimiento AUTO iba a 100 y estamos a ≥95 %: ABIERTO.
      //   - Si el último movimiento AUTO iba a 0   y estamos a ≤5 %:  CERRADO.
      //   - Paradas manuales (porcUltimoObjetivoAuto == -1) o autos a
      //     posiciones intermedias NO usan esa tolerancia; sólo se
      //     publican opened/closed si la posición real ya está al tope.
      {
        int pct = getPorcentajeEntero();
        if (porcUltimoObjetivoAuto == 100 && pct >= 95) return String(MSG_ABIERTO);
        if (porcUltimoObjetivoAuto == 0   && pct <= 5)  return String(MSG_CERRADO);
        if (pct >= 100) return String(MSG_ABIERTO);
        if (pct <= 0)   return String(MSG_CERRADO);
      }
      return String(MSG_PARADO);
    }*/
    String getEstadoString() {
      if (enEmergencia)                               return String(MSG_EMERGENCIA);
      if (errorAtasco)                                return String(MSG_ERROR_ATASCO);
      if (errorLimite)                                return String(MSG_ERROR_LIMITE);
      if (calibrando)                                 return String(MSG_CALIBRANDO);
      bool sinCalibrar = (!modoTiempo && pulsos100 == 0) || (modoTiempo && tiempoTotalRecorrido == 0);
      if (sinCalibrar)                                return String(MSG_NO_CALIBRADO);

      // Si el motor se mueve, o estamos esperando a que termine de frenar, seguimos reportando movimiento
      if ((motorEnMovimiento || confirmandoParada) && sentidoGiro == 1)  return String(MSG_ABRIENDO);
      if ((motorEnMovimiento || confirmandoParada) && sentidoGiro == -1) return String(MSG_CERRANDO);
      // -------------------------

      // --- Motor parado: detectar si estamos en un extremo ---
      {
        int pct = getPorcentajeEntero();
        if (porcUltimoObjetivoAuto == 100 && pct >= 95) return String(MSG_ABIERTO);
        if (porcUltimoObjetivoAuto == 0   && pct <= 5)  return String(MSG_CERRADO);
        if (pct >= 100) return String(MSG_ABIERTO);
        if (pct <= 0)   return String(MSG_CERRADO);
      }
      return String(MSG_PARADO);
    }

    int getPorcentajeEntero() {
      if (modoTiempo) {
        if (tiempoTotalRecorrido == 0) return 0;
        return (int)((getPosicionTiempoEstimada() * 100UL) / tiempoTotalRecorrido);
      } else {
        if (pulsos100 == 0) return 0;
        long pos = enc->read();
        return (int)constrain((pos * 100L) / pulsos100, 0L, 100L);
      }
    }

    void imprimirEstado() {
      String s = "Estado: " + getEstadoString() + " | Pos: " + String(getPorcentajeEntero()) + "% | Modo: ";
      if (modoTiempo) {
        s += "TIEMPO | Actual: " + String(getPosicionTiempoEstimada()) + " ms | Total: " + String(tiempoTotalRecorrido) + " ms";
      } else {
        s += "ENCODER | Actual: " + String(enc->read()) + " pulsos | Total: " + String(pulsos100) + " pulsos";
      }
      debug(s);
    }
};

#endif
