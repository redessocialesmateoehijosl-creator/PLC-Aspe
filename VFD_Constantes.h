#ifndef VFD_CONSTANTES_H
#define VFD_CONSTANTES_H

#include <Arduino.h>

// --- PARÁMETROS DE CONEXIÓN ---
const uint8_t  SLAVE_ID       = 1;
const uint32_t BAUDRATE       = 9600;
const uint16_t SERIAL_CONFIG  = SERIAL_8E1;

// --- REGISTROS MODBUS ---
const uint16_t REG_CONTROL     = 0x2000; // Comando de control (marcha/parada)
const uint16_t REG_FAULT_CMD   = 0x2002; // Comando de falla externa y reset
const uint16_t REG_STATUS_BASE = 0x2101; // Primer registro de lectura documentado
const uint16_t READ_COUNT      = 6;      // 2101H-2106H: estado, freq_cfg, freq_sal, corriente, bus, tension

// --- VALORES DE COMANDO (Registro 2000H) ---
const uint16_t CMD_STOP       = 0x0001;
const uint16_t CMD_RUN_FWD    = 0x0012;
const uint16_t CMD_RUN_REV    = 0x0022;

// --- VALORES DE COMANDO (Registro 2002H) ---
const uint16_t CMD_EXT_FAULT  = 0x0001; // Entrada de falla externa
const uint16_t CMD_RESET_FLT  = 0x0002; // Reinicio de falla

// --- ESCALAS DE DATOS ---
const float SCALE_FREQ        = 100.0;

// --- ESTRUCTURA DE DATOS PARA ERRORES ---
struct InfoError {
  String nombre;
  String detalle;
};

// --- TRADUCTOR DE CÓDIGOS (Basado en Tabla Modbus 2101H) ---
inline InfoError obtenerInfoError(uint16_t code) {
  switch (code) {
    case 0:  
      return {"OK", "Sistema operando normalmente. Sin anomalias."};
    case 1:  
      return {"Falla de modulo", "Falla critica en el modulo de potencia (IGBT). Revisar cortocircuitos en salida o sobrecarga extrema instantanea."};
    case 2:  
      return {"Sobretension", "Voltaje del bus DC excedido. Revisar si el voltaje de entrada es muy alto o si la inercia del motor requiere resistencia de frenado."};
    case 3:  
      return {"Falla de temperatura", "Sobrecalentamiento del disipador. Revisar limpieza de ranuras de aire y funcionamiento de ventiladores internos."};
    case 4:  
      return {"Sobrecarga del inversor", "El variador esta entregando mas corriente de la nominal. Revisar si el variador es pequeño para el motor o la carga."};
    case 5:  
      return {"Sobrecarga del motor", "Proteccion termica activa. El motor esta trabajando forzado. Revisar rozamientos mecanicos o atascos."};
    case 6:  
      return {"Falla externa", "Entrada digital de falla externa activada. Revisar seguridades externas conectadas a los bornes del variador."};
    case 10: 
      return {"Sobrecorriente en ACEL", "Corriente excesiva durante el arranque. Aumentar el tiempo de aceleracion (P0.11) o revisar par de arranque."};
    case 11: 
      return {"Sobrecorriente en DECE", "Corriente excesiva durante el frenado. Aumentar el tiempo de desaceleracion (P0.12)."};
    case 12: 
      return {"Sobrecorriente en VEL CONST", "Pico de corriente en funcionamiento estable. Revisar posibles enganchones mecanicos repentinos."};
    case 14:
      return {"Subtension", "Voltaje de entrada por debajo del limite operacional. Revisar caidas de tension en la red o contactores de entrada."};
    case 15:
      return {"Perdida de fase de entrada (E-iLF)", "Una o mas fases de la alimentacion trifasica estan ausentes o desequilibradas. Revisar fusibles y contactor de entrada."};
    case 16:
      return {"Perdida de fase de salida (E-oLF)", "Una o mas fases de salida hacia el motor estan abiertas. Revisar conexiones del motor, terminales U/V/W y el cable de potencia."};
    case 17:
      return {"Error de comunicacion (E485)", "Falla de enlace entre PLC y Variador. Revisar cableado RS485, ruidos electricos y terminal GND."};
    default: 
      return {"Error desconocido (" + String(code) + ")", "Codigo de falla no documentado. Consulte el manual tecnico del fabricante."};
  }
}

#endif