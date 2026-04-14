#ifndef COMANDOS_H
#define COMANDOS_H

#include "Motor.h"
#include "MotorRollUp.h"
#include "GrupoMotores.h"

// Ahora pasamos las referencias a los 3 Roll-Ups
void ejecutarComando(String cmd, GrupoMotores* grupo, MotorRollUp* ru1, MotorRollUp* ru2, MotorRollUp* ru3, bool esRemoto = false) {
  if (cmd.length() == 0) return;
  
  char c = cmd.charAt(0);
  
  // --- LÓGICA DE SELECCIÓN DE ROLL-UP ---
  // Si el comando tiene un número después de la letra (ej: "u2"), seleccionamos ese motor.
  // Si no tiene número (ej: "u"), por defecto usamos ru1.
  int indiceRU = 1; 
  if (cmd.length() > 1 && isDigit(cmd.charAt(1))) {
      indiceRU = String(cmd.charAt(1)).toInt();
  }

  MotorRollUp* ruSeleccionado = nullptr;
  if      (indiceRU == 1) ruSeleccionado = ru1;
  else if (indiceRU == 2) ruSeleccionado = ru2;
  else if (indiceRU == 3) ruSeleccionado = ru3;

  // --- EJECUCIÓN COMANDOS ROLL-UP ---
  if (ruSeleccionado != nullptr) {
      if      (c == 'u') { ruSeleccionado->subir(); return; }
      else if (c == 'd') { ruSeleccionado->bajar(); return; }
      else if (c == 'p') { ruSeleccionado->parar(); return; }
  }

  // --- GRUPO DE MOTORES (TECHO) ---
  if (grupo == nullptr) return;

  // Comandos de Configuración
  if      (c == 'T') { grupo->setModo(true);  return; }
  else if (c == 'E') { grupo->setModo(false); return; }
  else if (c == 'c') { grupo->calibrar();     return; }
  else if (c == '0') { grupo->setZero();      return; }
  else if (c == '1') { grupo->set100();       return; }
  else if (c == 'f') { grupo->finCalibrado(); return; }
  else if (c == 'i') { 
      grupo->imprimirEstado(); 
      if (ru1) ru1->imprimirEstado(); 
      if (ru2) ru2->imprimirEstado(); 
      if (ru3) ru3->imprimirEstado(); 
      return; 
  }

  // Comandos de Movimiento Techo
  if (c == 'a') { 
      if (esRemoto) grupo->abrir(); 
      else grupo->abrirManual(); 
  }
  else if (c == 'z') {
      if (esRemoto) grupo->cerrar();
      else grupo->cerrarManual();
  }
  else if (c == 's') {
      grupo->parar();
  }
  else if (cmd.length() > 1 /*&& !isDigit(cmd.charAt(1))*/) { 
    // Si el segundo caracter no es un número de RU, es un valor (ej: "m50")
    int val = cmd.substring(1).toInt();
    if      (c == 'A') grupo->setAnticipacion(val);
    else if (c == 'm') Serial.println("---ENTRAAA---");grupo->moverA(val);
  }
}

#endif

/*#ifndef COMANDOS_H
#define COMANDOS_H

#include "Motor.h"
#include "MotorRollUp.h"
#include "GrupoMotores.h"

// Ahora pasamos la referencia al Grupo en lugar de al Motor individual
void ejecutarComando(String cmd, GrupoMotores* grupo, MotorRollUp* ru1, bool esRemoto = false) {
  if (cmd.length() == 0) return;
  char c = cmd.charAt(0);

  // --- ROLL-UP (Independiente) ---
  if      (c == 'u') { if (ru1) ru1->subir(); return; }
  else if (c == 'd') { if (ru1) ru1->bajar(); return; }
  else if (c == 'p') { if (ru1) ru1->parar(); return; }

  // --- GRUPO DE MOTORES (TECHO) ---
  if (grupo == nullptr) return;

  // Comandos de Configuración (No disparan movimiento crítico de red)
  if      (c == 'T') { grupo->setModo(true);  return; }
  else if (c == 'E') { grupo->setModo(false); return; }
  else if (c == 'c') { grupo->calibrar();     return; }
  else if (c == '0') { grupo->setZero();      return; }
  else if (c == '1') { grupo->set100();       return; }
  else if (c == 'f') { grupo->finCalibrado(); return; }
  else if (c == 'i') { grupo->imprimirEstado(); if (ru1) ru1->imprimirEstado(); return; }

  // Comandos de Movimiento (Sujetos a la seguridad del Latido)
  if (c == 'a') { 
      if (esRemoto) grupo->abrir(); 
      else grupo->abrirManual(); // <-- Usar la manual aquí
  }
  else if (c == 'z') {
      if (esRemoto) grupo->cerrar();
      else grupo->cerrarManual(); // <-- Y aquí
  }
  else if (c == 's') {
      grupo->parar();
  }
  else if (cmd.length() > 1) {
    int val = cmd.substring(1).toInt();
    if      (c == 'A') grupo->setAnticipacion(val);
    else if (c == 'm') grupo->moverA(val);
  }
}

#endif
*/