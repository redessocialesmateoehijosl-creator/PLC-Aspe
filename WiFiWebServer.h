#ifndef WIFIWEBSERVER_H
#define WIFIWEBSERVER_H

// ============================================================
//  WiFiWebServer.h — Adaptado para M-Duino 21+ (Arduino Mega)
//
//  Cambios respecto a la versión ESP32:
//   • WiFi + WebServer + ESPmDNS + Update → eliminados
//   • Implementación pure Ethernet (W5500 del M-Duino)
//   • HTTP parsing manual con EthernetServer
//   • Sección OTA firmware eliminada (no disponible en Mega)
//   • std::function → puntero de función simple
// ============================================================

#include <Ethernet.h>

extern String webLogBuffer;   // Definido en Motor_Abaran.ino

class WiFiWebServer {
  public:
    typedef void (*CommandHandler)(String);
    CommandHandler onCommandReceived = nullptr;

    void setCommandCallback(CommandHandler handler) {
      onCommandReceived = handler;
    }

    // Añade una línea al buffer de log circular
    /*void log(String text) {
      webLogBuffer = "[" + String(millis() / 1000) + "s] " + text + "\n" + webLogBuffer;
      if (webLogBuffer.length() > 800) webLogBuffer = webLogBuffer.substring(0, 800);
    }*/
    void log(String text) {
        // Añadir al final es mucho más ligero
        webLogBuffer += "[" + String(millis() / 1000) + "s] " + text + "\n";
        
        // Si se pasa de tamaño, cortamos un trozo del principio, pero solo de vez en cuando
        if (webLogBuffer.length() > 700) {
            webLogBuffer = webLogBuffer.substring(200); 
        }
    }

    // --------------------------------------------------------
    //  SETUP — Inicia Ethernet y el servidor HTTP en puerto 80
    // --------------------------------------------------------
    void setup() {
      byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
      Serial.println(F("Iniciando Ethernet W5500..."));

      // Intento DHCP con timeout corto para no bloquear
      if (Ethernet.begin(mac, 4000, 2000) == 0) {
        Serial.println(F("DHCP fallido. Usando IP estatica 192.168.1.100"));
        IPAddress ip(192, 168, 1, 100);
        IPAddress gw(192, 168, 1,   1);
        IPAddress sn(255, 255, 255, 0);
        Ethernet.begin(mac, ip, gw, gw, sn);
      }

      Serial.print(F("Ethernet OK. IP: "));
      Serial.println(Ethernet.localIP());
      server.begin();
      Serial.println(F("Servidor Web en puerto 80."));
    }

    // --------------------------------------------------------
    //  LOOP — Atender clientes HTTP entrantes
    //  Llamar desde loop() del .ino
    // --------------------------------------------------------
    void loop() {
      Ethernet.maintain();              // Renueva el DHCP si hace falta

      EthernetClient client = server.available();
      if (!client) return;

      // Leer cabecera HTTP (hasta doble CRLF)
      String requestLine = "";
      unsigned long timeout = millis() + 500;
      bool lineRead = false;

      while (client.connected() && millis() < timeout) {
        if (client.available()) {
          char c = client.read();
          if (!lineRead) {
            if (c == '\n') { lineRead = true; }
            else if (c != '\r') { requestLine += c; }
          } else {
            // Consumir resto de cabeceras hasta línea en blanco
            // (evita que el buffer del cliente se llene)
          }
        }
        // Pequeña pausa para no hacer busy-wait puro
        if (lineRead && !client.available()) break;
      }

      // Enrutar según la petición
      if (requestLine.indexOf(F("GET /cmd")) >= 0) {
        handleCommand(client, requestLine);
      } else if (requestLine.indexOf(F("GET /logs")) >= 0) {
        sendPlain(client, webLogBuffer);
      } else if (requestLine.indexOf(F("GET /")) >= 0) {
        sendPage(client);
      } else {
        sendPlain(client, F("Bad Request"));
      }

      delay(1);
      client.stop();
    }

  private:
    EthernetServer server = EthernetServer(80);

    // --------------------------------------------------------
    //  Procesa GET /cmd?c=<comando>
    // --------------------------------------------------------
    void handleCommand(EthernetClient& client, const String& req) {
      int ci = req.indexOf(F("?c="));
      if (ci >= 0) {
        int end = req.indexOf(' ', ci);
        String cmd = (end > 0) ? req.substring(ci + 3, end) : req.substring(ci + 3);
        urlDecode(cmd);
        cmd.trim();
        if (cmd.length() > 0 && onCommandReceived) {
          onCommandReceived(cmd);
        }
      }
      sendPlain(client, F("OK"));
    }

    // --------------------------------------------------------
    //  Decodifica %XX de una URL
    // --------------------------------------------------------
    void urlDecode(String& s) {
      s.replace(F("+"), F(" "));
      for (int i = 0; i < (int)s.length() - 2; i++) {
        if (s.charAt(i) == '%') {
          char hex[3] = { s.charAt(i + 1), s.charAt(i + 2), '\0' };
          char decoded = (char)strtol(hex, nullptr, 16);
          s = s.substring(0, i) + decoded + s.substring(i + 3);
        }
      }
    }

    // --------------------------------------------------------
    //  Respuesta HTTP texto plano
    // --------------------------------------------------------
    void sendPlain(EthernetClient& c, const String& body) {
      c.println(F("HTTP/1.1 200 OK"));
      c.println(F("Content-Type: text/plain; charset=utf-8"));
      c.println(F("Connection: close"));
      c.println();
      c.print(body);
    }
    void sendPlain(EthernetClient& c, const __FlashStringHelper* body) {
      c.println(F("HTTP/1.1 200 OK"));
      c.println(F("Content-Type: text/plain; charset=utf-8"));
      c.println(F("Connection: close"));
      c.println();
      c.print(body);
    }

    // --------------------------------------------------------
    //  Página principal HTML
    //  Se sirve en fragmentos con println para no acumular
    //  grandes Strings en RAM (solo 8 KB en Mega)
    // --------------------------------------------------------
    void sendPage(EthernetClient& c) {
      c.println(F("HTTP/1.1 200 OK"));
      c.println(F("Content-Type: text/html; charset=utf-8"));
      c.println(F("Connection: close"));
      c.println();

      // HEAD
      c.println(F("<!DOCTYPE html><html><head>"));
      c.println(F("<meta charset='UTF-8'>"));
      c.println(F("<meta name='viewport' content='width=device-width,initial-scale=1'>"));
      c.println(F("<title>PLC Motor Techo</title>"));
      c.println(F("<style>"));
      c.println(F("body{font-family:sans-serif;background:#f0f2f5;display:flex;flex-direction:column;align-items:center;padding:20px;margin:0;}"));
      c.println(F(".card{background:#fff;padding:1.5rem;border-radius:1rem;box-shadow:0 4px 14px rgba(0,0,0,.1);width:100%;max-width:480px;margin-bottom:18px;text-align:center;}"));
      c.println(F("h1{color:#3182ce;margin:0 0 4px;}p.sub{color:#718096;font-size:.85rem;margin:0 0 1.2rem;}"));
      c.println(F(".row{display:flex;justify-content:center;gap:8px;margin-bottom:14px;}"));
      c.println(F("input[type=text]{flex:1;padding:9px;font-size:1rem;border:2px solid #e2e8f0;border-radius:6px;outline:none;}"));
      c.println(F("button{padding:9px 18px;background:#3182ce;color:#fff;border:none;border-radius:6px;cursor:pointer;font-weight:600;}"));
      c.println(F("button:active{background:#2c5282;}"));
      c.println(F(".btns{display:flex;flex-wrap:wrap;gap:8px;justify-content:center;margin-bottom:14px;}"));
      c.println(F(".btns button{min-width:80px;}"));
      c.println(F("#console{background:#1a202c;color:#48bb78;font-family:monospace;padding:12px;border-radius:6px;height:180px;overflow-y:auto;text-align:left;font-size:.82rem;}"));
      c.println(F("</style></head><body>"));

      // CARD
      c.println(F("<div class='card'>"));
      c.println(F("<h1>MATEO E HIJO</h1>"));
      c.println(F("<p class='sub'>PLC Motor Techo &mdash; M-Duino 21+</p>"));

      // Botones rápidos
      c.println(F("<div class='btns'>"));
      c.println(F("<button onclick=\"cmd('a')\">Abrir</button>"));
      c.println(F("<button onclick=\"cmd('z')\">Cerrar</button>"));
      c.println(F("<button onclick=\"cmd('s')\" style='background:#e53e3e'>STOP</button>"));
      c.println(F("<button onclick=\"cmd('i')\" style='background:#718096'>Estado</button>"));
      c.println(F("</div>"));

      // Campo libre
      c.println(F("<div class='row'>"));
      c.println(F("<input type='text' id='ci' placeholder='m50, A30, c, E...' maxlength='10' onkeydown='if(event.key==\"Enter\")sendCmd()'>"));
      c.println(F("<button onclick='sendCmd()'>Enviar</button>"));
      c.println(F("</div>"));

      // Console de logs
      c.println(F("<div id='console'>Cargando...</div>"));
      c.println(F("</div>"));

      // JS
      c.println(F("<script>"));
      c.println(F("function cmd(v){fetch('/cmd?c='+encodeURIComponent(v));}"));
      c.println(F("function sendCmd(){var v=document.getElementById('ci').value;if(v.length){cmd(v);document.getElementById('ci').value='';}}"));
      c.println(F("setInterval(function(){fetch('/logs').then(r=>r.text()).then(d=>{var el=document.getElementById('console');el.innerHTML=d.replace(/\\n/g,'<br>');el.scrollTop=0;});},1500);"));
      c.println(F("</script></body></html>"));
    }
};

#endif
