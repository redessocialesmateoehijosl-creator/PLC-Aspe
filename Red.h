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

      Serial.print(F("[RED] IP: "));
      Serial.println(Ethernet.localIP());
      delay(500);
    }

    static void loop() {
      _linkJustRecovered = false;

      bool linkOK = (Ethernet.linkStatus() == LinkON);

      if (linkOK && !_lastLinkON) {
        // ── Cable reconectado ──────────────────────────────
        Serial.println(F("[RED] Cable reconectado."));
        if (!_inhibirDHCP) {
          _intentarDHCP("Cable reconectado.");
          _dhcpPendiente = false;
        } else {
          // Motor en movimiento: diferir el DHCP para no bloquear el loop
          Serial.println(F("[RED] Motor en movimiento. DHCP diferido."));
          _dhcpPendiente = true;
        }
        _linkJustRecovered = true;   // MQTT reconectará aunque cambie la IP

      } else if (!linkOK && _lastLinkON) {
        Serial.println(F("[RED] !!! Cable Ethernet desconectado !!!"));
      }

      _lastLinkON = linkOK;

      // ── DHCP diferido: reintentar cuando el motor ya esté parado ──
      if (_dhcpPendiente && !_inhibirDHCP && linkOK) {
        Serial.println(F("[RED] Ejecutando DHCP diferido."));
        _intentarDHCP("DHCP diferido.");
        _dhcpPendiente     = false;
        _linkJustRecovered = true;   // fuerza reconexión MQTT con posible nueva IP
      }

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