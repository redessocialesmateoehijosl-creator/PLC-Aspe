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
// ============================================================

class Red {
  private:
    static bool _lastLinkON;
    static bool _linkJustRecovered;

  public:
    static void setup() {
      byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
      Serial.println(F("[RED] Iniciando Ethernet W5500..."));

      if (Ethernet.begin(mac, 5000) == 0) {
        Serial.println(F("[RED] DHCP fallido. Usando IP estatica 192.168.1.100..."));
        IPAddress ip(192, 168, 1, 100);
        IPAddress dns(8, 8, 8, 8);
        IPAddress gw(192, 168, 1, 1);
        IPAddress sn(255, 255, 255, 0);
        Ethernet.begin(mac, ip, dns, gw, sn);
      }

      _lastLinkON       = (Ethernet.linkStatus() == LinkON);
      _linkJustRecovered = false;

      Serial.print(F("[RED] IP: "));
      Serial.println(Ethernet.localIP());
      delay(500);
    }

    static void loop() {
      _linkJustRecovered = false;

      bool linkOK = (Ethernet.linkStatus() == LinkON);

      if (linkOK && !_lastLinkON) {
        // Cable reconectado — renovar DHCP
        Serial.println(F("[RED] Cable reconectado. Intentando DHCP..."));
        byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
        if (Ethernet.begin(mac, 3000) != 0) {
          Serial.print(F("[RED] DHCP OK. Nueva IP: "));
          Serial.println(Ethernet.localIP());
        } else {
          Serial.println(F("[RED] DHCP fallido. Manteniendo IP anterior."));
        }
        _linkJustRecovered = true;
      } else if (!linkOK && _lastLinkON) {
        Serial.println(F("[RED] !!! Cable Ethernet desconectado !!!"));
      }

      _lastLinkON = linkOK;

      // Mantener concesión DHCP y loguear cambios
      int r = Ethernet.maintain();
      if      (r == 2) { Serial.print(F("[RED] DHCP renovado. IP: ")); Serial.println(Ethernet.localIP()); }
      else if (r == 3) { Serial.println(F("[RED] Fallo en renovacion DHCP (rebind fallido).")); }
      else if (r == 4) { Serial.println(F("[RED] DHCP renew fallido.")); }
    }

    // Mqtt.h llama esto para saber si debe reconectar más rápido
    static bool linkJustRecovered() { return _linkJustRecovered; }
    static bool linkOK()            { return (Ethernet.linkStatus() == LinkON); }
};

// Definición de los static — guardada con inline para evitar símbolo duplicado
// si Red.h se incluye desde varios .h (requiere C++17; en AVR/Mega usamos
// la definición condicional clásica).
#ifndef RED_STATICS_DEFINED
#define RED_STATICS_DEFINED
bool Red::_lastLinkON        = false;
bool Red::_linkJustRecovered = false;
#endif

#endif