#ifndef VFD_CONSTANTES_H
#define VFD_CONSTANTES_H

#include <Arduino.h>

// --- PARÁMETROS DE CONEXIÓN ---
const uint8_t  SLAVE_ID       = 1;
const uint32_t BAUDRATE       = 9600;
const uint16_t SERIAL_CONFIG  = SERIAL_8E1;

// --- REGISTROS MODBUS ---
const uint16_t REG_CONTROL     = 0x2000; // Comando de control (marcha/parada)
const uint16_t REG_FREQ_SET    = 0x2001; // Consigna de frecuencia por comunicación (0-10000 = 0-100% de FREQ_MAX_HZ)
const uint16_t REG_FAULT_CMD   = 0x2002; // Comando de falla externa y reset
const uint16_t REG_STATUS_BASE = 0x2100; // Bloque de lectura confirmado: empieza en 2100H
const uint16_t READ_COUNT      = 7;      // 2100H-2106H: falla, estado, freq_cfg, freq_sal, corriente, bus, tension

// --- ÍNDICES DENTRO DEL BLOQUE DE LECTURA (offset desde 2100H) ---
// reg[0] = 2100H → código de falla activa (0 = sin falla)
// reg[1] = 2101H → estado operativo (bits Run/Stop/Dir)
// reg[2] = 2102H → frecuencia configurada (÷100 Hz)
// reg[3] = 2103H → frecuencia de salida   (÷100 Hz)
// reg[4] = 2104H → corriente de salida    (÷10 A)
// reg[5] = 2105H → voltaje bus DC         (÷10 V)
// reg[6] = 2106H → voltaje de salida      (÷10 V)
const uint8_t  IDX_FAULT      = 0;
const uint8_t  IDX_STATUS     = 1;
const uint8_t  IDX_FREQ_CFG   = 2;
const uint8_t  IDX_FREQ_SAL   = 3;
const uint8_t  IDX_CORR       = 4;
const uint8_t  IDX_VBUS       = 5;
const uint8_t  IDX_VSAL       = 6;

// --- FRECUENCIA MÁXIMA DEL VARIADOR ---
// Debe coincidir con el parámetro P0.03 configurado en el VFD físico.
const float    FREQ_MAX_HZ     = 50.0;   // Hz — estándar europeo 50Hz

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