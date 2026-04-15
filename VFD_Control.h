#ifndef VFD_CONTROL_H
#define VFD_CONTROL_H

#include <ModbusRTUMaster.h>
#include "VFD_Constantes.h"

class VFDController {
  private:
    ModbusRTUMaster& _master;

    // --- LOGGING ---
    typedef void (*LogCallback)(String);
    LogCallback _log     = nullptr;
    bool        _verbose = false;

    void debug(String msg) { if (_log) _log("[VFD] " + msg); }
    void vlog(String msg)  { if (_verbose && _log) _log("[VFD] " + msg); }

    // --- ESTADO RESPUESTAS ---
    bool          _esperandoLectura   = false;
    bool          _esHeartbeat        = false;
    unsigned long _ultimaComunicacion = 0;
    static const unsigned long INTERVALO_HEARTBEAT = 4000UL; // 4s (VFD timeout P6.02 = 10s)

  public:
    VFDController(ModbusRTUMaster& master) : _master(master) {}

    void setLogger(LogCallback cb) { _log = cb; }

    void toggleVerbose() {
      _verbose = !_verbose;
      debug(_verbose ? F("Verbose ON") : F("Verbose OFF"));
    }

    bool isBusy() { return _master.isWaitingResponse(); }

    // --- COMANDOS (comprueban bus libre antes de enviar) ---
    void marchaAdelante() {
      if (isBusy()) { vlog(F("Bus ocupado, ignorando marchaAdelante")); return; }
      vlog(F(">> TX FC06 REG_CONTROL=0x0012 (RUN_FWD)"));
      _master.writeSingleRegister(SLAVE_ID, REG_CONTROL, CMD_RUN_FWD);
      _ultimaComunicacion = millis();
    }

    void marchaAtras() {
      if (isBusy()) { vlog(F("Bus ocupado, ignorando marchaAtras")); return; }
      vlog(F(">> TX FC06 REG_CONTROL=0x0022 (RUN_REV)"));
      _master.writeSingleRegister(SLAVE_ID, REG_CONTROL, CMD_RUN_REV);
      _ultimaComunicacion = millis();
    }

    void parar() {
      if (isBusy()) { vlog(F("Bus ocupado, ignorando parar")); return; }
      vlog(F(">> TX FC06 REG_CONTROL=0x0001 (STOP)"));
      _master.writeSingleRegister(SLAVE_ID, REG_CONTROL, CMD_STOP);
      _ultimaComunicacion = millis();
    }

    void resetErrores() {
      if (isBusy()) { vlog(F("Bus ocupado, ignorando resetErrores")); return; }
      vlog(F(">> TX FC06 REG_FAULT_CMD=0x0002 (RESET_FLT)"));
      _master.writeSingleRegister(SLAVE_ID, REG_FAULT_CMD, CMD_RESET_FLT);
      _ultimaComunicacion = millis();
    }

    // Inyecta una falla externa (para pruebas). El variador detiene el motor.
    // Para limpiarla después: enviar resetErrores()
    void forzarFallaExterna() {
      if (isBusy()) { vlog(F("Bus ocupado, ignorando forzarFallaExterna")); return; }
      debug(F(">> TEST: Inyectando falla externa (CMD_EXT_FAULT)"));
      _master.writeSingleRegister(SLAVE_ID, REG_FAULT_CMD, CMD_EXT_FAULT);
      _ultimaComunicacion = millis();
    }

    bool actualizar() {
      if (isBusy()) return false;
      vlog(F(">> TX FC03 lectura 2101H-2106H"));
      bool ok = _master.readHoldingRegisters(SLAVE_ID, REG_STATUS_BASE, READ_COUNT);
      if (ok) { _esperandoLectura = true; _ultimaComunicacion = millis(); }
      return ok;
    }

    // --------------------------------------------------------
    //  UPDATE — llamar cada ciclo de loop()
    //  Gestiona heartbeat keep-alive + procesado de respuestas
    // --------------------------------------------------------
    void update() {

      // Heartbeat: lectura silenciosa cada 4s para evitar error E485
      if (!isBusy() && (millis() - _ultimaComunicacion >= INTERVALO_HEARTBEAT)) {
        vlog(F("Heartbeat keep-alive..."));
        _master.readHoldingRegisters(SLAVE_ID, REG_STATUS_BASE, READ_COUNT);
        _esperandoLectura   = true;
        _esHeartbeat        = true;
        _ultimaComunicacion = millis();
      }

      // Procesar respuesta pendiente
      if (!isBusy()) return;
      ModbusResponse res = _master.available();
      if (!res) return;

      if (!_esperandoLectura) {
        // Respuesta de escritura (FC06)
        if (!res.hasError()) {
          vlog(F("<< OK escritura confirmada"));
        } else {
          debug("<< ERROR escritura (cod:" + String(res.getErrorCode()) + ")");
        }

      } else {
        // Respuesta de lectura (FC03)
        if (!res.hasError()) {
          uint16_t statusReg  = res.getRegister(0);
          float    frecuencia = res.getRegister(2) / SCALE_FREQ;

          // Interpretación según manual registro 2101H:
          // Bit1  (0x0002) = Apagado         → motor detenido
          // Bit3  (0x0008) = Avance           → dirección FWD seleccionada (puede activo sin girar)
          // Bit4  (0x0010) = Reversa          → dirección REV seleccionada
          // Bit10 (0x0400) = Canal Modbus     → comandos vía RS485 activos
          // Bit12 (0x1000) = En funcionamiento→ motor girando realmente
          bool apagado      = (statusReg & 0x0002);
          bool enFwd        = (statusReg & 0x0008);
          bool enRev        = (statusReg & 0x0010);
          bool enMarcha     = (statusReg & 0x1000);
          bool canalModbus  = (statusReg & 0x0400);

          bool enMovimiento = (bool)enMarcha;

          uint16_t faultCode = (!enMovimiento && statusReg >= 1 && statusReg <= 17)
                               ? statusReg : 0;

          String estado;
          if      (enMarcha && enRev)  estado = "EN MARCHA REV";
          else if (enMarcha)           estado = "EN MARCHA FWD";
          else if (apagado)            estado = "PARADO";
          else                         estado = "STANDBY";

          // Estado normal: solo en verbose
          if (!_esHeartbeat || _verbose) {
            vlog("<< 2101H=0x" + String(statusReg, HEX) +
                 " | Freq=" + String(frecuencia) + "Hz" +
                 " | " + estado +
                 (canalModbus ? " | [Modbus]" : " | [Local]"));
          }

          // Fallo: SIEMPRE visible, independientemente del verbose
          if (faultCode > 0) {
            InfoError info = obtenerInfoError(faultCode);
            debug("!!! FALLA VARIADOR cod:" + String(faultCode) +
                  " — " + info.nombre + " — " + info.detalle);
          }

        } else {
          // Error de comunicación RS485: siempre visible
          if (!_esHeartbeat) {
            debug("<< ERROR RS485 (cod:" + String(res.getErrorCode()) +
                  "). Revisar cable y terminacion.");
          } else {
            vlog("<< Heartbeat sin respuesta (cod:" + String(res.getErrorCode()) + ")");
          }
        }
      }

      _esperandoLectura = false;
      _esHeartbeat      = false;
    }

    InfoError diagnosticoCompleto(uint16_t codigo) {
      return obtenerInfoError(codigo);
    }
};

#endif
