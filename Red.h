#ifndef RED_H
#define RED_H

#include <Ethernet.h>

// ============================================================
//  Red.h — Gestión Ethernet W5500 para M-Duino 21+
//
//  Mejoras respecto a versión anterior:
//   • Detección de desconexión/reconexión de cable
//   • Log de renovaciones DHCP (Ethernet.maintain)
//   • linkJustRecovered() para que Mqtt.h reconecte más rápido
//   • inhibirDHCP(): cuando el motor está en movimiento se
//     difieren Ethernet.begin() y Ethernet.maintain() para
//     que NUNCA bloqueen el loop durante un movimiento.
//     El .ino llama Red::inhibirDHCP(grupo.estaMoviendo())
//     ANTES de Red::loop() en cada iteración.
// ============================================================

class Red {
  private:
    static bool _lastLinkON;
    static bool _linkJustRecovered;
    static bool _inhibirDHCP;      // true = motor en movimiento, no bloquear
    static bool _dhcpPendiente;    // DHCP diferido: pendiente de hacer cuando pare

    static const byte _mac[6];

    static void _intentarDHCP(const char* motivo) {
      Serial.print(F("[RED] "));
      Serial.print(motivo);
      Serial.println(F(" Intentando DHCP (max 1s)..."));
      // Timeout reducido a 1000 ms (antes 3000) para limitar el bloqueo
      // al mínimo imprescindible. Si falla, se mantiene la IP anterior.
      if (Ethernet.begin(const_cast<byte*>(_mac), 1000) != 0) {
        Serial.print(F("[RED] DHCP OK. IP: "));
        Serial.println(Ethernet.localIP());
      } else {
        Serial.println(F("[RED] DHCP fallido. Manteniendo IP anterior."));
      }
    }

  public:

    // --------------------------------------------------------
    //  Llamar ANTES de Red::loop() desde el .ino:
    //    Red::inhibirDHCP(grupo.estaMoviendo());
    //  Mientras sea true, ninguna operación de red bloquea.
    // --------------------------------------------------------
    static void inhibirDHCP(bool v) { _inhibirDHCP = v; }

    static void setup() {
      Serial.println(F("[RED] Iniciando Ethernet W5500..."));

      // En setup() el motor siempre está parado; timeout generoso está bien.
      if (Ethernet.begin(const_cast<byte*>(_mac), 5000) == 0) {
        Serial.println(F("[RED] DHCP fallido. Usando IP estatica 192.168.1.100..."));
        IPAddress ip(192, 168, 1, 100);
        IPAddress dns(8, 8, 8, 8);
        IPAddress gw(192, 168, 1, 1);
        IPAddress sn(255, 255, 255, 0);
        Ethernet.begin(const_cast<byte*>(_mac), ip, dns, gw, sn);
      }

      _lastLinkON        = (Ethernet.linkStatus() == LinkON);
      _linkJustRecovered = false;
      _dhcpPendiente     = false;

      // ── Limitar timeout TCP del W5500 ────────────────────────────────────
      // Por defecto el W5500 usa RTR=200ms × RCR=8 retries, lo que hace que
      // EthernetClient::write() y ::stop() bloqueen el loop hasta 30 segundos
      // cuando el cable se desconecta (intentan enviar TCP y esperan ACKs).
      // Con RTR=100ms × RCR=3, el peor caso de bloqueo es ~300ms,
      // suficiente para red local y seguro para el loop de control.
      Ethernet.setRetransmissionTimeout(100);  // 100 ms por reintento
      Ethernet.setRetransmissionCount(3);       // máximo 3 reintentos

      Serial.print(F("[RED] IP: "));
      Serial.println(Ethernet.localIP());
      delay(500);
    }

    /*static void loop() {
      _linkJustRecovered = false;

      bool linkOK = (Ethernet.linkStatus() == LinkON);

      if (linkOK && !_lastLinkON) {
        // ── Cable reconectado ──────────────────────────────
        Serial.println(F("[RED] Cable reconectado."));
        // NO relanzar Ethernet.begin()/DHCP aquí: en varios builds de la
        // librería Ethernet, begin() ignora el timeout y hace múltiples
        // intentos DISCOVER internos, bloqueando el loop 4-5 segundos.
        // En redes normales el router asigna la misma IP (lease DHCP activo).
        // Si la IP cambió, el intento de reconexión MQTT fallará con estado
        // -2 y lo reintentará; Ethernet.maintain() se encargará de renovar
        // el lease en segundo plano sin bloquear (llamado en cada ciclo loop).
        _dhcpPendiente = false;     // Cancelar cualquier DHCP pendiente previo
        _linkJustRecovered = true;  // MQTT reconectará inmediatamente

      } else if (!linkOK && _lastLinkON) {
        Serial.println(F("[RED] !!! Cable Ethernet desconectado !!!"));
      }

      _lastLinkON = linkOK;
      */
    static void loop() {

      _linkJustRecovered = false;

      bool linkOK = (Ethernet.linkStatus() == LinkON);

      if (linkOK && !_lastLinkON) {
        // ── Cable reconectado ──────────────────────────────
        Serial.println(F("[RED] Cable reconectado. Refrescando IP..."));
        
        // Forzamos un inicio rápido de Ethernet para refrescar Gateway y tablas ARP.
        // Usamos un timeout de 1000ms para no bloquear mucho el loop.
        if (Ethernet.begin(const_cast<byte*>(_mac), 1000) != 0) {
          Serial.print(F("[RED] IP Refrescada: "));
          Serial.println(Ethernet.localIP());
        } else {
          Serial.println(F("[RED] Error al refrescar DHCP. Usando previa."));
        }

        _dhcpPendiente = false;
        _linkJustRecovered = true;  // Esto dispara la reconexión inmediata en Mqtt.h

      } else if (!linkOK && _lastLinkON) {
        Serial.println(F("[RED] !!! Cable Ethernet desconectado !!!"));
      }

      _lastLinkON = linkOK;

      // ── DHCP diferido: desactivado ────────────────────────────────
      // Ya no se relanza DHCP en reconexión de cable (ver comentario arriba).
      // _dhcpPendiente siempre estará en false; bloque conservado por si se
      // reactiva en el futuro.
      // if (_dhcpPendiente && !_inhibirDHCP && linkOK) { ... }

      // ── Mantener concesión DHCP ───────────────────────────────────
      // Ethernet.maintain() puede bloquear unos ms al renovar el lease.
      // Lo saltamos si el motor está en movimiento; el lease se renovará
      // en el siguiente ciclo cuando pare.
      if (!_inhibirDHCP) {
        int r = Ethernet.maintain();
        if      (r == 2) { Serial.print(F("[RED] DHCP renovado. IP: ")); Serial.println(Ethernet.localIP()); }
        else if (r == 3) { Serial.println(F("[RED] Fallo en renovacion DHCP (rebind fallido).")); }
        else if (r == 4) { Serial.println(F("[RED] DHCP renew fallido.")); }
      }
    }

    // Mqtt.h llama esto para saber si debe reconectar más rápido
    static bool linkJustRecovered() { return _linkJustRecovered; }
    static bool linkOK()            { return (Ethernet.linkStatus() == LinkON); }
};

// ── Definición de los miembros static ────────────────────────────────────────
#ifndef RED_STATICS_DEFINED
#define RED_STATICS_DEFINED
bool        Red::_lastLinkON        = false;
bool        Red::_linkJustRecovered = false;
bool        Red::_inhibirDHCP       = false;
bool        Red::_dhcpPendiente     = false;
const byte  Red::_mac[6]            = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
#endif

#endif