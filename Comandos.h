#ifndef COMANDOS_H
#define COMANDOS_H

// ============================================================
//  Comandos.h — Adaptado para M-Duino 21+ (Arduino Mega)
//
//  Simplificado respecto a la versión ESP32:
//   • Eliminados todos los parámetros de Roll-Up (ru1/ru2/ru3)
//   • Esta finca solo tiene un motor de techo
// ============================================================

#include "Motor.h"
#include "GrupoMotores.h"

// ============================================================
//  Tabla de comandos disponibles
//
//  a       → Abrir techo (manual si esRemoto=false, auto si =true)
//  z       → Cerrar techo
//  s       → STOP
//  m<N>    → Mover a N% (ej: m50, m0, m100)
//  c       → Iniciar secuencia de calibración
//  0       → Set Zero (paso 2 de calibración)
//  1       → Set 100% (paso 3 de calibración)
//  f       → Finalizar calibración
//  E       → Modo ENCODER
//  T       → Modo TIEMPO
//  A<N>    → Ajustar anticipación del freno (N pulsos, ej: A30)
//  i       → Imprimir estado por Serial
//  M       → Activar/desactivar verbose MQTT (diagnóstico)
//  F<N>    → Fijar frecuencia del VFD en N Hz (ej: F50, F40, F35). Rango: 0-50Hz.
//  R       → Reset de errores del variador VFD
// ============================================================

void ejecutarComando(String cmd, GrupoMotores* grupo, bool esRemoto = false) {
  if (cmd.length() == 0 || grupo == nullptr) return;

  char c = cmd.charAt(0);

  // --- Comandos de configuración y calibración ---
  if      (c == 'T') { grupo->setModo(true);  return; }
  else if (c == 'E') { grupo->setModo(false); return; }
  else if (c == 'c') { grupo->calibrar();     return; }
  else if (c == '0') { grupo->setZero();      return; }
  else if (c == '1') { grupo->set100();       return; }
  else if (c == 'f') { grupo->finCalibrado(); return; }
  else if (c == 'i') { grupo->imprimirEstado(); return; }
  else if (c == 'R') { grupo->resetErroresVFD(); return; }
  else if (c == 'X') { grupo->forzarFallaExterna(); return; }  // TEST: inyecta falla externa (cod.6). Limpiar con R.
  else if (c == 'F') {
    // Formato: F50 = 50Hz, F40 = 40Hz, F35 = 35Hz (entero o decimal, ej: F47.5)
    // Rango válido: 0 – FREQ_MAX_HZ (50Hz). El VFD_Control lo limita internamente.
    float hz = cmd.substring(1).toFloat();
    grupo->setFrecuenciaVFD(hz);
    return;
  }

  // --- Comandos de movimiento ---
  if (c == 'a') {
    if (esRemoto) grupo->abrir();
    else          grupo->abrirManual();
  }
  else if (c == 'z') {
    if (esRemoto) grupo->cerrar();
    else          grupo->cerrarManual();
  }
  else if (c == 's') {
    grupo->parar();
  }
  // Movimiento libre (sin límites de calibración) — desde panel de admin
  // 'u' = up free  → abrirManual()  (ignora posición, respeta seta y error VFD)
  // 'd' = down free → cerrarManual() (ignora posición, respeta seta y error VFD)
  else if (c == 'u') {
    grupo->abrirManual();
  }
  else if (c == 'd') {
    grupo->cerrarManual();
  }
  // Comandos con valor numérico (ej: "m50", "A30")
  else if (cmd.length() > 1) {
    int val = cmd.substring(1).toInt();
    if      (c == 'm') grupo->moverA(val);
    else if (c == 'A') grupo->setAnticipacion(val);
  }
}

#endif
