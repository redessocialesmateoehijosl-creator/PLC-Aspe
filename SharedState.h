#ifndef SHAREDSTATE_H
#define SHAREDSTATE_H

// ============================================================
//  SharedState.h — Variables compartidas entre tasks FreeRTOS
//
//  Dos tasks acceden a recursos comunes:
//    • taskMotor   (prioridad 2) — motor, encoder, VFD, botones
//    • taskNetwork (prioridad 1) — Ethernet, MQTT, heartbeat
//
//  Principio de diseño:
//    • Las variables de control de seguridad son 'volatile bool'
//      → en AVR, bool es 1 byte y su lectura/escritura es ATÓMICA.
//    • El flujo es siempre productor-único / consumidor-único:
//      taskNetwork escribe flags, taskMotor los consume.
//      Nunca hay dos productores simultáneos en las mismas variables.
//    • Las lecturas de estado del motor por la tarea de red (para
//      publicar por MQTT) son 'eventual consistency': un ciclo de
//      retraso es irrelevante para telemetría, nunca para seguridad.
// ============================================================

// ── Network → Motor: parada de emergencia ─────────────────
// Seteado por taskNetwork cuando el heartbeat MQTT expira.
// Consumido (y limpiado) por taskMotor en cada ciclo.
volatile bool emergencyStopRequest = false;

// ── Network → Motor: comando MQTT pendiente ───────────────
// Protocolo productor/consumidor de 1 slot:
//   1. taskNetwork comprueba !cmdPendiente
//   2. Copia el comando en cmdBuffer[]
//   3. Pone cmdEsRemoto
//   4. Setea cmdPendiente = true   ← SIEMPRE el último paso
//
//   taskMotor:
//   1. Lee cmdPendiente
//   2. Limpia cmdPendiente = false  ← ANTES de ejecutar
//   3. Ejecuta el comando
//
// Garantía: bool es atómico en AVR → no hay race en el flag.
#define SHARED_CMD_MAX 32
volatile bool cmdPendiente       = false;
volatile char cmdBuffer[SHARED_CMD_MAX] = {0};
volatile bool cmdEsRemoto        = false;

#endif
