/*#ifndef WIFIWEBSERVER_H
#define WIFIWEBSERVER_H

#include <SPI.h>
#include <Ethernet.h> // Para la conexión a Internet por Cable
#include <WiFi.h>     // Para el Servidor Web por WiFi
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>

// El buffer de logs se define como extern para que sea accesible
extern String webLogBuffer; 

class WiFiWebServer {
public:
    // Constructor: Recibe los datos del WiFi y el nombre de red (host)
    WiFiWebServer(const char* ssid, const char* password, const char* host) 
        : _ssid(ssid), _password(password), _host(host), server(80) {}

    typedef std::function<void(String)> CommandHandler;
    CommandHandler onCommandReceived;

    void setCommandCallback(CommandHandler handler) {
        onCommandReceived = handler;
    }

    // Método para añadir líneas al log de la web
    void log(String text) {
        String timePrefix = "[" + String(millis()/1000) + "s] ";
        webLogBuffer = timePrefix + text + "\n" + webLogBuffer; 
        if (webLogBuffer.length() > 2000) webLogBuffer = webLogBuffer.substring(0, 2000);
    }

    void setup() {
        // 1. FORZAR PIN CS PARA INDUSTRIAL SHIELDS
          //Ethernet.init(5); 

          byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
          Serial.println("Iniciando Ethernet W5500...");

          // 2. USAR TIMEOUT PARA NO BLOQUEAR LOS BOTONES
          // Intentamos conectar. Si en 2 segundos no hay respuesta, saltamos.
          if (Ethernet.begin(mac, 2000, 1000) == 0) {
              Serial.println("Fallo Ethernet (Cable no detectado o sin DHCP)");
          } else {
              Serial.print("Conectado por Cable. IP: ");
              Serial.println(Ethernet.localIP());
          }

        // 2. INICIAR WIFI (Para acceso inalámbrico al Servidor Web)
        Serial.println("--- INICIANDO SERVIDOR WIFI ---");
        WiFi.begin(_ssid, _password);
        
        unsigned long startWiFi = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startWiFi < 10000) {
            delay(500);
            Serial.print(".");
        }

        if(WiFi.status() == WL_CONNECTED) {
            Serial.print("\nWiFi Conectado. IP para entrar a la WEB: ");
            Serial.println(WiFi.localIP());
        } else {
            Serial.println("\nNo se pudo conectar al WiFi. El servidor no estará disponible sin cables.");
        }

        // Configurar mDNS (permite entrar por http://host.local)
        if (!MDNS.begin(_host)) {
            Serial.println("Error configurando mDNS");
        }

        // Definición de Rutas del Servidor
        server.on("/", HTTP_GET, std::bind(&WiFiWebServer::handleRoot, this));
        server.on("/serverIndex", HTTP_GET, std::bind(&WiFiWebServer::handleServerIndex, this));
        server.on("/login", HTTP_POST, std::bind(&WiFiWebServer::handleLogin, this));
        server.on("/update", HTTP_POST, std::bind(&WiFiWebServer::handleUpdateEnd, this), std::bind(&WiFiWebServer::handleDoUpdate, this));
        server.on("/cmd", HTTP_GET, std::bind(&WiFiWebServer::handleCommand, this));
        server.on("/logs", HTTP_GET, std::bind(&WiFiWebServer::handleGetLogs, this));

        server.begin();
        Serial.println("Servidor Web iniciado correctamente.");
    }

    void loop() {
        server.handleClient(); // Atiende peticiones WiFi
        Ethernet.maintain();   // Mantiene la IP del cable
    }

private:
    const char* _ssid;
    const char* _password;
    const char* _host;
    WebServer server;

    // --- ESTILOS VISUALES ---
    String getStyle() {
        return F(
            "<style>"
            "@import url('https://fonts.googleapis.com/css2?family=Poppins:wght@300;400;500;700&display=swap');"
            "body { background: #f0f2f5; font-family: 'Poppins', sans-serif; display: flex; flex-direction: column; align-items: center; min-height: 100vh; margin: 0; color: #4a5568; padding: 20px; }"
            ".container { width: 100%; max-width: 500px; }"
            ".card { background: white; padding: 2rem; border-radius: 1rem; box-shadow: 0 10px 25px -5px rgba(0,0,0,0.1); margin-bottom: 20px; text-align: center; }"
            ".company-name { font-size: 1.8rem; font-weight: 700; margin-bottom: 0; background: -webkit-linear-gradient(45deg, #3182ce, #2c5282); -webkit-background-clip: text; -webkit-text-fill-color: transparent; }"
            ".subtitle { font-size: 0.9rem; color: #718096; margin-bottom: 1.5rem; font-weight: 500; text-transform: uppercase; letter-spacing: 1px; }"
            "h2 { font-size: 1.2rem; font-weight: 600; margin-bottom: 1rem; color: #2d3748; border-bottom: 2px solid #edf2f7; padding-bottom: 5px; display: inline-block; }"
            ".cmd-group { display: flex; gap: 10px; margin-bottom: 15px; }"
            "#cmdInput { flex: 1; padding: 12px; font-size: 1.1rem; text-align: center; border: 2px solid #e2e8f0; border-radius: 8px; outline: none; transition: border-color 0.2s; }" 
            "#cmdInput:focus { border-color: #4299e1; }"
            ".btn-send { background: linear-gradient(135deg, #4299e1 0%, #3182ce 100%); color: white; border: none; border-radius: 8px; padding: 0 20px; font-weight: 600; cursor: pointer; transition: transform 0.1s; }"
            ".btn-send:active { transform: scale(0.95); }"
            "#console { background: #1a202c; color: #48bb78; font-family: 'Courier New', monospace; padding: 15px; border-radius: 8px; text-align: left; height: 200px; overflow-y: auto; font-size: 0.85rem; margin-top: 10px; border: 1px solid #2d3748; box-shadow: inset 0 2px 4px rgba(0,0,0,0.3); }"
            "input { width: 100%; padding: 0.8rem; margin-bottom: 1rem; border: 1px solid #e2e8f0; border-radius: 0.5rem; box-sizing: border-box; background: #f7fafc; }"
            ".btn-login { width: 100%; background: linear-gradient(135deg, #4299e1 0%, #3182ce 100%); color: white; padding: 0.8rem; border: none; border-radius: 0.5rem; font-weight: 600; cursor: pointer; }"
            ".file-upload-label { display: block; padding: 1rem; color: #4a5568; background: #edf2f7; border-radius: 0.5rem; cursor: pointer; border: 2px dashed #cbd5e0; margin-bottom: 10px; }"
            "#prgbar { width: 100%; background-color: #edf2f7; border-radius: 10px; height: 10px; overflow: hidden; }"
            "#bar { width: 0%; height: 100%; background: #48bb78; transition: width 0.3s; }"
            "</style>"
        );
    }

    // --- MANEJADORES DE PETICIONES (Páginas) ---
    void handleRoot() {
        String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>" + getStyle() + "</head><body><div class='container'><div class='card'>";
        html += "<div class='company-name'>MATEO E HIJO</div><div class='subtitle'>PLC de ABARAN</div>";
        html += "<h2>Carga de Código</h2>";
        html += "<form name='loginForm'>";
        html += "<input type='text' name='userid' placeholder='Usuario' autocomplete='off'>";
        html += "<input type='password' name='pwd' placeholder='Contraseña'>";
        html += "<input type='button' onclick='check()' class='btn-login' value='Entrar'>";
        html += "</form></div></div>";
        html += "<script>function check() { var u = document.getElementsByName('userid')[0].value; var p = document.getElementsByName('pwd')[0].value; fetch('/login', { method: 'POST', headers: {'Content-Type': 'application/x-www-form-urlencoded'}, body: 'userid='+encodeURIComponent(u)+'&pwd='+encodeURIComponent(p) }).then(r => { if(r.ok) window.location.href='/serverIndex'; else alert('Acceso Denegado'); }); }</script></body></html>";
        server.send(200, "text/html", html);
    }

    void handleServerIndex() {
        String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>" + getStyle() + "<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script></head><body><div class='container'>";
        html += "<div class='card'>";
        html += "<div class='company-name'>MATEO E HIJO</div><div class='subtitle'>PLC de Abaran</div>";
        html += "<h2>Enviar Comando</h2>";
        html += "<div class='cmd-group'>";
        html += "<input type='text' id='cmdInput' placeholder='Ej: a, z, m20...' maxlength='10' onkeydown='if(event.key===\"Enter\") sendCmd()'>";
        html += "<button class='btn-send' onclick='sendCmd()'>Enviar</button>";
        html += "</div>";
        html += "<h2>Registro (Logs)</h2>";
        html += "<div id='console'>Conectando con PLC...</div>";
        html += "</div>";
        html += "<div class='card'>";
        html += "<h2>Actualizar Firmware</h2>";
        html += "<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>";
        html += "<input type='file' name='update' id='file' onchange='sub(this)' style='display:none;'>";
        html += "<label for='file' class='file-upload-label' id='file-label'>📂 Toca para elegir archivo .bin</label>";
        html += "<input type='submit' class='btn-login' value='Instalar Actualización'>";
        html += "<div id='prgbar' style='margin-top:10px;'><div id='bar'></div></div>";
        html += "<div id='prg'></div>";
        html += "</form></div>";
        html += "</div>"; 
        html += "<script>";
        html += "function sendCmd() { var input = document.getElementById('cmdInput'); var val = input.value; if(val.length > 0) { fetch('/cmd?c=' + val); input.value = ''; } }";
        html += "setInterval(function() { fetch('/logs').then(r => r.text()).then(data => { document.getElementById('console').innerHTML = data.replace(/\\n/g, '<br>'); }); }, 1000);";
        html += "function sub(obj){ var fileName = obj.value.split('\\\\'); document.getElementById('file-label').innerHTML = '📄 ' + fileName[fileName.length-1]; };";
        html += "$('form').submit(function(e){ e.preventDefault(); var form = $('#upload_form')[0]; var data = new FormData(form); $.ajax({ url: '/update', type: 'POST', data: data, contentType: false, processData:false, xhr: function() { var xhr = new window.XMLHttpRequest(); xhr.upload.addEventListener('progress', function(evt) { if (evt.lengthComputable) { var per = evt.loaded / evt.total; $('#prg').html(Math.round(per*100) + '%'); $('#bar').css('width',Math.round(per*100) + '%'); } }, false); return xhr; }, success:function(d, s) { console.log('success!') }, error: function (a, b, c) { } }); });";
        html += "</script></body></html>";
        server.send(200, "text/html", html);
    }

    void handleLogin() {
        if (server.hasArg("userid") && server.hasArg("pwd")) {
            if (server.arg("userid") == "admin" && server.arg("pwd") == "admin") {
                server.sendHeader("Location", "/serverIndex");
                server.send(303);
            } else { server.send(401, "text/plain", "Acceso Denegado"); }
        } else { server.send(400, "text/plain", "Petición Incorrecta"); }
    }

    void handleCommand() {
        if (server.hasArg("c")) {
            String cmd = server.arg("c");
            if (cmd.length() > 0 && onCommandReceived) {
                onCommandReceived(cmd);
            }
        }
        server.send(200, "text/plain", "OK");
    }

    void handleGetLogs() {
        server.send(200, "text/plain", webLogBuffer);
    }

    void handleUpdateEnd() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/plain", (Update.hasError()) ? "FALLO" : "OK");
        ESP.restart();
    }

    void handleDoUpdate() {
        HTTPUpload& upload = server.upload();
        if (upload.status == UPLOAD_FILE_START) {
            log("Iniciando actualización: " + upload.filename);
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) Update.printError(Serial);
        } else if (upload.status == UPLOAD_FILE_END) {
            if (Update.end(true)) log("Firmware actualizado: " + String(upload.totalSize) + " bytes");
            else Update.printError(Serial);
        }
    }
};

#endif
*/













#ifndef WIFIWEBSERVER_H
#define WIFIWEBSERVER_H

#include <WiFi.h>     // Maneja toda la conexión de red (Adiós Ethernet)
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>

// El buffer de logs se define como extern para que sea accesible
extern String webLogBuffer; 

class WiFiWebServer {
public:
    // Constructor: Recibe los datos del WiFi y el nombre de red (host)
    WiFiWebServer(const char* ssid, const char* password, const char* host) 
        : _ssid(ssid), _password(password), _host(host), server(80) {}

    typedef std::function<void(String)> CommandHandler;
    CommandHandler onCommandReceived;

    void setCommandCallback(CommandHandler handler) {
        onCommandReceived = handler;
    }

    // Método para añadir líneas al log de la web
    void log(String text) {
        String timePrefix = "[" + String(millis()/1000) + "s] ";
        webLogBuffer = timePrefix + text + "\n" + webLogBuffer; 
        if (webLogBuffer.length() > 2000) webLogBuffer = webLogBuffer.substring(0, 2000);
    }

    void setup() {
        Serial.println("--- INICIANDO CONEXIÓN WIFI ---");
        
        // Iniciamos la conexión WiFi principal
        WiFi.begin(_ssid, _password);
        
        unsigned long startWiFi = millis();
        // Esperamos hasta 10 segundos para conectar
        while (WiFi.status() != WL_CONNECTED && millis() - startWiFi < 10000) {
            delay(500);
            Serial.print(".");
        }

        if(WiFi.status() == WL_CONNECTED) {
            Serial.print("\nWiFi Conectado exitosamente.\nIP del PLC para entrar a la WEB: ");
            Serial.println(WiFi.localIP());
        } else {
            Serial.println("\n[ERROR] No se pudo conectar al WiFi. El PLC está desconectado de la red.");
            // Aquí podrías agregar código para que el ESP32 cree su propia red (Modo AP) si falla el router
        }

        // Configurar mDNS (permite entrar por http://host.local)
        if (!MDNS.begin(_host)) {
            Serial.println("Error configurando mDNS");
        }

        // Definición de Rutas del Servidor
        server.on("/", HTTP_GET, std::bind(&WiFiWebServer::handleRoot, this));
        server.on("/serverIndex", HTTP_GET, std::bind(&WiFiWebServer::handleServerIndex, this));
        server.on("/login", HTTP_POST, std::bind(&WiFiWebServer::handleLogin, this));
        server.on("/update", HTTP_POST, std::bind(&WiFiWebServer::handleUpdateEnd, this), std::bind(&WiFiWebServer::handleDoUpdate, this));
        server.on("/cmd", HTTP_GET, std::bind(&WiFiWebServer::handleCommand, this));
        server.on("/logs", HTTP_GET, std::bind(&WiFiWebServer::handleGetLogs, this));

        server.begin();
        Serial.println("Servidor Web iniciado correctamente.");
    }

    void loop() {
        server.handleClient(); // Atiende peticiones web inalámbricas
    }

private:
    const char* _ssid;
    const char* _password;
    const char* _host;
    WebServer server;

    // --- ESTILOS VISUALES ---
    String getStyle() {
        return F(
            "<style>"
            "@import url('https://fonts.googleapis.com/css2?family=Poppins:wght@300;400;500;700&display=swap');"
            "body { background: #f0f2f5; font-family: 'Poppins', sans-serif; display: flex; flex-direction: column; align-items: center; min-height: 100vh; margin: 0; color: #4a5568; padding: 20px; }"
            ".container { width: 100%; max-width: 500px; }"
            ".card { background: white; padding: 2rem; border-radius: 1rem; box-shadow: 0 10px 25px -5px rgba(0,0,0,0.1); margin-bottom: 20px; text-align: center; }"
            ".company-name { font-size: 1.8rem; font-weight: 700; margin-bottom: 0; background: -webkit-linear-gradient(45deg, #3182ce, #2c5282); -webkit-background-clip: text; -webkit-text-fill-color: transparent; }"
            ".subtitle { font-size: 0.9rem; color: #718096; margin-bottom: 1.5rem; font-weight: 500; text-transform: uppercase; letter-spacing: 1px; }"
            "h2 { font-size: 1.2rem; font-weight: 600; margin-bottom: 1rem; color: #2d3748; border-bottom: 2px solid #edf2f7; padding-bottom: 5px; display: inline-block; }"
            ".cmd-group { display: flex; gap: 10px; margin-bottom: 15px; }"
            "#cmdInput { flex: 1; padding: 12px; font-size: 1.1rem; text-align: center; border: 2px solid #e2e8f0; border-radius: 8px; outline: none; transition: border-color 0.2s; }" 
            "#cmdInput:focus { border-color: #4299e1; }"
            ".btn-send { background: linear-gradient(135deg, #4299e1 0%, #3182ce 100%); color: white; border: none; border-radius: 8px; padding: 0 20px; font-weight: 600; cursor: pointer; transition: transform 0.1s; }"
            ".btn-send:active { transform: scale(0.95); }"
            "#console { background: #1a202c; color: #48bb78; font-family: 'Courier New', monospace; padding: 15px; border-radius: 8px; text-align: left; height: 200px; overflow-y: auto; font-size: 0.85rem; margin-top: 10px; border: 1px solid #2d3748; box-shadow: inset 0 2px 4px rgba(0,0,0,0.3); }"
            "input { width: 100%; padding: 0.8rem; margin-bottom: 1rem; border: 1px solid #e2e8f0; border-radius: 0.5rem; box-sizing: border-box; background: #f7fafc; }"
            ".btn-login { width: 100%; background: linear-gradient(135deg, #4299e1 0%, #3182ce 100%); color: white; padding: 0.8rem; border: none; border-radius: 0.5rem; font-weight: 600; cursor: pointer; }"
            ".file-upload-label { display: block; padding: 1rem; color: #4a5568; background: #edf2f7; border-radius: 0.5rem; cursor: pointer; border: 2px dashed #cbd5e0; margin-bottom: 10px; }"
            "#prgbar { width: 100%; background-color: #edf2f7; border-radius: 10px; height: 10px; overflow: hidden; }"
            "#bar { width: 0%; height: 100%; background: #48bb78; transition: width 0.3s; }"
            "</style>"
        );
    }

    // --- MANEJADORES DE PETICIONES (Páginas) ---
    void handleRoot() {
        String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>" + getStyle() + "</head><body><div class='container'><div class='card'>";
        html += "<div class='company-name'>MATEO E HIJO</div><div class='subtitle'>PLC de ABARAN</div>";
        html += "<h2>Carga de Código</h2>";
        html += "<form name='loginForm'>";
        html += "<input type='text' name='userid' placeholder='Usuario' autocomplete='off'>";
        html += "<input type='password' name='pwd' placeholder='Contraseña'>";
        html += "<input type='button' onclick='check()' class='btn-login' value='Entrar'>";
        html += "</form></div></div>";
        html += "<script>function check() { var u = document.getElementsByName('userid')[0].value; var p = document.getElementsByName('pwd')[0].value; fetch('/login', { method: 'POST', headers: {'Content-Type': 'application/x-www-form-urlencoded'}, body: 'userid='+encodeURIComponent(u)+'&pwd='+encodeURIComponent(p) }).then(r => { if(r.ok) window.location.href='/serverIndex'; else alert('Acceso Denegado'); }); }</script></body></html>";
        server.send(200, "text/html", html);
    }

    void handleServerIndex() {
        String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>" + getStyle() + "<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script></head><body><div class='container'>";
        html += "<div class='card'>";
        html += "<div class='company-name'>MATEO E HIJO</div><div class='subtitle'>PLC de Abaran</div>";
        html += "<h2>Enviar Comando</h2>";
        html += "<div class='cmd-group'>";
        html += "<input type='text' id='cmdInput' placeholder='Ej: a, z, m20...' maxlength='10' onkeydown='if(event.key===\"Enter\") sendCmd()'>";
        html += "<button class='btn-send' onclick='sendCmd()'>Enviar</button>";
        html += "</div>";
        html += "<h2>Registro (Logs)</h2>";
        html += "<div id='console'>Conectando con PLC...</div>";
        html += "</div>";
        html += "<div class='card'>";
        html += "<h2>Actualizar Firmware</h2>";
        html += "<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>";
        html += "<input type='file' name='update' id='file' onchange='sub(this)' style='display:none;'>";
        html += "<label for='file' class='file-upload-label' id='file-label'>📂 Toca para elegir archivo .bin</label>";
        html += "<input type='submit' class='btn-login' value='Instalar Actualización'>";
        html += "<div id='prgbar' style='margin-top:10px;'><div id='bar'></div></div>";
        html += "<div id='prg'></div>";
        html += "</form></div>";
        html += "</div>"; 
        html += "<script>";
        html += "function sendCmd() { var input = document.getElementById('cmdInput'); var val = input.value; if(val.length > 0) { fetch('/cmd?c=' + val); input.value = ''; } }";
        html += "setInterval(function() { fetch('/logs').then(r => r.text()).then(data => { document.getElementById('console').innerHTML = data.replace(/\\n/g, '<br>'); }); }, 1000);";
        html += "function sub(obj){ var fileName = obj.value.split('\\\\'); document.getElementById('file-label').innerHTML = '📄 ' + fileName[fileName.length-1]; };";
        html += "$('form').submit(function(e){ e.preventDefault(); var form = $('#upload_form')[0]; var data = new FormData(form); $.ajax({ url: '/update', type: 'POST', data: data, contentType: false, processData:false, xhr: function() { var xhr = new window.XMLHttpRequest(); xhr.upload.addEventListener('progress', function(evt) { if (evt.lengthComputable) { var per = evt.loaded / evt.total; $('#prg').html(Math.round(per*100) + '%'); $('#bar').css('width',Math.round(per*100) + '%'); } }, false); return xhr; }, success:function(d, s) { console.log('success!') }, error: function (a, b, c) { } }); });";
        html += "</script></body></html>";
        server.send(200, "text/html", html);
    }

    void handleLogin() {
        if (server.hasArg("userid") && server.hasArg("pwd")) {
            if (server.arg("userid") == "admin" && server.arg("pwd") == "admin") {
                server.sendHeader("Location", "/serverIndex");
                server.send(303);
            } else { server.send(401, "text/plain", "Acceso Denegado"); }
        } else { server.send(400, "text/plain", "Petición Incorrecta"); }
    }

    void handleCommand() {
        if (server.hasArg("c")) {
            String cmd = server.arg("c");
            if (cmd.length() > 0 && onCommandReceived) {
                onCommandReceived(cmd);
            }
        }
        server.send(200, "text/plain", "OK");
    }

    void handleGetLogs() {
        server.send(200, "text/plain", webLogBuffer);
    }

    void handleUpdateEnd() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/plain", (Update.hasError()) ? "FALLO" : "OK");
        ESP.restart();
    }

    void handleDoUpdate() {
        HTTPUpload& upload = server.upload();
        if (upload.status == UPLOAD_FILE_START) {
            log("Iniciando actualización: " + upload.filename);
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) Update.printError(Serial);
        } else if (upload.status == UPLOAD_FILE_END) {
            if (Update.end(true)) log("Firmware actualizado: " + String(upload.totalSize) + " bytes");
            else Update.printError(Serial);
        }
    }
};

#endif







/*
#ifndef WIFIWEBSERVER_H
#define WIFIWEBSERVER_H

#include <WiFi.h>     // Maneja toda la conexión de red (Adiós Ethernet)
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>

// El buffer de logs se define como extern para que sea accesible
extern String webLogBuffer; 

class WiFiWebServer {
public:
    // Constructor: Recibe los datos del WiFi y el nombre de red (host)
    WiFiWebServer(const char* ssid, const char* password, const char* host) 
        : _ssid(ssid), _password(password), _host(host), server(80) {}

    typedef std::function<void(String)> CommandHandler;
    CommandHandler onCommandReceived;

    void setCommandCallback(CommandHandler handler) {
        onCommandReceived = handler;
    }

    // Método para añadir líneas al log de la web
    void log(String text) {
        String timePrefix = "[" + String(millis()/1000) + "s] ";
        webLogBuffer = timePrefix + text + "\n" + webLogBuffer; 
        if (webLogBuffer.length() > 2000) webLogBuffer = webLogBuffer.substring(0, 2000);
    }

    void setup() {
        Serial.println("--- INICIANDO CONEXIÓN WIFI ---");
        
        // Iniciamos la conexión WiFi principal
        WiFi.begin(_ssid, _password);
        
        unsigned long startWiFi = millis();
        // Esperamos hasta 10 segundos para conectar
        while (WiFi.status() != WL_CONNECTED && millis() - startWiFi < 10000) {
            delay(500);
            Serial.print(".");
        }

        if(WiFi.status() == WL_CONNECTED) {
            Serial.print("\nWiFi Conectado exitosamente.\nIP del PLC para entrar a la WEB: ");
            Serial.println(WiFi.localIP());
        } else {
            Serial.println("\n[ERROR] No se pudo conectar al WiFi. El PLC está desconectado de la red.");
            // Aquí podrías agregar código para que el ESP32 cree su propia red (Modo AP) si falla el router
        }

        // Configurar mDNS (permite entrar por http://host.local)
        if (!MDNS.begin(_host)) {
            Serial.println("Error configurando mDNS");
        }

        // Definición de Rutas del Servidor
        server.on("/", HTTP_GET, std::bind(&WiFiWebServer::handleRoot, this));
        server.on("/serverIndex", HTTP_GET, std::bind(&WiFiWebServer::handleServerIndex, this));
        server.on("/login", HTTP_POST, std::bind(&WiFiWebServer::handleLogin, this));
        server.on("/update", HTTP_POST, std::bind(&WiFiWebServer::handleUpdateEnd, this), std::bind(&WiFiWebServer::handleDoUpdate, this));
        server.on("/cmd", HTTP_GET, std::bind(&WiFiWebServer::handleCommand, this));
        server.on("/logs", HTTP_GET, std::bind(&WiFiWebServer::handleGetLogs, this));

        server.begin();
        Serial.println("Servidor Web iniciado correctamente.");
    }

    void loop() {
        server.handleClient(); // Atiende peticiones web inalámbricas
    }

private:
    const char* _ssid;
    const char* _password;
    const char* _host;
    WebServer server;

    // --- ESTILOS VISUALES ---
    String getStyle() {
        return F(
            "<style>"
            "@import url('https://fonts.googleapis.com/css2?family=Poppins:wght@300;400;500;700&display=swap');"
            "body { background: #f0f2f5; font-family: 'Poppins', sans-serif; display: flex; flex-direction: column; align-items: center; min-height: 100vh; margin: 0; color: #4a5568; padding: 20px; }"
            ".container { width: 100%; max-width: 500px; }"
            ".card { background: white; padding: 2rem; border-radius: 1rem; box-shadow: 0 10px 25px -5px rgba(0,0,0,0.1); margin-bottom: 20px; text-align: center; }"
            ".company-name { font-size: 1.8rem; font-weight: 700; margin-bottom: 0; background: -webkit-linear-gradient(45deg, #3182ce, #2c5282); -webkit-background-clip: text; -webkit-text-fill-color: transparent; }"
            ".subtitle { font-size: 0.9rem; color: #718096; margin-bottom: 1.5rem; font-weight: 500; text-transform: uppercase; letter-spacing: 1px; }"
            "h2 { font-size: 1.2rem; font-weight: 600; margin-bottom: 1rem; color: #2d3748; border-bottom: 2px solid #edf2f7; padding-bottom: 5px; display: inline-block; }"
            ".cmd-group { display: flex; gap: 10px; margin-bottom: 15px; }"
            "#cmdInput { flex: 1; padding: 12px; font-size: 1.1rem; text-align: center; border: 2px solid #e2e8f0; border-radius: 8px; outline: none; transition: border-color 0.2s; }" 
            "#cmdInput:focus { border-color: #4299e1; }"
            ".btn-send { background: linear-gradient(135deg, #4299e1 0%, #3182ce 100%); color: white; border: none; border-radius: 8px; padding: 0 20px; font-weight: 600; cursor: pointer; transition: transform 0.1s; }"
            ".btn-send:active { transform: scale(0.95); }"
            "#console { background: #1a202c; color: #48bb78; font-family: 'Courier New', monospace; padding: 15px; border-radius: 8px; text-align: left; height: 200px; overflow-y: auto; font-size: 0.85rem; margin-top: 10px; border: 1px solid #2d3748; box-shadow: inset 0 2px 4px rgba(0,0,0,0.3); }"
            "input { width: 100%; padding: 0.8rem; margin-bottom: 1rem; border: 1px solid #e2e8f0; border-radius: 0.5rem; box-sizing: border-box; background: #f7fafc; }"
            ".btn-login { width: 100%; background: linear-gradient(135deg, #4299e1 0%, #3182ce 100%); color: white; padding: 0.8rem; border: none; border-radius: 0.5rem; font-weight: 600; cursor: pointer; }"
            ".file-upload-label { display: block; padding: 1rem; color: #4a5568; background: #edf2f7; border-radius: 0.5rem; cursor: pointer; border: 2px dashed #cbd5e0; margin-bottom: 10px; }"
            "#prgbar { width: 100%; background-color: #edf2f7; border-radius: 10px; height: 10px; overflow: hidden; }"
            "#bar { width: 0%; height: 100%; background: #48bb78; transition: width 0.3s; }"
            "</style>"
        );
    }

    // --- MANEJADORES DE PETICIONES (Páginas) ---
    void handleRoot() {
        String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>" + getStyle() + "</head><body><div class='container'><div class='card'>";
        html += "<div class='company-name'>MATEO E HIJO</div><div class='subtitle'>PLC de ABARAN</div>";
        html += "<h2>Carga de Código</h2>";
        html += "<form name='loginForm'>";
        html += "<input type='text' name='userid' placeholder='Usuario' autocomplete='off'>";
        html += "<input type='password' name='pwd' placeholder='Contraseña'>";
        html += "<input type='button' onclick='check()' class='btn-login' value='Entrar'>";
        html += "</form></div></div>";
        html += "<script>function check() { var u = document.getElementsByName('userid')[0].value; var p = document.getElementsByName('pwd')[0].value; fetch('/login', { method: 'POST', headers: {'Content-Type': 'application/x-www-form-urlencoded'}, body: 'userid='+encodeURIComponent(u)+'&pwd='+encodeURIComponent(p) }).then(r => { if(r.ok) window.location.href='/serverIndex'; else alert('Acceso Denegado'); }); }</script></body></html>";
        server.send(200, "text/html", html);
    }

    void handleServerIndex() {
        String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>" + getStyle() + "<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script></head><body><div class='container'>";
        html += "<div class='card'>";
        html += "<div class='company-name'>MATEO E HIJO</div><div class='subtitle'>PLC de Abaran</div>";
        html += "<h2>Enviar Comando</h2>";
        html += "<div class='cmd-group'>";
        html += "<input type='text' id='cmdInput' placeholder='Ej: a, z, m20...' maxlength='10' onkeydown='if(event.key===\"Enter\") sendCmd()'>";
        html += "<button class='btn-send' onclick='sendCmd()'>Enviar</button>";
        html += "</div>";
        html += "<h2>Registro (Logs)</h2>";
        html += "<div id='console'>Conectando con PLC...</div>";
        html += "</div>";
        html += "<div class='card'>";
        html += "<h2>Actualizar Firmware</h2>";
        html += "<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>";
        html += "<input type='file' name='update' id='file' onchange='sub(this)' style='display:none;'>";
        html += "<label for='file' class='file-upload-label' id='file-label'>📂 Toca para elegir archivo .bin</label>";
        html += "<input type='submit' class='btn-login' value='Instalar Actualización'>";
        html += "<div id='prgbar' style='margin-top:10px;'><div id='bar'></div></div>";
        html += "<div id='prg'></div>";
        html += "</form></div>";
        html += "</div>"; 
        html += "<script>";
        html += "function sendCmd() { var input = document.getElementById('cmdInput'); var val = input.value; if(val.length > 0) { fetch('/cmd?c=' + val); input.value = ''; } }";
        html += "setInterval(function() { fetch('/logs').then(r => r.text()).then(data => { document.getElementById('console').innerHTML = data.replace(/\\n/g, '<br>'); }); }, 1000);";
        html += "function sub(obj){ var fileName = obj.value.split('\\\\'); document.getElementById('file-label').innerHTML = '📄 ' + fileName[fileName.length-1]; };";
        html += "$('form').submit(function(e){ e.preventDefault(); var form = $('#upload_form')[0]; var data = new FormData(form); $.ajax({ url: '/update', type: 'POST', data: data, contentType: false, processData:false, xhr: function() { var xhr = new window.XMLHttpRequest(); xhr.upload.addEventListener('progress', function(evt) { if (evt.lengthComputable) { var per = evt.loaded / evt.total; $('#prg').html(Math.round(per*100) + '%'); $('#bar').css('width',Math.round(per*100) + '%'); } }, false); return xhr; }, success:function(d, s) { console.log('success!') }, error: function (a, b, c) { } }); });";
        html += "</script></body></html>";
        server.send(200, "text/html", html);
    }

    void handleLogin() {
        if (server.hasArg("userid") && server.hasArg("pwd")) {
            if (server.arg("userid") == "admin" && server.arg("pwd") == "admin") {
                server.sendHeader("Location", "/serverIndex");
                server.send(303);
            } else { server.send(401, "text/plain", "Acceso Denegado"); }
        } else { server.send(400, "text/plain", "Petición Incorrecta"); }
    }

    void handleCommand() {
        if (server.hasArg("c")) {
            String cmd = server.arg("c");
            if (cmd.length() > 0 && onCommandReceived) {
                onCommandReceived(cmd);
            }
        }
        server.send(200, "text/plain", "OK");
    }

    void handleGetLogs() {
        server.send(200, "text/plain", webLogBuffer);
    }

    void handleUpdateEnd() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/plain", (Update.hasError()) ? "FALLO" : "OK");
        ESP.restart();
    }

    void handleDoUpdate() {
        HTTPUpload& upload = server.upload();
        if (upload.status == UPLOAD_FILE_START) {
            log("Iniciando actualización: " + upload.filename);
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) Update.printError(Serial);
        } else if (upload.status == UPLOAD_FILE_END) {
            if (Update.end(true)) log("Firmware actualizado: " + String(upload.totalSize) + " bytes");
            else Update.printError(Serial);
        }
    }
};

#endif

*/









/*
#ifndef WIFIWEBSERVER_H
#define WIFIWEBSERVER_H

#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>

String webLogBuffer = "";

class WiFiWebServer {
public:
  WiFiWebServer(const char* ssid, const char* password, const char* host) 
    : ssid(ssid), password(password), host(host), server(80) {}

  // --- CAMBIO AQUÍ: Ahora aceptamos String (texto completo) en vez de char (letra) ---
  typedef std::function<void(String)> CommandHandler;
  CommandHandler onCommandReceived;

  void setCommandCallback(CommandHandler handler) {
    onCommandReceived = handler;
  }

  void log(String text) {
    String timePrefix = "[" + String(millis()/1000) + "s] ";
    webLogBuffer = timePrefix + text + "\n" + webLogBuffer; 
    if (webLogBuffer.length() > 2000) webLogBuffer = webLogBuffer.substring(0, 2000);
  }

  void setup() {
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
    }
    Serial.println("");
    Serial.print("WiFi Conectado. IP: "); Serial.println(WiFi.localIP());

    if (!MDNS.begin(host)) {
      Serial.println("Error mDNS");
    }

    server.on("/", HTTP_GET, std::bind(&WiFiWebServer::handleRoot, this));
    server.on("/serverIndex", HTTP_GET, std::bind(&WiFiWebServer::handleServerIndex, this));
    server.on("/login", HTTP_POST, std::bind(&WiFiWebServer::handleLogin, this));
    server.on("/update", HTTP_POST, std::bind(&WiFiWebServer::handleUpdateEnd, this), std::bind(&WiFiWebServer::handleDoUpdate, this));
    server.on("/cmd", HTTP_GET, std::bind(&WiFiWebServer::handleCommand, this));
    server.on("/logs", HTTP_GET, std::bind(&WiFiWebServer::handleGetLogs, this));

    server.begin();
  }

  void loop() {
    server.handleClient();
  }

private:
  const char* ssid;
  const char* password;
  const char* host;
  WebServer server;

  // ESTILOS CSS
  String getStyle() {
    return F(
      "<style>"
      "@import url('https://fonts.googleapis.com/css2?family=Poppins:wght@300;400;500;700&display=swap');"
      "body { background: #f0f2f5; font-family: 'Poppins', sans-serif; display: flex; flex-direction: column; align-items: center; min-height: 100vh; margin: 0; color: #4a5568; padding: 20px; }"
      ".container { width: 100%; max-width: 500px; }"
      ".card { background: white; padding: 2rem; border-radius: 1rem; box-shadow: 0 10px 25px -5px rgba(0,0,0,0.1); margin-bottom: 20px; text-align: center; }"
      ".company-name { font-size: 1.8rem; font-weight: 700; margin-bottom: 0; background: -webkit-linear-gradient(45deg, #3182ce, #2c5282); -webkit-background-clip: text; -webkit-text-fill-color: transparent; }"
      ".subtitle { font-size: 0.9rem; color: #718096; margin-bottom: 1.5rem; font-weight: 500; text-transform: uppercase; letter-spacing: 1px; }"
      "h2 { font-size: 1.2rem; font-weight: 600; margin-bottom: 1rem; color: #2d3748; border-bottom: 2px solid #edf2f7; padding-bottom: 5px; display: inline-block; }"
      ".cmd-group { display: flex; gap: 10px; margin-bottom: 15px; }"
      "#cmdInput { flex: 1; padding: 12px; font-size: 1.1rem; text-align: center; border: 2px solid #e2e8f0; border-radius: 8px; outline: none; transition: border-color 0.2s; }" 
      "#cmdInput:focus { border-color: #4299e1; }"
      ".btn-send { background: linear-gradient(135deg, #4299e1 0%, #3182ce 100%); color: white; border: none; border-radius: 8px; padding: 0 20px; font-weight: 600; cursor: pointer; transition: transform 0.1s; }"
      ".btn-send:active { transform: scale(0.95); }"
      "#console { background: #1a202c; color: #48bb78; font-family: 'Courier New', monospace; padding: 15px; border-radius: 8px; text-align: left; height: 200px; overflow-y: auto; font-size: 0.85rem; margin-top: 10px; border: 1px solid #2d3748; box-shadow: inset 0 2px 4px rgba(0,0,0,0.3); }"
      "input { width: 100%; padding: 0.8rem; margin-bottom: 1rem; border: 1px solid #e2e8f0; border-radius: 0.5rem; box-sizing: border-box; background: #f7fafc; }"
      ".btn-login { width: 100%; background: linear-gradient(135deg, #4299e1 0%, #3182ce 100%); color: white; padding: 0.8rem; border: none; border-radius: 0.5rem; font-weight: 600; cursor: pointer; }"
      ".file-upload-label { display: block; padding: 1rem; color: #4a5568; background: #edf2f7; border-radius: 0.5rem; cursor: pointer; border: 2px dashed #cbd5e0; margin-bottom: 10px; }"
      "#prgbar { width: 100%; background-color: #edf2f7; border-radius: 10px; height: 10px; overflow: hidden; }"
      "#bar { width: 0%; height: 100%; background: #48bb78; transition: width 0.3s; }"
      "</style>"
    );
  }

  void handleRoot() {
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>" + getStyle() + "</head><body><div class='container'><div class='card'>";
    html += "<div class='company-name'>MATEO E HIJO</div><div class='subtitle'>PLC de ABARAN</div>";
    html += "<h2>Carga de Código</h2>";
    html += "<form name='loginForm'>";
    html += "<input type='text' name='userid' placeholder='Usuario' autocomplete='off'>";
    html += "<input type='password' name='pwd' placeholder='Contraseña'>";
    html += "<input type='button' onclick='check()' class='btn-login' value='Entrar'>";
    html += "</form></div></div>";
    html += "<script>function check() { var u = document.getElementsByName('userid')[0].value; var p = document.getElementsByName('pwd')[0].value; fetch('/login', { method: 'POST', headers: {'Content-Type': 'application/x-www-form-urlencoded'}, body: 'userid='+encodeURIComponent(u)+'&pwd='+encodeURIComponent(p) }).then(r => { if(r.ok) window.location.href='/serverIndex'; else alert('Acceso Denegado'); }); }</script></body></html>";
    server.send(200, "text/html", html);
  }

  void handleServerIndex() {
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>" + getStyle() + "<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script></head><body><div class='container'>";
    html += "<div class='card'>";
    html += "<div class='company-name'>MATEO E HIJO</div><div class='subtitle'>PLC de Abaran</div>";
    html += "<h2>Enviar Comando</h2>";
    html += "<div class='cmd-group'>";
    html += "<input type='text' id='cmdInput' placeholder='Ej: a, z, m20...' maxlength='10' onkeydown='if(event.key===\"Enter\") sendCmd()'>";
    html += "<button class='btn-send' onclick='sendCmd()'>Enviar</button>";
    html += "</div>";
    html += "<h2>Registro (Logs)</h2>";
    html += "<div id='console'>Conectando con PLC...</div>";
    html += "</div>";
    html += "<div class='card'>";
    html += "<h2>Actualizar Firmware</h2>";
    html += "<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>";
    html += "<input type='file' name='update' id='file' onchange='sub(this)' style='display:none;'>";
    html += "<label for='file' class='file-upload-label' id='file-label'>📂 Toca para elegir archivo .bin</label>";
    html += "<input type='submit' class='btn-login' value='Instalar Actualización'>";
    html += "<div id='prgbar' style='margin-top:10px;'><div id='bar'></div></div>";
    html += "<div id='prg'></div>";
    html += "</form></div>";
    html += "</div>"; 
    html += "<script>";
    html += "function sendCmd() { var input = document.getElementById('cmdInput'); var val = input.value; if(val.length > 0) { fetch('/cmd?c=' + val); input.value = ''; } }";
    html += "setInterval(function() { fetch('/logs').then(r => r.text()).then(data => { document.getElementById('console').innerHTML = data.replace(/\\n/g, '<br>'); }); }, 1000);";
    html += "function sub(obj){ var fileName = obj.value.split('\\\\'); document.getElementById('file-label').innerHTML = '📄 ' + fileName[fileName.length-1]; };";
    html += "$('form').submit(function(e){ e.preventDefault(); var form = $('#upload_form')[0]; var data = new FormData(form); $.ajax({ url: '/update', type: 'POST', data: data, contentType: false, processData:false, xhr: function() { var xhr = new window.XMLHttpRequest(); xhr.upload.addEventListener('progress', function(evt) { if (evt.lengthComputable) { var per = evt.loaded / evt.total; $('#prg').html(Math.round(per*100) + '%'); $('#bar').css('width',Math.round(per*100) + '%'); } }, false); return xhr; }, success:function(d, s) { console.log('success!') }, error: function (a, b, c) { } }); });";
    html += "</script></body></html>";
    server.send(200, "text/html", html);
  }

  void handleLogin() {
    if (server.hasArg("userid") && server.hasArg("pwd")) {
      if (server.arg("userid") == "admin" && server.arg("pwd") == "admin") {
        server.sendHeader("Location", "/serverIndex");
        server.send(303);
      } else { server.send(401, "text/plain", "Error"); }
    } else { server.send(400, "text/plain", "Bad Request"); }
  }

  // --- CAMBIO AQUÍ: Enviamos el string completo (cmd) ---
  void handleCommand() {
    if (server.hasArg("c")) {
      String cmd = server.arg("c");
      if (cmd.length() > 0 && onCommandReceived) {
        // Antes era: char c = cmd.charAt(0);
        // AHORA: Pasamos todo el string
        onCommandReceived(cmd);
      }
    }
    server.send(200, "text/plain", "OK");
  }

  void handleGetLogs() {
    server.send(200, "text/plain", webLogBuffer);
  }

  void handleUpdateEnd() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "FALLO" : "OK");
    ESP.restart();
  }

  void handleDoUpdate() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      log("Actualizando: " + upload.filename);
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) Update.printError(Serial);
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) log("Exito OTA: " + String(upload.totalSize) + " bytes");
      else Update.printError(Serial);
    }
  }
};

#endif
*/
