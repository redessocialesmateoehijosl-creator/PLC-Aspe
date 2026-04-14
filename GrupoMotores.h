#ifndef GRUPOMOTORES_H
#define GRUPOMOTORES_H

#include <vector>
#include <Arduino.h> 
#include <functional> // Necesario para el callback
#include "Motor.h"
#include "Constantes.h"

class GrupoMotores {
  private:
    std::vector<Motor*> motores; 
    
    // PINES
    int pinG_Abrir = -1, pinG_Cerrar = -1, pinG_Calib = -1;
    int pinG_ModoEnc = -1, pinG_ModoTime = -1;
    bool tieneBotonera = false;

    // Estado Botones
    bool lastBtnModoEnc = false;
    bool lastBtnModoTime = false;

    // --- SISTEMA DE LOGGING ---
    typedef std::function<void(String)> LogCallback;
    LogCallback externalLog = nullptr;

    void debug(String msg) {
       // Si hay logger configurado, úsalo (Web + Serial). Si no, solo Serial.
       if (externalLog) externalLog(msg);
       else Serial.println(msg);
    }

  public:
    GrupoMotores() {}

    // Configurar el logger desde el .ino
    void setLogger(LogCallback callback) { 
        externalLog = callback; 
        debug("[GRUPO] Logger configurado correctamente.");
    }

    void agregarMotor(Motor* m) { 
        motores.push_back(m); 
    }

    void setupBotonera(int pAbrir, int pCerrar, int pCalib, int pModoEnc, int pModoTime) {
        pinG_Abrir = pAbrir; pinG_Cerrar = pCerrar; pinG_Calib = pCalib;
        pinG_ModoEnc = pModoEnc; pinG_ModoTime = pModoTime;
        
        pinMode(pinG_Abrir, INPUT); pinMode(pinG_Cerrar, INPUT); pinMode(pinG_Calib, INPUT);
        pinMode(pinG_ModoEnc, INPUT); pinMode(pinG_ModoTime, INPUT);
        tieneBotonera = true;

        // -- LEER ESTADO INICIAL ---
        // Esto evita que al arrancar detecte un "falso cambio" si el interruptor ya está activado.
        lastBtnModoEnc = digitalRead(pinG_ModoEnc);
        lastBtnModoTime = digitalRead(pinG_ModoTime);

        debug("[GRUPO] Botonera maestra lista.");
    }

    // --- VERIFICADOR DE SEGURIDAD DEL GRUPO ---
    bool esSeguroMoverseAutomatico() {
        String estadoGlobal = getEstadoString(); // Obtenemos el consenso del grupo
        
        // LISTA NEGRA: Estados que PROHÍBEN el movimiento automático
        if (estadoGlobal == MSG_NO_CALIBRADO) {
            debug("[GRUPO ERROR] Orden rechazada: Sistema NO CALIBRADO.");
            return false;
        }
        if (estadoGlobal == MSG_CALIBRANDO) {
            debug("[GRUPO ERROR] Orden rechazada: Sistema en MODO CALIBRACIÓN.");
            return false;
        }
        if (estadoGlobal == MSG_EMERGENCIA) {
            debug("[GRUPO ERROR] Orden rechazada: EMERGENCIA ACTIVA.");
            return false;
        }
        if (estadoGlobal == MSG_ERROR_LIMITE || estadoGlobal == MSG_ERROR_ATASCO) {
            debug("[GRUPO ERROR] Orden rechazada: Hay ERRORES en el sistema.");
            return false;
        }
        
        // Si no es ninguno de los malos, es seguro
        return true;
    }
   // --- COMANDOS CON FILTRO DE SEGURIDAD ---

    void abrir() { 
        // 1. El Portero verifica
        if (!esSeguroMoverseAutomatico()) return;

        // 2. Si pasa, ejecutamos
        debug("[GRUPO CMD] >> ABRIR TODOS");
        for (auto m : motores) m->abrir(); 
    }

    void cerrar() { 
        if (!esSeguroMoverseAutomatico()) return;

        debug("[GRUPO CMD] >> CERRAR TODOS");
        for (auto m : motores) m->cerrar(); 
    }

    void abrirManual() {
        for (auto m : motores) m->abrirManual();
    }

    void cerrarManual() {
        for (auto m : motores) m->cerrarManual();
    }

    void moverA(int porcentaje) { 
        if (!esSeguroMoverseAutomatico()) return;

        debug("[GRUPO CMD] >> MOVER A " + String(porcentaje) + "%");
        for (auto m : motores) m->moverA(porcentaje); 
    }

    bool hayMovimientoPorRed() {
        for (auto m : motores) {
            if (m->esMovimientoRed()) return true;
        }
        return false;
    }

    void parar() { 
        debug("[GRUPO CMD] >> PARAR TODOS");
        for (auto m : motores) m->parar(); 
    }
    
    void setAnticipacion(int valor) { 
        debug("[GRUPO CFG] Anticipación: " + String(valor));
        for (auto m : motores) m->setAnticipacion(valor); 
    }
    void setModo(bool esTiempo) { 
        debug("[GRUPO CFG] Modo Global: " + String(esTiempo ? "TIEMPO" : "ENCODER"));
        for (auto m : motores) m->setModo(esTiempo); 
    }
    
    void calibrar()     { debug("[GRUPO CAL] Inicio"); for (auto m : motores) m->calibrar(); }
    void setZero()      { debug("[GRUPO CAL] Set 0"); for (auto m : motores) m->setZero(); }
    void set100()       { debug("[GRUPO CAL] Set 100"); for (auto m : motores) m->set100(); }
    void finCalibrado() { debug("[GRUPO CAL] Fin"); for (auto m : motores) m->finCalibrado(); }

    // --- DEBUG CRÍTICO PARA TU PROBLEMA MQTT ---
    bool checkMqttRequest() {
        bool algunMotorQuiere = false;
        for (auto m : motores) {
            if (m->checkMqttRequest()) {
                // Si ves este mensaje en la web, es que la cadena funciona
                debug("[GRUPO] Update MQTT");
                algunMotorQuiere = true;
            }
        }
        return algunMotorQuiere;
    }

    void gestionarBotonera() {
        if (!tieneBotonera) return;

        bool bA = digitalRead(pinG_Abrir);
        bool bC = digitalRead(pinG_Cerrar);
        bool bCal = digitalRead(pinG_Calib);
        bool bMEnc = digitalRead(pinG_ModoEnc);
        bool bMTime = digitalRead(pinG_ModoTime);

        for (auto m : motores) {
            m->setEntradasExternas(bA, bC, bCal);
        }

        if (bMEnc && !lastBtnModoEnc) {
            debug("[GRUPO BTN] Cambio a MODO ENCODER");
            setModo(false); 
        }
        if (bMTime && !lastBtnModoTime) {
            debug("[GRUPO BTN] Cambio a MODO TIEMPO");
            setModo(true); 
        }
        lastBtnModoEnc = bMEnc;
        lastBtnModoTime = bMTime;
    }

    void update() {
        gestionarBotonera(); 
        for (auto m : motores) m->update();
    }
    
    // --- ESTADO ---
    String getEstadoString() {
        bool alguienEmergencia = false;
        bool alguienErrorLim = false;
        bool alguienAtasco = false;
        bool alguienNoCal = false;
        bool alguienCalibrando = false;
        bool alguienMoviendo = false;
        
        int contAbiertos = 0;
        int contCerrados = 0;

        for (auto m : motores) {
            String s = m->getEstadoString();
            
            if (s == MSG_EMERGENCIA) alguienEmergencia = true;
            if (s == MSG_ERROR_LIMITE) alguienErrorLim = true;
            if (s == MSG_ERROR_ATASCO) alguienAtasco = true;
            if (s == MSG_NO_CALIBRADO) alguienNoCal = true;
            if (s == MSG_CALIBRANDO) alguienCalibrando = true;
            if (s == MSG_ABRIENDO || s == MSG_CERRANDO) alguienMoviendo = true;
            if (s == MSG_ABIERTO) contAbiertos++;
            if (s == MSG_CERRADO) contCerrados++;
        }

        if (alguienEmergencia) return MSG_EMERGENCIA;
        if (alguienAtasco)     return MSG_ERROR_ATASCO; 
        if (alguienErrorLim)   return MSG_ERROR_LIMITE;
        if (alguienNoCal)      return MSG_NO_CALIBRADO;
        if (alguienCalibrando) return MSG_CALIBRANDO;
        if (alguienMoviendo)   return MSG_ABRIENDO; 
        if (contAbiertos == motores.size()) return MSG_ABIERTO;
        if (contCerrados == motores.size()) return MSG_CERRADO;

        return MSG_PARADO; 
    }

    int getPorcentajeEntero() {
        if (motores.empty()) return 0;
        long suma = 0;
        for (auto m : motores) { suma += m->getPorcentajeEntero(); }
        return (int)(suma / motores.size());
    }
    
    // Esta función la llamas con el comando 'i'
    void imprimirEstado() {
       debug("------- ESTADO GRUPO -------");
       debug("Consenso: " + getEstadoString());
       debug("Media Pos: " + String(getPorcentajeEntero()) + "%");
       
       for(size_t i=0; i<motores.size(); i++) {
           // Forzamos al motor individual a imprimir su estado usando su propio logger
           motores[i]->imprimirEstado(); 
       }
       debug("----------------------------");
    }
};

#endif
