#ifndef MOTOR_H
#define MOTOR_H

#include <ESP32Encoder.h>
#include <Preferences.h>
#include <functional>
#include "Constantes.h"

// ==========================================
//        MENSAJES DE ESTADO (Salida)
// ==========================================
// Estos son los textos que el PLC enviará por MQTT
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

// Prefijo para la posición (ej: "pos 50")
const char* PREFIJO_POS       = "pos "; 


struct DatosMotor {
  long valor;       
  float porcentaje; 
};

class Motor {
  private:
    String id; 
    
    // --- PINES ---
    int pinAbrir, pinCerrar;
    int pinRojo, pinVerde, pinNaranja;
    int pinBtnAbrir, pinBtnCerrar, pinBtnCalib, pinSeta;

    // ---  VARIABLES DE ENTRADA EXTERNA ---
    bool extBtnAbrir = false;
    bool extBtnCerrar = false;
    bool extBtnCalib = false;

    ESP32Encoder enc;
    Preferences prefs; 

    // --- ESTADO INTERNO ---
    bool modoTiempo = false;           
    bool motorEnMovimiento = false;
    int sentidoGiro = 0;     
    bool requestMqttUpdate = false;     

    // Variables Encoder/Tiempo
    long pulsos100 = 0;           
    long pulsosObjetivo = 0;      
    bool moviendoAutomatico = false; 
    unsigned long tiempoTotalRecorrido = 0; 
    unsigned long posicionActualTiempo = 0; 
    unsigned long tiempoInicioMovimiento = 0; 

    // Variable de Anticipación (Freno)
    long pulsosAnticipacion = 50; 

    // Variables Calibración
    bool calibrando = false;
    bool esperandoInicio = false; 
    bool midiendoTiempo = false; 
    bool tiempoInvertido = false; 
    bool buscandoBajar = false;
    
    // Estados Botones
    bool lastBtnAbrir = false;
    bool lastBtnCerrar = false;
    bool lastBtnCalib = false;
    int pasoCalibracion = 0; 

    // Luces y Errores
    unsigned long lastBlinkTime = 0;
    bool blinkState = false;           
    bool enEmergencia = false;
    bool errorLimite = false; // Seguridad Software
    unsigned long finCalibracionTime = 0; 
    bool mostrandoExito = false;     

    bool movimientoPorRed = false;  

    // Inercia Stop
    bool esperandoParadaReal = false;   
    long lastEncValStop = 0;            
    unsigned long lastEncTimeStop = 0;  
    const int TIEMPO_ESTABILIZACION = 300; 

    // Error Encoder
    bool errorAtasco = false;         // bandera de error
    long lastPosCheck = 0;            // Última posición conocida para chequeo
    unsigned long lastMoveTime = 0;   // Última vez que vimos que se movía
    
    // Configuración de sensibilidad
    const int TIEMPO_GRACIA_ARRANQUE = 1000; // Damos 1s al arrancar para vencer inercia
    const int TIEMPO_MAX_SIN_PULSOS = 1500;  // Si en 1.5s no cambia el número -> ERROR

    // Logger
    typedef std::function<void(String)> LogCallback;
    LogCallback externalLog = nullptr;

    void debug(String msg) {
      Serial.println("[" + id + "] " + msg); 
      if (externalLog) externalLog("[" + id + "] " + msg);
    }

    // --- VERIFICACIÓN DE LÍMITES (Modo Encoder) ---
    /*void verificarLimitesSeguridad() {
       if (calibrando || modoTiempo) return;
       if (!motorEnMovimiento) return;

       long pos = enc.getCount();

       // OPCIÓN A: Sistema de Pulsos POSITIVO (0 ... 10000)
       if (pulsos100 < 0) {
           // Se pasó del 100% por arriba (ej: 10050)
           if (pos > pulsos100 && sentidoGiro == 1) {
               debug("!!! ERROR: Limite Superior (+) !!!");
               parar(); errorLimite = true; 
           }
           // Se bajó del 0% (ej: -10)
           if (pos < 0 && sentidoGiro == -1) {
               debug("!!! ERROR: Limite Inferior (+) !!!");
               parar(); errorLimite = true; 
           }
       }
       // OPCIÓN B: Sistema de Pulsos NEGATIVO (0 ... -19000) -> TU CASO
       else {
           // Se pasó del 100% por "abajo" (ej: -20000 es menor que -19000)
           // Nota: Al ser negativo, "mayor recorrido" es un número "menor"
           if (pos < pulsos100 && sentidoGiro == 1) {
               debug("!!! ERROR: Limite Superior (-) !!!");
               parar(); errorLimite = true; 
           }
           // Se pasó del 0% hacia positivos (ej: 10 es mayor que 0)
           if (pos > 0 && sentidoGiro == -1) {
               debug("!!! ERROR: Limite Inferior (-) !!!");
               parar(); errorLimite = true; 
           }
       }
    }*/
    // --- VERIFICACIÓN DE LÍMITES (Modo Encoder) ---
    void verificarLimitesSeguridad() {
       if (calibrando || modoTiempo) return;
       if (!motorEnMovimiento) return;

       long pos = enc.getCount();

       // OPCIÓN A: Sistema de Pulsos POSITIVO (0 ... 10000)
       if (pulsos100 > 0) {
           // Se pasó del 100% por arriba y sigue subiendo
           if (pos > pulsos100 && sentidoGiro == 1) {
               debug("!!! ERROR: Limite Superior (+) !!!");
               parar(); errorLimite = true; 
           }
           // Se bajó del 0% y sigue bajando
           if (pos < 0 && sentidoGiro == -1) {
               debug("!!! ERROR: Limite Inferior (+) !!!");
               parar(); errorLimite = true; 
           }
       }
       // OPCIÓN B: Sistema de Pulsos NEGATIVO (0 ... -19000) -> TU CASO
       else {
           // CASO 1: Límite del 100% (El fondo del pozo, ej: -19000)
           // Si pos es -19500 (te has pasado) y sigues bajando (-1) -> ERROR
           // (Si subes hacia -10000, te dejo pasar)
           if (pos < pulsos100 && sentidoGiro == -1) {
               debug("!!! ERROR: Limite Superior (-) !!!");
               parar(); errorLimite = true; 
           }
           
           // CASO 2: Límite del 0% (El techo, 0)
           // Si pos es +50 (te has pasado por inercia) y sigues subiendo (1) -> ERROR
           // (Si bajas hacia -5000, te dejo pasar)
           if (pos > 0 && sentidoGiro == 1) { 
               debug("!!! ERROR: Limite Inferior (-) !!!");
               parar(); errorLimite = true; 
           }
       }
    }

    // --- WATCHDOG DE ENCODER (Detección de Atasco / Rotura) ---
    void verificarAtasco() {
       // Solo vigilamos si:
       // 1. El motor debería estar moviéndose (tiene corriente).
       // 2. NO es modo tiempo (ahí no hay encoder).
       // 3. NO estamos calibrando (ahí puede haber movimientos raros manuales).
       if (!motorEnMovimiento || modoTiempo || calibrando) return;

       // 1. GRACIA DE ARRANQUE:
       if (millis() - tiempoInicioMovimiento < TIEMPO_GRACIA_ARRANQUE) {
           lastMoveTime = millis(); 
           lastPosCheck = enc.getCount();
           return;
       }

       long posActual = enc.getCount();

       // 2. COMPROBACIÓN:
       if (abs(posActual - lastPosCheck) > 2) {
           // SÍ se mueve: Todo bien.
           lastMoveTime = millis();
           lastPosCheck = posActual;
       }
       else {
           // NO se mueve. ¿Cuánto tiempo lleva así?
           if (millis() - lastMoveTime > TIEMPO_MAX_SIN_PULSOS) {
               debug("!!! ERROR FATAL: ENCODER NO RESPONDE (Posible rotura o atasco) !!!");
               parar();
               errorAtasco = true; // Activamos alarma
               
               // --- DESCALIBRACIÓN FORZOSA DE SEGURIDAD ---
               // Como no sabemos cuánto se ha movido realmente el motor sin contar pulsos,
               // la posición actual es BASURA. Obligamos a recalibrar.
               
               pulsos100 = 0;              // Borramos el límite máximo en RAM
               prefs.putLong("pulsos100", 0); // Borramos el límite en Memoria Flash
               
               // Opcional: También podrías borrar la posición actual si quieres ser drástico
               // enc.clearCount(); 
               
               debug(">> SEGURIDAD: Sistema marcado como NO CALIBRADO. Se requiere Homing.");
           }
       }
    }

    void gestionarLuces() {
      unsigned long now = millis();
      if (now - lastBlinkTime > 300) { blinkState = !blinkState; lastBlinkTime = now; }

      if (enEmergencia || errorLimite || errorAtasco) { // Rojo parpadeando en fallo
        digitalWrite(pinRojo, blinkState); digitalWrite(pinVerde, LOW); digitalWrite(pinNaranja, LOW); return;
      }
      if (mostrandoExito) {
        if (now - finCalibracionTime < 3000) {
           digitalWrite(pinRojo, blinkState); digitalWrite(pinVerde, blinkState); digitalWrite(pinNaranja, blinkState);
        } else { mostrandoExito = false; }
        return;
      }
      if (calibrando) {
        if (pasoCalibracion == 2) digitalWrite(pinNaranja, HIGH);
        else digitalWrite(pinNaranja, blinkState);
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

    void gestionarEntradas() {
      if (digitalRead(pinSeta) == LOW) {
         if (!enEmergencia) { debug("!!! EMERGENCIA !!!"); parar(); enEmergencia = true; }
         return; 
      } else { 
        if (enEmergencia) { 
          debug("Emergencia OFF."); 
          enEmergencia = false;
          // REARME DEL ERROR DE LÍMITE
             // Si había un error de software, al soltar la seta lo limpiamos.
          if (errorLimite || errorAtasco) { 
                 errorLimite = false;
                 errorAtasco = false;         
                 debug(">> REARME: Errores limpiados.");
             }
        } 
      }

      // COMBINAMOS EL BOTÓN FÍSICO LOCAL CON EL GENERAL
      // El motor reaccionará si pulsas el suyo O el general
      bool btnA = digitalRead(pinBtnAbrir) || extBtnAbrir;
      bool btnC = digitalRead(pinBtnCerrar) || extBtnCerrar;
      bool btnCal = digitalRead(pinBtnCalib) || extBtnCalib;

      if (btnCal && !lastBtnCalib) {
         if (pasoCalibracion == 0) { debug(">> Calibrar..."); calibrar(); } 
         else if (pasoCalibracion == 1) { debug(">> Set 0..."); setZero(); } 
         else if (pasoCalibracion == 2) { debug(">> Set 100..."); set100(); finCalibrado(); }
      }
      lastBtnCalib = btnCal;

      if (calibrando || modoTiempo || errorAtasco) {
         if (btnA) abrirManual();
         else if (btnC) cerrarManual();
         else {
            if (motorEnMovimiento && !moviendoAutomatico) parar();
         }
      }
      else {
         if (btnA && !lastBtnAbrir) { debug("Btn: Auto Abrir"); moverA(0); }
         if (!btnA && lastBtnAbrir) { debug("Btn: Stop"); parar(); }
         if (btnC && !lastBtnCerrar) { debug("Btn: Auto Cerrar"); moverA(100); }
         if (!btnC && lastBtnCerrar) { debug("Btn: Stop"); parar(); }
      }
      lastBtnAbrir = btnA;
      lastBtnCerrar = btnC;
    }

   bool debeSumarTiempo() {
       if (calibrando && midiendoTiempo) return true;
       return (sentidoGiro == 1); 
   }

    // Cálculo en vivo (para que la barra web se mueva suave)
    unsigned long getPosicionTiempoEstimada() {
        if (!modoTiempo) return 0;
        if (!motorEnMovimiento) return posicionActualTiempo; 
        unsigned long delta = millis() - tiempoInicioMovimiento;
        long estimada;
        if (debeSumarTiempo()) estimada = posicionActualTiempo + delta;
        else estimada = (long)posicionActualTiempo - (long)delta;
        
        if (estimada < 0) return 0;
        if (estimada > tiempoTotalRecorrido) return tiempoTotalRecorrido;
        return (unsigned long)estimada;
    }

    void cargarDatosDeMemoria() {
       pulsosAnticipacion = prefs.getLong("antiEnc", 50);

       if (modoTiempo) {
          tiempoTotalRecorrido = prefs.getULong("timeTotal", 0);
          posicionActualTiempo = prefs.getULong("posTime", 0);
          debug("Inicio: Restaurado TIEMPO (Pos: " + String(posicionActualTiempo) + " / Total: " + String(tiempoTotalRecorrido) + ")");
       } else {
          pulsos100 = prefs.getLong("pulsos100", 0);
          long savedPulsos = prefs.getLong("posEnc", 0);
          enc.setCount(savedPulsos);
          debug("Inicio: Restaurado ENCODER (Pos: " + String(savedPulsos) + " / Total: " + String(pulsos100) + ")");
          debug("Anticipación Freno: " + String(pulsosAnticipacion) + " pulsos");
       }
    }

  public:
    Motor(String nombreUnico, int pA, int pC, int encA, int encB, int pR, int pV, int pN, int bA, int bC, int bCal, int bSeta) {
      id = nombreUnico;
      pinAbrir = pA; pinCerrar = pC;
      pinRojo = pR; pinVerde = pV; pinNaranja = pN;
      pinBtnAbrir = bA; pinBtnCerrar = bC; pinBtnCalib = bCal; pinSeta = bSeta;

      pinMode(pinAbrir, OUTPUT); pinMode(pinCerrar, OUTPUT);
      pinMode(pinRojo, OUTPUT); pinMode(pinVerde, OUTPUT); pinMode(pinNaranja, OUTPUT);
      digitalWrite(pinAbrir, LOW); digitalWrite(pinCerrar, LOW);

      pinMode(pinBtnAbrir, INPUT); pinMode(pinBtnCerrar, INPUT);
      pinMode(pinBtnCalib, INPUT); pinMode(pinSeta, INPUT); 

      enc.attachHalfQuad(encA, encB); 
    }

    void setLogger(LogCallback callback) { externalLog = callback; }

    void begin() {
      String nsp = id.substring(0, 15);
      prefs.begin(nsp.c_str(), false);
      modoTiempo = prefs.getBool("isTimeMode", false); 
      cargarDatosDeMemoria();
    }

    void setAnticipacion(long pulsos) {
       pulsosAnticipacion = pulsos;
       prefs.putLong("antiEnc", pulsosAnticipacion);
       debug("Anticipación actualizada a: " + String(pulsosAnticipacion) + " pulsos");
    }

    /*void setModo(bool esTiempo) {
       // --- PROTECCIÓN: Si ya estamos en ese modo, NO hacemos nada ---
       if (modoTiempo == esTiempo) {
           return; // Salimos sin borrar la calibración
       }

       parar(); 
       
       modoTiempo = esTiempo; 
       prefs.putBool("isTimeMode", modoTiempo); 
       
       // RESETEO DE CALIBRACIÓN (Solo ocurre si el modo HA CAMBIADO)
       pulsos100 = 0;
       tiempoTotalRecorrido = 0;
       posicionActualTiempo = 0;
       enc.clearCount(); 
       
       debug("CAMBIO DE MODO: Variables reseteadas. Sistema NO CALIBRADO.");
    }*/

    void setModo(bool esTiempo) {
       // Protección: Si ya estamos en ese modo, no hacemos nada para no borrar datos a lo tonto
       if (modoTiempo == esTiempo) return;

       parar(); 
       
       modoTiempo = esTiempo; 
       prefs.putBool("isTimeMode", modoTiempo); 
       
       // --- AQUÍ ESTÁ LA CORRECCIÓN ---
       // Antes solo borrábamos la RAM. Ahora borramos TAMBIÉN la Flash.
       
       pulsos100 = 0;
       prefs.putLong("pulsos100", 0); // <--- Borrar límite Encoder en Flash
       
       tiempoTotalRecorrido = 0;
       prefs.putULong("timeTotal", 0); // <--- Borrar límite Tiempo en Flash
       
       posicionActualTiempo = 0;
       prefs.putULong("posTime", 0);   // <--- Borrar posición Tiempo en Flash
       
       enc.clearCount(); 
       prefs.putLong("posEnc", 0);     // <--- Borrar posición Encoder en Flash
       
       debug("CAMBIO DE MODO: Variables y Memoria Flash reseteadas. Sistema NO CALIBRADO.");
    }

    // --- FUNCIÓN PARA INYECTAR LOS BOTONES GENERALES ---
    void setEntradasExternas(bool ab, bool ce, bool cal) {
        extBtnAbrir = ab;
        extBtnCerrar = ce;
        extBtnCalib = cal;
    }

    void update() {
      gestionarLuces();
      gestionarEntradas();
      
      // Chequeo constante de límites
      //verificarLimitesSeguridad(); 
      verificarAtasco();

      if (esperandoParadaReal && !modoTiempo) {
          long lecturaActual = enc.getCount();
          if (lecturaActual != lastEncValStop) {
              lastEncValStop = lecturaActual;
              lastEncTimeStop = millis(); 
          } 
          else {
              if (millis() - lastEncTimeStop > TIEMPO_ESTABILIZACION) {
                  prefs.putLong("posEnc", lecturaActual);
                  esperandoParadaReal = false; 
                  debug(">> Motor FRENADO. Pos guardada: " + String(lecturaActual));
              }
          }
      }

      // Si hay cualquier error o parada, no ejecutamos lógica de movimiento automático
      if (!motorEnMovimiento || calibrando || enEmergencia || errorLimite || errorAtasco ) return;

      if (modoTiempo && moviendoAutomatico) {
        unsigned long delta = millis() - tiempoInicioMovimiento;
        long posEstimada;
        if (debeSumarTiempo()) posEstimada = posicionActualTiempo + delta;
        else posEstimada = posicionActualTiempo - delta;
        
        if (debeSumarTiempo() && posEstimada >= tiempoTotalRecorrido) { debug("Fin carrera (Abierto)."); parar(); } 
        else if (!debeSumarTiempo() && posEstimada <= 0) { debug("Fin carrera (Cerrado)."); parar(); }
      }
      else if (!modoTiempo && moviendoAutomatico) {
        long pulsosActuales = enc.getCount();
        // Lógica de frenado anticipado
        if (buscandoBajar) { 
            if (pulsosActuales <= (pulsosObjetivo + pulsosAnticipacion)) { 
                debug("Destino alcanzado (Anticipado)."); parar(); 
            } 
        } 
        else { 
            if (pulsosActuales >= (pulsosObjetivo - pulsosAnticipacion)) { 
                debug("Destino alcanzado (Anticipado)."); parar(); 
            } 
        }
      }
    }

    // --- ACCIONES AUTOMÁTICAS (BLOQUEAN SI HAY ERROR) ---
   void abrir() {
      movimientoPorRed = true;
      esperandoParadaReal = false;
      if (enEmergencia || errorLimite) return; 

      if (calibrando) {
         if (modoTiempo && esperandoInicio) {
            tiempoInicioMovimiento = millis(); esperandoInicio = false; midiendoTiempo = true; tiempoInvertido = false;
            debug(">> CAL (CMD): Midiendo (Abriendo)...");
         }
         moviendoAutomatico = true; 
      }
      else if (!modoTiempo) { moverA(100); return; } 
      else {
         // --- MODO TIEMPO ---
         // Lógica Pura: Abrir es ir hacia el Tiempo Total.
         // Da igual si los cables están invertidos, matemáticamente vamos al tope.
         
         if (posicionActualTiempo >= tiempoTotalRecorrido) {
             debug(">> Ignorado: Ya está ABIERTO (Tiempo).");
             requestMqttUpdate = true;
             return;
         }
         
         tiempoInicioMovimiento = millis(); moviendoAutomatico = true;
      }

      // INVERSIÓN FÍSICA (Aquí es donde cruzamos los cables si hace falta)
      int pinFisicoActivar = (!modoTiempo || !tiempoInvertido) ? pinAbrir : pinCerrar;
      int pinFisicoApagar  = (!modoTiempo || !tiempoInvertido) ? pinCerrar : pinAbrir;

      digitalWrite(pinFisicoActivar, HIGH); 
      digitalWrite(pinFisicoApagar, LOW);
      
      motorEnMovimiento = true; 
      sentidoGiro = 1; // Matemáticamente SIEMPRE sumamos al abrir
   }

   void cerrar() {
      movimientoPorRed = true;
      esperandoParadaReal = false;
      if (enEmergencia || errorLimite) return; 

      if (calibrando) {
         if (modoTiempo && esperandoInicio) {
            tiempoInicioMovimiento = millis(); esperandoInicio = false; midiendoTiempo = true; tiempoInvertido = true;
            debug(">> CAL (CMD): Midiendo (Cerrando)...");
         }
         moviendoAutomatico = true;
      }
      else if (!modoTiempo) { moverA(0); return; } 
      else {
         // --- MODO TIEMPO ---
         // Lógica Pura: Cerrar es ir hacia 0.
         
         if (posicionActualTiempo == 0) {
             debug(">> Ignorado: Ya está CERRADO (Tiempo).");
             requestMqttUpdate = true;
             return;
         }

         tiempoInicioMovimiento = millis(); moviendoAutomatico = true;
      }

      // INVERSIÓN FÍSICA
      int pinFisicoActivar = (!modoTiempo || !tiempoInvertido) ? pinCerrar : pinAbrir;
      int pinFisicoApagar  = (!modoTiempo || !tiempoInvertido) ? pinAbrir : pinCerrar;

      digitalWrite(pinFisicoActivar, HIGH); 
      digitalWrite(pinFisicoApagar, LOW);
      
      motorEnMovimiento = true; 
      sentidoGiro = -1; // Matemáticamente SIEMPRE restamos al cerrar
   }

    /*void moverA(int porcentaje) {
       esperandoParadaReal = false;
       if(modoTiempo || calibrando || enEmergencia || errorLimite || errorAtasco) { 
           debug("Accion rechazada (Error o Estado)."); return;
       }
       //long porcentajeLim = constrain(porcentaje, 0, 100);
       long porcentajeLim = porcentaje;
       pulsosObjetivo = (pulsos100 * porcentajeLim) / 100;
       long pulsosActuales = enc.getCount();
       debug("Auto a: " + String(porcentajeLim) + "%");
       
       if (pulsosActuales < pulsosObjetivo) { 
           buscandoBajar = false; 
           digitalWrite(pinAbrir, LOW); digitalWrite(pinCerrar, HIGH); 
           motorEnMovimiento = true; sentidoGiro = -1; moviendoAutomatico = true; 
       } 
       else if (pulsosActuales > pulsosObjetivo) { 
           buscandoBajar = true; 
           digitalWrite(pinAbrir, HIGH); digitalWrite(pinCerrar, LOW); 
           motorEnMovimiento = true; sentidoGiro = 1; moviendoAutomatico = true; 
       }
    }*/
    // Añade este método para que el Grupo pueda leer y bajar la bandera
    bool checkMqttRequest() {
        if (requestMqttUpdate) {
            requestMqttUpdate = false; // La bajamos al leerla
            return true;
        }
        return false;
    }

   void moverA(int porcentaje) {
       esperandoParadaReal = false;
       
       // 1. CHEQUEO DE BLOQUEOS
       if(modoTiempo || calibrando || enEmergencia || errorLimite || errorAtasco) { 
           debug("Accion rechazada (Error o Estado)."); return;
       }
       
       long porcentajeLim = constrain(porcentaje, 0, 100); 
       //long porcentajeLim = porcentaje; // Permitimos >100% para pruebas si quieres
       pulsosObjetivo = (pulsos100 * porcentajeLim) / 100;
       long pulsosActuales = enc.getCount();

       // Si estamos a menos distancia que la anticipación, consideramos que ya hemos llegado.
       // Esto evita arranques de 0.1 segundos.
       long distancia = abs(pulsosObjetivo - pulsosActuales);
       if (distancia <= pulsosAnticipacion) {
           debug(">> Mando ignorado: Ya estamos en la zona de destino (Diferencia < Anticipacion).");

           requestMqttUpdate = true;
           return; 
       }

       debug("Auto a: " + String(porcentajeLim) + "%");
       
       // 3. REINICIAR WATCHDOG (Importante para evitar Error Atasco falso)
       tiempoInicioMovimiento = millis(); 
       lastMoveTime = millis();
       lastPosCheck = pulsosActuales;

       // 4. DECIDIR DIRECCIÓN (Lógica Corregida)
       
       // CASO A: Estamos ABAJO y queremos SUBIR (ej: estamos en 50, vamos a 100)
       if (pulsosActuales < pulsosObjetivo) { 
           buscandoBajar = false; 
           // Activamos ABRIR (Subir)
           digitalWrite(pinAbrir, HIGH); digitalWrite(pinCerrar, LOW); 
           motorEnMovimiento = true; sentidoGiro = 1; moviendoAutomatico = true; 
       } 
       // CASO B: Estamos ARRIBA y queremos BAJAR (ej: estamos en 102, vamos a 100)
       else if (pulsosActuales > pulsosObjetivo) { 
           buscandoBajar = true; 
           // Activamos CERRAR (Bajar)
           digitalWrite(pinAbrir, LOW); digitalWrite(pinCerrar, HIGH); 
           motorEnMovimiento = true; sentidoGiro = -1; moviendoAutomatico = true; 
       }
    }


    void moverARemoto(int porcentaje) {
      movimientoPorRed = true;
      moverA(porcentaje);
    }
    
    // --- ACCIONES MANUALES (RESETEAN ERROR) ---
    void abrirManual() {
      movimientoPorRed = false;
      esperandoParadaReal = false;
      errorLimite = false; // REARME MANUAL
      errorAtasco = false;
      
      if (enEmergencia) return;
      if (calibrando && modoTiempo && esperandoInicio) {
         tiempoInicioMovimiento = millis(); esperandoInicio = false; midiendoTiempo = true; tiempoInvertido = false; 
         debug(">> CAL (BTN): Midiendo (Abriendo)...");
      }
      digitalWrite(pinAbrir, HIGH); digitalWrite(pinCerrar, LOW);
      motorEnMovimiento = true; sentidoGiro = 1; moviendoAutomatico = false;
    }

    void cerrarManual() {
      movimientoPorRed = false;
      esperandoParadaReal = false;
      errorLimite = false; // REARME MANUAL
      errorAtasco = false;
      
      if (enEmergencia) return;
      if (calibrando && modoTiempo && esperandoInicio) {
         tiempoInicioMovimiento = millis(); esperandoInicio = false; midiendoTiempo = true; tiempoInvertido = true; 
         debug(">> CAL (BTN): Midiendo (Cerrando)...");
      }
      digitalWrite(pinAbrir, LOW); digitalWrite(pinCerrar, HIGH);
      motorEnMovimiento = true; sentidoGiro = -1; moviendoAutomatico = false;
    }

    bool esMovimientoRed() {
        return (motorEnMovimiento && movimientoPorRed);
    }

    void parar() {
      digitalWrite(pinAbrir, LOW); digitalWrite(pinCerrar, LOW);

      // CRÍTICO: Al parar (ya sea por botón o por sistema),
      // reseteamos el flag de red para que el PLC deje de buscar el PONG.
      movimientoPorRed = false;
      
      if (modoTiempo && motorEnMovimiento && !calibrando && moviendoAutomatico) {
        unsigned long delta = millis() - tiempoInicioMovimiento;
        if (debeSumarTiempo()) { posicionActualTiempo += delta; if (posicionActualTiempo > tiempoTotalRecorrido) posicionActualTiempo = tiempoTotalRecorrido; } 
        else { if (delta > posicionActualTiempo) posicionActualTiempo = 0; else posicionActualTiempo -= delta; }
      }

      if (calibrando && modoTiempo && midiendoTiempo && motorEnMovimiento) {
         tiempoTotalRecorrido = millis() - tiempoInicioMovimiento;
         posicionActualTiempo = tiempoTotalRecorrido; 
         set100(); midiendoTiempo = false; finCalibrado(); 
      }
      
      motorEnMovimiento = false; moviendoAutomatico = false; sentidoGiro = 0;

      if (!calibrando) {
        if (modoTiempo) {
          prefs.putULong("posTime", posicionActualTiempo); 
        } else {
          esperandoParadaReal = true;
          lastEncValStop = enc.getCount();
          lastEncTimeStop = millis();
          debug("Frenando...");
        }
      }
    }

    // --- MÉTODOS DE CALIBRACIÓN Y GETTERS ---
    void calibrar() { 
       calibrando = true; midiendoTiempo = false; esperandoInicio = false; posicionActualTiempo = 0; 
       pasoCalibracion = 1; 
       debug("MODO CALIBRACIÓN ACTIVADO."); 
    }
    void setZero() { 
       if (!calibrando) return;
       pasoCalibracion = 2; 
       if (modoTiempo) { 
          esperandoInicio = true; midiendoTiempo = false; tiempoInvertido = false; 
          prefs.putULong("posTime", 0); debug(">> Punto 0 FIJADO (Tiempo)."); 
       } else { 
          enc.clearCount(); prefs.putLong("posEnc", 0); debug(">> Punto 0 FIJADO (Encoder)."); 
       }
    }
    void set100() { 
       if (!calibrando) return;
       if (!modoTiempo) { 
          pulsos100 = enc.getCount(); prefs.putLong("pulsos100", pulsos100); debug(">> 100% FIJADO: " + String(pulsos100)); 
       } else { 
          prefs.putULong("timeTotal", tiempoTotalRecorrido); debug(">> 100% FIJADO: " + String(tiempoTotalRecorrido) + " ms"); 
       }
    }
    void finCalibrado() { 
       if (!calibrando) return;
       calibrando = false; pasoCalibracion = 0; mostrandoExito = true; finCalibracionTime = millis(); 
       debug("FIN CALIBRACIÓN."); 
    }

    bool estaCalibrando() { 
        return calibrando; 
    }

    bool estaCalibrado() {
        if (modoTiempo) return (tiempoTotalRecorrido > 0);
        else return (pulsos100 != 0);
    }
    
    bool esModoTiempo() {
        return modoTiempo;
    }
    
    DatosMotor getPosicionActual() {
      DatosMotor datos;
      if (modoTiempo) { datos.valor = (long)posicionActualTiempo; datos.porcentaje = -1.0; } 
      else { datos.valor = enc.getCount(); if (pulsos100 != 0) datos.porcentaje = ((float)datos.valor / (float)pulsos100) * 100.0; else datos.porcentaje = -1.0; }
      return datos;
    }

    String getEstadoString() {
      if (enEmergencia) return MSG_EMERGENCIA;
      if (errorLimite)  return MSG_ERROR_LIMITE; // Aviso de límite superado
      if (errorAtasco)  return MSG_ERROR_ATASCO;

      bool calibrado = (modoTiempo && tiempoTotalRecorrido > 0) || (!modoTiempo && pulsos100 != 0);
      if (!calibrado) return MSG_NO_CALIBRADO;
      if (calibrando) return MSG_CALIBRANDO;

      if (motorEnMovimiento) {
         if (sentidoGiro == 1) return MSG_ABRIENDO; 
         if (sentidoGiro == -1) return MSG_CERRANDO;
      }

      float p = 0.0;
      if (modoTiempo) {
          unsigned long posViva = getPosicionTiempoEstimada();
          if (tiempoTotalRecorrido > 0) p = ((float)posViva / (float)tiempoTotalRecorrido) * 100.0;
      } else {
          DatosMotor datos = getPosicionActual();
          p = datos.porcentaje;
      }

      if (p >= 98.0) return MSG_ABIERTO;
      if (p <= 2.0)  return MSG_CERRADO;
      
      return MSG_PARADO;
    }

    int getPorcentajeEntero() {
       if (modoTiempo) {
          if (tiempoTotalRecorrido == 0) return 0;
          unsigned long posViva = getPosicionTiempoEstimada();
          float p = ((float)posViva / (float)tiempoTotalRecorrido) * 100.0;
          return constrain((int)p, 0, 100);
       } else {
          DatosMotor datos = getPosicionActual();
          if (datos.porcentaje < 0) return 0; 
          return (int)datos.porcentaje;
       }
    }

    void imprimirEstado() {
      String msg = "";
      if (modoTiempo) {
        msg = "[TIEMPO] Actual: " + String(posicionActualTiempo) + " ms / Total: " + String(tiempoTotalRecorrido) + " ms";
        if (tiempoTotalRecorrido == 0) msg += " (NO CALIBRADO)";
      } 
      else {
        long pulsosActuales = enc.getCount();
        String strPorc = "";
        if (pulsos100 != 0) {
          float porcentaje = ((float)pulsosActuales / (float)pulsos100) * 100.0;
          strPorc = String(porcentaje, 1) + "%"; 
        } else { strPorc = "NO CAL"; }
        msg = "[ENCODER] " + strPorc + " | Pulsos: " + String(pulsosActuales) + " / " + String(pulsos100);
        msg += " | Anticip: " + String(pulsosAnticipacion);
      }
      debug(msg);
    }
  };

#endif
