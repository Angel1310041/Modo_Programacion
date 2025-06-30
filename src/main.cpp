#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <RCSwitch.h>
#include <heltec.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include "esp_task_wdt.h"

// ==================== CONFIGURACIÓN AP ====================
IPAddress local_IP(192, 168, 1, 1);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
const char* ssidAP = "PARQUES AZCAPOTZALCO";

// ==================== CONFIGURACIÓN STA ====================
String ssidSTA = "";
String passwordSTA = "";

// Para registrar una red wifi a la tarjeta debe de ingresarse desde el navegador lo siguiente: 192.168.1.1/Grabar-Wifi



// ==================== RED MOSTRADA EN INTERFAZ ====================
String ssidMostrada = "";
String passwordMostrada = "";

// ==================== SERVIDOR Y DNS ====================
AsyncWebServer server(80);
DNSServer dnsServer;

// ==================== LED & RELÉ ====================
#define LED_PIN 35
#define RELE_AMPLIFICADOR 47
bool estadoRele = false;
unsigned long previousMillis = 0;
const long onTime = 500;
const long offTime = 2000;
bool ledState = false;

// ==================== TARJETA ====================
struct Tarjeta {
  String servidorComandos = "alarmasvecinales.online";
  String tarjetaID = "SV33333333";
} tarjeta;

// ==================== REFLECTORES & RF ====================
RCSwitch TransmisorRF = RCSwitch();
const uint8_t reflectorPins[8] = {1, 2, 3, 4, 5, 6, 7, 48};
IRsend* irReflectores[8];
const uint64_t controlReflector[24] = {
  0xffa05f, 0xff20df, 0xff609f, 0xffe01f, 0xff906f, 0xff10ef, 0xff50af, 0xffd02f,
  0xffb04f, 0xff30cf, 0xff708f, 0xfff00f, 0xffa857, 0xff28d7, 0xff6897, 0xffe817,
  0xff9867, 0xff18e7, 0xff58a7, 0xffd827, 0xff8877, 0xff08f7, 0xff48b7, 0xffc837
};
const uint64_t controlRF[4] = {8999768, 8999764, 8999762, 8999761};

// ==================== COLA DE COMANDOS ====================
typedef struct {
  char nombre[32];
  char telefono[20];
} ComandoWeb;
QueueHandle_t comandoQueue;
String ultimoComandoWeb = "";
bool comandoF2Enviado = true;

// ==================== FUNCIONES AUXILIARES ====================
void parpadearLed() {
  digitalWrite(LED_PIN, HIGH);
  delay(100);
  digitalWrite(LED_PIN, LOW);
}

String getSendURL() {
  return "http://" + tarjeta.servidorComandos + "/MiAlarma3.0/ABM_LORA/Suscripcion_AZCAPOTZALCO.php?id=" + tarjeta.tarjetaID + "&Vecino=";
}

String urlEncode(const char* str) {
  String encoded = "";
  char c, code0, code1;
  for (int i = 0; str[i] != '\0'; i++) {
    c = str[i];
    if (isalnum(c)) {
      encoded += c;
    } else if (c == ' ') {
      encoded += "%20";
    } else {
      code1 = (c & 0xf) + '0'; if ((c & 0xf) > 9) code1 = (c & 0xf) - 10 + 'A';
      code0 = ((c >> 4) & 0xf) + '0'; if (((c >> 4) & 0xf) > 9) code0 = ((c >> 4) & 0xf) - 10 + 'A';
      encoded += '%'; encoded += code0; encoded += code1;
    }
  }
  return encoded;
}

// ==================== TAREAS ====================
void wifiTask(void * parameter) {
  const unsigned long reconnectInterval = 10000;
  unsigned long lastReconnectAttempt = 0;
  for (;;) {
    if (WiFi.status() == WL_DISCONNECTED) {
      if (millis() - lastReconnectAttempt > reconnectInterval) {
        lastReconnectAttempt = millis();
        Serial.println("Intentando reconectar STA...");
        if (ssidSTA.length() > 0 && passwordSTA.length() > 0) {
          WiFi.begin(ssidSTA.c_str(), passwordSTA.c_str());
        }
      }
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

void enviarComandoWebTask(void * parameter) {
  ComandoWeb comando;
  for (;;) {
    if (xQueueReceive(comandoQueue, &comando, portMAX_DELAY) == pdTRUE) {
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("No hay conexión STA para enviar: " + String(comando.nombre));
        continue;
      }
      esp_task_wdt_reset();

      char telefonoSinEspacios[20]; int j = 0;
      for (int i = 0; comando.telefono[i] != '\0' && j < 19; i++) {
        if (comando.telefono[i] != ' ') telefonoSinEspacios[j++] = comando.telefono[i];
      }
      telefonoSinEspacios[j] = '\0';

      String SEND_URL = getSendURL() + urlEncode(comando.nombre) + "&Tel=" + telefonoSinEspacios;
      Serial.println("URL enviada: " + SEND_URL);

      const int maxReintentos = 3;
      for (int reintentos = 0; reintentos < maxReintentos; reintentos++) {
        WiFiClient client;
        HTTPClient http;
        http.setTimeout(5000);
        http.setReuse(false);
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        int sendHttpCode = -1;
        if (http.begin(client, SEND_URL)) sendHttpCode = http.GET();
        if (sendHttpCode > 0 && WiFi.status() == WL_CONNECTED) {
          Serial.println("Comando enviado: " + String(comando.nombre));
          if (comandoF2Enviado) ultimoComandoWeb = String(comando.nombre);
          http.end(); break;
        } else {
          Serial.println("Error: " + String(comando.nombre) + " -> " + http.errorToString(sendHttpCode));
          http.end(); vTaskDelay(200 / portTICK_PERIOD_MS);
        }
      }
    }
  }
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  pinMode(RELE_AMPLIFICADOR, OUTPUT);
  digitalWrite(RELE_AMPLIFICADOR, LOW);

  Heltec.begin(true, false, true);
  Heltec.display->clear();
  Heltec.display->setTextAlignment(TEXT_ALIGN_CENTER);
  Heltec.display->setFont(ArialMT_Plain_16);
  Heltec.display->drawString(64, 25, "AZCAPOTZALCO");
  Heltec.display->display();
  digitalWrite(LED_PIN, HIGH); delay(200); digitalWrite(LED_PIN, LOW);

  if (!SPIFFS.begin(true)) { Serial.println("Error al montar SPIFFS"); return; }

  // Leer SSID y password desde SPIFFS si existen
  File f = SPIFFS.open("/wifi.txt", FILE_READ);
  if (f) {
    String ssid = f.readStringUntil('\n');
    String pass = f.readStringUntil('\n');
    ssid.trim();
    pass.trim();
    if (ssid.length() > 0) ssidSTA = ssid;
    if (pass.length() > 0) passwordSTA = pass;
    f.close();
    Serial.println("WiFi personalizado leído de SPIFFS");
  }

  // Leer SSID mostrada desde SPIFFS si existe
  File f2 = SPIFFS.open("/red_mostrada.txt", FILE_READ);
  if (f2) {
    ssidMostrada = f2.readStringUntil('\n');
    ssidMostrada.trim();
    f2.close();
    Serial.println("Red mostrada leída de SPIFFS");
  }
  // Leer contraseña mostrada desde SPIFFS si existe
  File f3 = SPIFFS.open("/clave_mostrada.txt", FILE_READ);
  if (f3) {
    passwordMostrada = f3.readStringUntil('\n');
    passwordMostrada.trim();
    f3.close();
    Serial.println("Clave mostrada leída de SPIFFS");
  }

  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAPConfig(local_IP, gateway, subnet)) { Serial.println("Error IP estática AP"); return; }
  if (!WiFi.softAP(ssidAP)) { Serial.println("Error iniciar AP"); return; }
  Serial.println("AP iniciado en IP: " + WiFi.softAPIP().toString());

  // Solo intenta conectar si hay datos guardados
  if (ssidSTA.length() > 0 && passwordSTA.length() > 0) {
    WiFi.begin(ssidSTA.c_str(), passwordSTA.c_str());
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) { delay(500); Serial.print("."); }
    if (WiFi.status() == WL_CONNECTED)
      Serial.println("\nSTA conectada en IP: " + WiFi.localIP().toString());
    else Serial.println("\nNo se pudo conectar STA");
  } else {
    Serial.println("No hay datos de WiFi STA guardados.");
  }

  dnsServer.start(53, "*", local_IP);

  comandoQueue = xQueueCreate(5, sizeof(ComandoWeb));
  if (comandoQueue == NULL) { Serial.println("Error al crear la cola de comandos"); while (1) delay(1000); }

  TransmisorRF.enableTransmit(33);
  Serial2.begin(9600, SERIAL_8N1, 46, 45);
  for (int i = 0; i < 8; i++) {
    irReflectores[i] = new IRsend(reflectorPins[i]);
    irReflectores[i]->begin();
  }

  // RUTAS DEL SERVIDOR
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    // Lee la red mostrada y la clave mostrada
    String ssid_mostrada = ssidMostrada;
    String clave_mostrada = passwordMostrada;
    File f2 = SPIFFS.open("/red_mostrada.txt", FILE_READ);
    if (f2) {
      String s = f2.readStringUntil('\n');
      s.trim();
      if (s.length() > 0) ssid_mostrada = s;
      f2.close();
    }
    File f3 = SPIFFS.open("/clave_mostrada.txt", FILE_READ);
    if (f3) {
      String s = f3.readStringUntil('\n');
      s.trim();
      if (s.length() > 0) clave_mostrada = s;
      f3.close();
    }

    // Si existe el archivo comprimido, lo sirve y pasa la red y clave mostrada por cabecera
    if (SPIFFS.exists("/interfaz.html.gz")) {
      AsyncWebServerResponse *resp = request->beginResponse(SPIFFS, "/interfaz.html.gz", "text/html");
      resp->addHeader("Content-Encoding", "gzip");
      resp->addHeader("X-SSID-MOSTRADA", ssid_mostrada);
      resp->addHeader("X-PASSWORD-MOSTRADA", clave_mostrada);
      request->send(resp);
    } else {
      request->send(404, "text/plain", "Archivo interfaz.html.gz no encontrado");
    }
  });

  server.on("/enviar", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("comando") && request->hasParam("telefono")) {
      ComandoWeb cmd;
      request->getParam("comando")->value().toCharArray(cmd.nombre, sizeof(cmd.nombre));
      request->getParam("telefono")->value().toCharArray(cmd.telefono, sizeof(cmd.telefono));
      xQueueSend(comandoQueue, &cmd, 100);
      request->send(200, "text/plain", "Comando recibido correctamente");
    } else request->send(400, "text/plain", "Faltan parametros");
  });

  // Rutas para compatibilidad con portal cautivo
  server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request){ request->redirect("/"); });
  server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *request){ request->redirect("/"); });
  server.on("/fwlink", HTTP_GET, [](AsyncWebServerRequest *request){ request->redirect("/"); });

  // Ruta oculta para grabar WiFi real (NO se muestra en la interfaz)
  server.on("/Grabar-Wifi", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html = R"rawliteral(
      <!DOCTYPE html>
      <html>
      <head>
        <title>Configurar WiFi</title>
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <style>
          body {
            font-family: 'Segoe UI', Arial, sans-serif;
            background: linear-gradient(120deg, #0078d7 0%, #00c6fb 100%);
            min-height: 100vh;
            margin: 0;
            display: flex;
            justify-content: center;
            align-items: center;
          }
          .form-container {
            display: flex;
            flex-direction: column;
            gap: 32px;
            align-items: center;
          }
          .wifi-form {
            background: #fff;
            padding: 32px 28px 24px 28px;
            border-radius: 16px;
            box-shadow: 0 8px 32px rgba(0,0,0,0.18);
            min-width: 320px;
            max-width: 90vw;
            animation: fadeIn 0.7s;
          }
          @keyframes fadeIn {
            from { opacity: 0; transform: translateY(30px);}
            to { opacity: 1; transform: translateY(0);}
          }
          .wifi-form h2 {
            margin-bottom: 22px;
            color: #0078d7;
            text-align: center;
            font-weight: 600;
            letter-spacing: 1px;
          }
          .wifi-form label {
            display: block;
            margin-bottom: 7px;
            color: #333;
            font-size: 15px;
            font-weight: 500;
          }
          .wifi-form input[type='text'],
          .wifi-form input[type='password'] {
            width: 100%;
            padding: 10px 12px;
            margin-bottom: 18px;
            border: 1px solid #b3b3b3;
            border-radius: 6px;
            font-size: 16px;
            background: #f9f9f9;
            transition: border 0.2s;
          }
          .wifi-form input[type='text']:focus,
          .wifi-form input[type='password']:focus {
            border: 1.5px solid #0078d7;
            outline: none;
            background: #fff;
          }
          .wifi-form input[type='submit'] {
            width: 100%;
            padding: 12px;
            background: linear-gradient(90deg, #0078d7 0%, #00c6fb 100%);
            color: #fff;
            border: none;
            border-radius: 6px;
            font-size: 17px;
            font-weight: 600;
            cursor: pointer;
            box-shadow: 0 2px 8px rgba(0,120,215,0.08);
            transition: background 0.2s, transform 0.1s;
          }
          .wifi-form input[type='submit']:hover {
            background: linear-gradient(90deg, #005fa3 0%, #00a6d7 100%);
            transform: translateY(-2px) scale(1.02);
          }
        </style>
      </head>
      <body>
        <div class="form-container">
          <form class="wifi-form" action='/Guardar-Wifi' method='POST'>
            <h2>Registrar Red WiFi Real</h2>
            <label for="ssid">SSID:</label>
            <input type='text' id='ssid' name='ssid' required autocomplete="off">
            <label for="password">Contraseña:</label>
            <input type='password' id='password' name='password' required autocomplete="off">
            <input type='submit' value='Guardar'>
          </form>
          <form class="wifi-form" action='/Guardar-Red-Mostrada' method='POST'>
            <h2>Red Mostrada en Interfaz</h2>
            <label for="ssid_mostrada">Nombre de la Red a Mostrar:</label>
            <input type='text' id='ssid_mostrada' name='ssid_mostrada' required autocomplete="off">
            <label for="password_mostrada">Contraseña a Mostrar:</label>
            <input type='password' id='password_mostrada' name='password_mostrada' required autocomplete="off">
            <input type='submit' value='Guardar Red Mostrada'>
          </form>
        </div>
      </body>
      </html>
    )rawliteral";
    request->send(200, "text/html", html);
  });

  server.on("/Guardar-Wifi", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("ssid", true) && request->hasParam("password", true)) {
      String ssid = request->getParam("ssid", true)->value();
      String password = request->getParam("password", true)->value();

      File f = SPIFFS.open("/wifi.txt", FILE_WRITE);
      if (f) {
        f.println(ssid);
        f.println(password);
        f.close();
        ssidSTA = ssid;
        passwordSTA = password;
        request->send(200, "text/html", "<h3>Datos guardados. Reinicie el dispositivo.</h3>");
      } else {
        request->send(500, "text/plain", "Error guardando datos");
      }
    } else {
      request->send(400, "text/plain", "Faltan parámetros");
    }
  });

  // Ruta para guardar la red mostrada y su contraseña
  server.on("/Guardar-Red-Mostrada", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("ssid_mostrada", true) && request->hasParam("password_mostrada", true)) {
      String ssid_mostrada = request->getParam("ssid_mostrada", true)->value();
      String password_mostrada = request->getParam("password_mostrada", true)->value();

      File f = SPIFFS.open("/red_mostrada.txt", FILE_WRITE);
      File f2 = SPIFFS.open("/clave_mostrada.txt", FILE_WRITE);
      if (f && f2) {
        f.println(ssid_mostrada);
        f.close();
        f2.println(password_mostrada);
        f2.close();
        ssidMostrada = ssid_mostrada;
        passwordMostrada = password_mostrada;
        request->send(200, "text/html", "<h3>Red mostrada y clave guardadas.</h3>");
      } else {
        request->send(500, "text/plain", "Error guardando red mostrada o clave");
      }
    } else {
      request->send(400, "text/plain", "Faltan parámetros");
    }
  });

  server.onNotFound([](AsyncWebServerRequest *request) {
    // Redirige a la página principal dinámica
    request->redirect("/");
  });

  server.begin();
  xTaskCreate(wifiTask, "WiFi Task", 6144, NULL, 1, NULL);
  xTaskCreate(enviarComandoWebTask, "HTTP Task", 8192, NULL, 1, NULL);
}

// ==================== LOOP ====================
void loop() {
  dnsServer.processNextRequest();

  unsigned long currentMillis = millis();
  if (!ledState && currentMillis - previousMillis >= offTime) {
    ledState = true; previousMillis = currentMillis; digitalWrite(LED_PIN, HIGH);
  } else if (ledState && currentMillis - previousMillis >= onTime) {
    ledState = false; previousMillis = currentMillis; digitalWrite(LED_PIN, LOW);
  }

  if (Serial2.available()) {
    String comando = Serial2.readStringUntil('\n');
    comando.trim();
    if (comando.startsWith("LORA>R") && comando.length() >= 8) {
      int reflectorID = comando.substring(6, 7).toInt();
      int funcion = comando.substring(7).toInt();
      if (reflectorID >= 0 && reflectorID <= 8 && funcion >= 1 && funcion <= 24) {
        uint64_t codigoIR = controlReflector[funcion - 1];
        if (reflectorID == 0) {
          for (int i = 0; i < 8; i++) {
            irReflectores[i]->sendNEC(codigoIR);
            parpadearLed(); delay(50);
          }
        } else {
          irReflectores[reflectorID - 1]->sendNEC(codigoIR);
          parpadearLed();
        }
      } else if (funcion >= 25 && funcion <= 28) {
        int rfIndex = funcion - 25;
        if (rfIndex >= 0 && rfIndex < 4) {
          TransmisorRF.send(controlRF[rfIndex], 27);
          parpadearLed();
        }
      }
    } else if (comando.startsWith("LORA>A")) {
      estadoRele = !estadoRele;
      digitalWrite(RELE_AMPLIFICADOR, estadoRele ? HIGH : LOW);
    }
  }
}