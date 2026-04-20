#ifndef GRUPOMOTORES_H
#define GRUPOMOTORES_H

// ============================================================
//  GrupoMotores.h — Adaptado para M-Duino 21+ (Arduino Mega)
//
//  Cambios respecto a la versión ESP32:
//   • std::vector<Motor*> → array fijo (máx. 4 motores)
//   • std::function        → puntero de función simple
//   • #include <functional> → eliminado
// ============================================================

#include <Arduino.h>
#include "Motor.h"
#include "Constantes.h"

#define GRUPO_MAX_MOTORES 4

class GrupoMotores {
  private:
    Motor* motores[GRUPO_MAX_MOTORES];
    int    numMotores = 0;

    // Pines botonera maestra (opcionales)
    int pinG_Abrir   = -1;
    int pinG_Cerrar  = -1;
    int pinG_Calib   = -1;
    int pinG_ModoEnc = -1;
    int pinG_ModoTime= -1;
    bool tieneBotonera = false;

    bool lastBtnModoEnc  = false;
    bool lastBtnModoTime = false;

    // Logger
    typedef void (*LogCallback)(String);
    LogCallback externalLog = nullptr;

    void debug(String msg) {
      if (externalLog) externalLog(msg);
      else Serial.println(msg);
    }

  public:
    GrupoMotores() {
      for (int i = 0; i < GRUPO_MAX_MOTORES; i++) motores[i] = nullptr;
    }

    void setLogger(LogCallback callback) {
      externalLog = callback;
      debug(F("[GRUPO] Logger OK."));
    }

    void agregarMotor(Motor* m) {
      if (numMotores < GRUPO_MAX_MOTORES) {
        motores[numMotores++] = m;
      }
    }

    void setupBotonera(int pAbrir, int pCerrar, int pCalib, int pModoEnc, int pModoTime) {
      pinG_Abrir   = pAbrir;  pinG_Cerrar  = pCerrar;  pinG_Calib  = pCalib;
      pinG_ModoEnc = pModoEnc; pinG_ModoTime = pModoTime;

      pinMode(pinG_Abrir,   INPUT); pinMode(pinG_Cerrar,  INPUT); pinMode(pinG_Calib,    INPUT);
      pinMode(pinG_ModoEnc, INPUT); pinMode(pinG_ModoTime, INPUT);
      tieneBotonera = true;

      lastBtnModoEnc  = digitalRead(pinG_ModoEnc);
      lastBtnModoTime = digitalRead(pinG_ModoTime);
      debug(F("[GRUPO] Botonera maestra lista."));
    }

    // --------------------------------------------------------
    //  Seguridad: verifica que sea seguro iniciar movimiento
    // --------------------------------------------------------
    bool esSeguroMoverseAutomatico() {
      String estado = getEstadoString();
      if (estado == MSG_NO_CALIBRADO) { debug(F("[GRUPO] Rechazado: NO CALIBRADO."));   return false; }
      if (estado == MSG_CALIBRANDO)   { debug(F("[GRUPO] Rechazado: CALIBRANDO."));     return false; }
      if (estado == MSG_EMERGENCIA)   { debug(F("[GRUPO] Rechazado: EMERGENCIA."));     return false; }
      if (estado == MSG_ERROR_LIMITE || estado == MSG_ERROR_ATASCO || estado == MSG_ERROR_VFD) {
        debug(F("[GRUPO] Rechazado: ERRORES activos.")); return false;
      }
      return true;
    }

    // --------------------------------------------------------
    //  Comandos de movimiento
    // --------------------------------------------------------
    void abrir() {
      if (!esSeguroMoverseAutomatico()) return;
      debug(F("[GRUPO CMD] >> ABRIR"));
      for (int i = 0; i < numMotores; i++) motores[i]->abrir();
    }

    void cerrar() {
      if (!esSeguroMoverseAutomatico()) return;
      debug(F("[GRUPO CMD] >> CERRAR"));
      for (int i = 0; i < numMotores; i++) motores[i]->cerrar();
    }

    void abrirManual()  {
      debug(F("[GRUPO CMD] >> ABRIR MANUAL"));
      for (int i = 0; i < numMotores; i++) motores[i]->abrirManualCmd();
    }
    void cerrarManual() {
      debug(F("[GRUPO CMD] >> CERRAR MANUAL"));
      for (int i = 0; i < numMotores; i++) motores[i]->cerrarManualCmd();
    }

    void moverA(int porcentaje) {
      if (!esSeguroMoverseAutomatico()) return;
      debug("[GRUPO CMD] >> MOVER A " + String(porcentaje) + "%");
      for (int i = 0; i < numMotores; i++) motores[i]->moverA(porcentaje, true);  // siempre remoto
    }

    void parar() {
      debug(F("[GRUPO CMD] >> PARAR"));
      for (int i = 0; i < numMotores; i++) motores[i]->parar();
    }

    bool hayMovimientoPorRed() {
      for (int i = 0; i < numMotores; i++) {
        if (motores[i]->esMovimientoRed()) return true;
      }
      return false;
    }

    // true si el grupo está en movimiento activo (abriendo, cerrando o calibrando)
    // Usado por Red::inhibirDHCP() para proteger el loop de bloqueos de red.
    bool estaMoviendo() {
      String est = getEstadoString();
      return (est == MSG_ABRIENDO || est == MSG_CERRANDO || est == MSG_CALIBRANDO);
    }

    void setAnticipacion(int valor) {
      debug("[GRUPO CFG] Anticipacion: " + String(valor));
      for (int i = 0; i < numMotores; i++) motores[i]->setAnticipacion(valor);
    }

    void setModo(bool esTiempo) {
      debug(String(F("[GRUPO CFG] Modo: ")) + (esTiempo ? "TIEMPO" : "ENCODER"));
      for (int i = 0; i < numMotores; i++) motores[i]->setModo(esTiempo);
    }

    void calibrar()        { for (int i = 0; i < numMotores; i++) motores[i]->calibrar();        }
    void setZero()         { for (int i = 0; i < numMotores; i++) motores[i]->setZero();         }
    void set100()          { for (int i = 0; i < numMotores; i++) motores[i]->set100();          }
    void finCalibrado()    { for (int i = 0; i < numMotores; i++) motores[i]->finCalibrado();    }
    void resetErroresVFD() {
      debug(F("[GRUPO CMD] >> RESET ERRORES VFD"));
      for (int i = 0; i < numMotores; i++) motores[i]->resetErroresVFD();
    }

    // --------------------------------------------------------
    //  MQTT: chequea si algún motor requiere actualización
    // --------------------------------------------------------
    bool checkMqttRequest() {
      bool alguno = false;
      for (int i = 0; i < numMotores; i++) {
        if (motores[i]->checkMqttRequest()) alguno = true;
      }
      return alguno;
    }

    // --------------------------------------------------------
    //  Botonera maestra (inputs del grupo)
    // --------------------------------------------------------
    void gestionarBotonera() {
      if (!tieneBotonera) return;

      bool bA    = digitalRead(pinG_Abrir);
      bool bC    = digitalRead(pinG_Cerrar);
      bool bCal  = digitalRead(pinG_Calib);
      bool bMEnc = digitalRead(pinG_ModoEnc);
      bool bMTime= digitalRead(pinG_ModoTime);

      for (int i = 0; i < numMotores; i++) {
        motores[i]->setEntradasExternas(bA, bC, bCal);
      }

      if (bMEnc  && !lastBtnModoEnc)  { debug(F("[GRUPO BTN] Modo ENCODER"));  setModo(false); }
      if (bMTime && !lastBtnModoTime) { debug(F("[GRUPO BTN] Modo TIEMPO"));   setModo(true);  }
      lastBtnModoEnc  = bMEnc;
      lastBtnModoTime = bMTime;
    }

    // --------------------------------------------------------
    //  UPDATE  (llamar cada ciclo de loop)
    // --------------------------------------------------------
    void update() {
      gestionarBotonera();
      for (int i = 0; i < numMotores; i++) motores[i]->update();
    }

    // --------------------------------------------------------
    //  Estado del grupo (consenso de todos los motores)
    // --------------------------------------------------------
    String getEstadoString() {
      bool alguienEmergencia = false;
      bool alguienAtasco     = false;
      bool alguienErrorVFD   = false;
      bool alguienErrorLim   = false;
      bool alguienNoCal      = false;
      bool alguienCalibrando = false;
      bool alguienMoviendo   = false;
      int  contAbiertos      = 0;
      int  contCerrados      = 0;

      for (int i = 0; i < numMotores; i++) {
        String s = motores[i]->getEstadoString();
        if (s == MSG_EMERGENCIA)   alguienEmergencia = true;
        if (s == MSG_ERROR_ATASCO) alguienAtasco     = true;
        if (s == MSG_ERROR_VFD)    alguienErrorVFD   = true;
        if (s == MSG_ERROR_LIMITE) alguienErrorLim   = true;
        if (s == MSG_NO_CALIBRADO) alguienNoCal      = true;
        if (s == MSG_CALIBRANDO)   alguienCalibrando = true;
        if (s == MSG_ABRIENDO || s == MSG_CERRANDO) alguienMoviendo = true;
        if (s == MSG_ABIERTO) contAbiertos++;
        if (s == MSG_CERRADO) contCerrados++;
      }

      if (alguienEmergencia) return String(MSG_EMERGENCIA);
      if (alguienAtasco)     return String(MSG_ERROR_ATASCO);
      if (alguienErrorVFD)   return String(MSG_ERROR_VFD);
      if (alguienErrorLim)   return String(MSG_ERROR_LIMITE);
      if (alguienNoCal)      return String(MSG_NO_CALIBRADO);
      if (alguienCalibrando) return String(MSG_CALIBRANDO);
      if (alguienMoviendo)   return String(MSG_ABRIENDO);
      if (numMotores > 0 && contAbiertos == numMotores) return String(MSG_ABIERTO);
      if (numMotores > 0 && contCerrados == numMotores) return String(MSG_CERRADO);
      return String(MSG_PARADO);
    }

    int getPorcentajeEntero() {
      if (numMotores == 0) return 0;
      long suma = 0;
      for (int i = 0; i < numMotores; i++) suma += motores[i]->getPorcentajeEntero();
      return (int)(suma / numMotores);
    }

    /*void imprimirEstado() {
      debug(F("------- ESTADO GRUPO -------"));
      debug("Consenso: " + getEstadoString());
      debug("Media Pos: " + String(getPorcentajeEntero()) + "%");
      for (int i = 0; i < numMotores; i++) motores[i]->imprimirEstado();
      debug(F("----------------------------"));
    }*/

    void imprimirEstado() {
      // Usar Serial.print directo para evitar concatenaciones de String
      // que fallan silenciosamente cuando el heap AVR está fragmentado.
      Serial.println(F("--- ESTADO ---"));
      Serial.print(F("Consenso: "));
      Serial.print(getEstadoString());
      Serial.print(F(" | Pos media: "));
      Serial.print(getPorcentajeEntero());
      Serial.println(F("%"));
      for (int i = 0; i < numMotores; i++) motores[i]->imprimirEstado();
      Serial.println(F("--------------"));
    }
};

#endif
