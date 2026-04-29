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

    // Envío directo del comando STOP (bus debe estar libre antes de llamar)
    void _enviarStopAhora() {
      _stopPendiente    = false;
      _esperandoLectura = false;   // es un write (FC06), no una lectura
      _esHeartbeat      = false;
      vlog(F(">> TX FC06 REG_CONTROL=0x0001 (STOP)"));
      _master.writeSingleRegister(SLAVE_ID, REG_CONTROL, CMD_STOP);
      _ultimaComunicacion = millis();
    }

    // --- ESTADO RESPUESTAS ---
    bool          _esperandoLectura   = false;
    bool          _esHeartbeat        = false;
    unsigned long _ultimaComunicacion = 0;
    // Intervalo de polling Modbus: rápido en marcha (monitoreo crítico), lento en reposo
    static const unsigned long INTERVALO_HB_MARCHA = 200UL;   // 0.2s — motor en movimiento
    static const unsigned long INTERVALO_HB_REPOSO = 2000UL;  // 2s   — motor parado (P6.02=10s)

    // Cola de prioridad para STOP:
    // Si parar() se llama con el bus Modbus ocupado, el comando se
    // encola aquí y se despacha en el próximo ciclo de update() en
    // cuanto el bus quede libre. Garantiza que un stop de seguridad
    // nunca se pierda por un heartbeat o lectura en curso.
    bool          _stopPendiente      = false;

    // --- FEEDBACK ESTADO MOTOR (último parse válido de 2101H) ---
    bool          _motorGirandoUlt       = false;  // Bit12 "En funcionamiento"
    bool          _motorApagadoUlt       = false;  // Bit1  "Apagado"
    unsigned long _tsUltimaLecturaEstado = 0;      // millis() del último parse OK

    // --- FALLA ACTIVA DEL VARIADOR ---
    bool          _hayFalla              = false;  // true = variador en estado de falla
    uint16_t      _faultCode             = 0;      // 0 = sin falla; 1-14 = código activo (manual)

    // Antirrebote de falla: se requieren 2 lecturas consecutivas con el mismo
    // código para declarar falla real. Evita falsos positivos por ruido RS485
    // o estados de transición (arranque de rampa) donde el variador devuelve
    // brevemente un valor sin Bit10 que cae dentro del rango 1-14.
    uint8_t       _faultConsecutivo      = 0;      // contador de lecturas con falla
    uint16_t      _faultCandidato        = 0;      // último código candidato
    static const uint8_t FAULT_CONFIRM_MIN = 2;    // lecturas consecutivas para confirmar

    // --- FRECUENCIA PENDIENTE AL ARRANQUE ---
    // Se envía tras la primera comunicación Modbus exitosa
    bool          _freqPendienteOK       = false;
    float         _freqPendiente         = 0.0f;

    // --- TELEMETRÍA: últimos valores leídos (registros 2101H-2106H) ---
    float    _freqSalida      = 0.0;   // Hz  (2103H, 2 decimales)
    float    _freqConfigurada = 0.0;   // Hz  (2102H, 2 decimales)
    float    _corriente       = 0.0;   // A   (2104H, 1 decimal)
    float    _vBus            = 0.0;   // V   (2105H, 1 decimal)
    float    _vSalida         = 0.0;   // V   (2106H, 1 decimal)
    String   _estadoStr       = "";    // "PARADO" / "EN MARCHA FWD" / etc.

  public:
    VFDController(ModbusRTUMaster& master) : _master(master) {}

    void setLogger(LogCallback cb) { _log = cb; }

    void toggleVerbose() {
      _verbose = !_verbose;
      debug(_verbose ? F("Verbose ON") : F("Verbose OFF"));
    }

    bool isBusy() { return _master.isWaitingResponse(); }

    // --- ACCESORES DE ESTADO (última lectura 2101H válida) -------------
    //  motorGirando()   → true si el variador reportó Bit12="En funcionamiento"
    //  motorApagado()   → true si el variador reportó Bit1="Apagado"
    //  estadoFresco(ms) → true si la última lectura válida es más reciente que 'ms'
    bool motorGirando()                   { return _motorGirandoUlt; }
    bool motorApagado()                   { return _motorApagadoUlt; }
    bool estadoFresco(unsigned long ms)   { return (millis() - _tsUltimaLecturaEstado) < ms; }
    unsigned long msDesdeUltimaLectura()  { return millis() - _tsUltimaLecturaEstado; }

    // --- Falla activa del variador ---
    bool     hayFalla()       { return _hayFalla;   }
    uint16_t getCodigoFalla() { return _faultCode;  }
    void     limpiarFalla()   { _hayFalla = false; _faultCode = 0; _faultConsecutivo = 0; _faultCandidato = 0; }

    // --- Frecuencia pendiente al arranque ---
    // Motor::begin() llama esto al arrancar con la frecuencia guardada en EEPROM.
    // Se enviará automáticamente tras la primera respuesta Modbus válida.
    void setFreqPendiente(float hz) {
      _freqPendiente   = hz;
      _freqPendienteOK = false;  // pendiente de enviar
    }

    // --- Telemetría: getters de los últimos valores leídos ---
    float  getFreqSalida()      { return _freqSalida;      }
    float  getFreqConfigurada() { return _freqConfigurada; }
    float  getCorriente()       { return _corriente;       }
    float  getVBus()            { return _vBus;            }
    float  getVSalida()         { return _vSalida;         }
    String getEstadoStr()       { return _estadoStr;       }

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
      _stopPendiente = true;   // marca siempre, incluso si el bus está ocupado
      if (isBusy()) {
        vlog(F("Bus ocupado. STOP encolado: se enviara en el proximo ciclo libre."));
        return;
      }
      _enviarStopAhora();
    }

    void resetErrores() {
      if (isBusy()) { vlog(F("Bus ocupado, ignorando resetErrores")); return; }
      vlog(F(">> TX FC06 REG_FAULT_CMD=0x0002 (RESET_FLT)"));
      _master.writeSingleRegister(SLAVE_ID, REG_FAULT_CMD, CMD_RESET_FLT);
      _ultimaComunicacion = millis();
    }

    // Establece la consigna de frecuencia en el variador vía Modbus (registro 2001H).
    // hz: frecuencia deseada en Hz (0.0 a FREQ_MAX_HZ).
    // El registro espera el valor como porcentaje×100 de la frecuencia máxima:
    //   valor = (hz / FREQ_MAX_HZ) × 10000   →   50Hz = 10000, 25Hz = 5000
    // Requiere que el VFD esté configurado con fuente de frecuencia = RS485 (P0.01).
    void setFrecuencia(float hz) {
      if (isBusy()) { vlog(F("Bus ocupado, ignorando setFrecuencia")); return; }
      if (hz < 0.0f)          hz = 0.0f;
      if (hz > FREQ_MAX_HZ)   hz = FREQ_MAX_HZ;
      uint16_t regVal = (uint16_t)((hz / FREQ_MAX_HZ) * 10000.0f + 0.5f);  // +0.5 para redondeo
      vlog(">> TX FC06 REG_FREQ_SET=" + String(regVal) + " (" + String(hz, 1) + "Hz)");
      _master.writeSingleRegister(SLAVE_ID, REG_FREQ_SET, regVal);
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

      // ── PRIORIDAD MÁXIMA: STOP encolado ──────────────────────────────
      // Si parar() fue llamado mientras el bus estaba ocupado, enviamos
      // el STOP en cuanto el bus queda libre, ANTES que cualquier otra
      // operación (heartbeat, lectura de estado…).
      if (_stopPendiente && !isBusy()) {
        vlog(F(">> [COLA] Enviando STOP prioritario encolado."));   // vlog: solo en verbose
        _enviarStopAhora();
        return;   // procesar la respuesta en el próximo ciclo
      }

      // Heartbeat: intervalo dinámico según estado del motor
      //   En marcha o falla activa → 200ms (monitoreo crítico)
      //   Parado sin falla → 2s (keep-alive, evita error E485 del VFD)
      unsigned long intervaloHB = (_motorGirandoUlt || _hayFalla) ? INTERVALO_HB_MARCHA : INTERVALO_HB_REPOSO;
      if (!isBusy() && (millis() - _ultimaComunicacion >= intervaloHB)) {
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
        // Respuesta de lectura (FC03) — bloque 2100H-2106H
        if (!res.hasError()) {
          // ── Extraer registros del bloque ─────────────────────────────────
          // reg[IDX_FAULT=0]    = 2100H → código de falla (0 = sin falla)
          // reg[IDX_STATUS=1]   = 2101H → bits de estado operativo
          // reg[IDX_FREQ_CFG=2] = 2102H → frecuencia configurada
          // reg[IDX_FREQ_SAL=3] = 2103H → frecuencia de salida
          // reg[IDX_CORR=4]     = 2104H → corriente
          // reg[IDX_VBUS=5]     = 2105H → voltaje bus DC
          // reg[IDX_VSAL=6]     = 2106H → voltaje salida
          uint16_t faultCode = res.getRegister(IDX_FAULT);
          uint16_t statusReg = res.getRegister(IDX_STATUS);
          float    frecuencia = res.getRegister(IDX_FREQ_SAL) / SCALE_FREQ;

          // ── Detección de falla: directa desde 2100H ──────────────────────
          // El registro 2100H contiene el código de falla activa (1-17).
          // Permanece con el código hasta que se envía Reset (2002H=0x0002).
          // Valor 0 = sin falla. No requiere antirrebote: el registro es estable.
          bool enFallo = (faultCode > 0);

          // Limpiar candidato/contadores del antirrebote (ya no necesarios)
          _faultConsecutivo = 0;
          _faultCandidato   = 0;

          // ── Estado operativo desde 2101H ─────────────────────────────────
          // Bit1  (0x0002) = Apagado (motor detenido)
          // Bit3  (0x0008) = Avance (dirección FWD)
          // Bit4  (0x0010) = Reversa (dirección REV)
          // Bit10 (0x0400) = Canal Modbus activo
          // Bit12 (0x1000) = En funcionamiento (motor girando)
          bool apagado     = (statusReg & 0x0002);
          bool enFwd       = (statusReg & 0x0008);
          bool enRev       = (statusReg & 0x0010);
          bool enMarcha    = (statusReg & 0x1000);
          bool canalModbus = (statusReg & 0x0400);

          bool enMovimiento = enMarcha && !enFallo;

          _motorGirandoUlt       = enMovimiento;
          _motorApagadoUlt       = apagado;
          _tsUltimaLecturaEstado = millis();
          _hayFalla              = enFallo;
          _faultCode             = faultCode;

          String estado;
          if      (enFallo)           estado = "!FALLA (cod " + String(faultCode) + ")";
          else if (enMarcha && enRev) estado = "EN MARCHA REV";
          else if (enMarcha)          estado = "EN MARCHA FWD";
          else if (apagado)           estado = "PARADO";
          else                        estado = "STANDBY";

          // ── Telemetría completa para MQTT ─────────────────────────────────
          _freqSalida      = frecuencia;
          _freqConfigurada = res.getRegister(IDX_FREQ_CFG) / SCALE_FREQ;
          _corriente       = res.getRegister(IDX_CORR)     / 10.0f;
          _vBus            = res.getRegister(IDX_VBUS)     / 10.0f;
          _vSalida         = res.getRegister(IDX_VSAL)     / 10.0f;
          _estadoStr       = estado;

          // ── Log ───────────────────────────────────────────────────────────
          if (enFallo || !_esHeartbeat || _verbose) {
            vlog("<< 2100H=" + String(faultCode) +
                 " 2101H=0x" + String(statusReg, HEX) +
                 " | Freq=" + String(frecuencia, 1) + "Hz" +
                 " | " + estado +
                 (!enFallo && canalModbus ? " | [Modbus]" : ""));
          }

          if (enFallo) {
            InfoError info = obtenerInfoError(faultCode);
            debug("!!! FALLA VARIADOR cod:" + String(faultCode) +
                  " — " + info.nombre + " — " + info.detalle);
          }

          // Primera comunicación exitosa: restaurar frecuencia guardada en EEPROM
          if (!_freqPendienteOK && !enFallo) {
            _freqPendienteOK = true;
            debug(">> Comunicacion OK. Restaurando frecuencia: " + String(_freqPendiente, 1) + "Hz");
            setFrecuencia(_freqPendiente);
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
