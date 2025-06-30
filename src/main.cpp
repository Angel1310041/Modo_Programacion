#include <Arduino.h>
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

// ==================== CONFIGURACIÓN STA (Internet) ====================
String ssidSTA = "";
String passwordSTA = "";

// ==================== SERVIDOR Y DNS ====================
AsyncWebServer server(80);
DNSServer dnsServer;

// ==================== LED ====================
const int LED_PIN = 35;
unsigned long previousMillis = 0;
const long onTime = 500;
const long offTime = 2000;
bool ledState = false;

// ==================== VARIABLES TARJETA / ENVÍO WEB ====================
struct Tarjeta {
  String servidorComandos = "alarmasvecinales.online";
  String tarjetaID = "SV33333333";
} tarjeta;

String ultimoComandoWeb = "";
bool comandoF2Enviado = true;

// ==================== Estructura para la cola de comandos ====================
typedef struct {
  char nombre[32];
  char telefono[20];
} ComandoWeb;

QueueHandle_t comandoQueue;

// ==================== URL de envío remoto ====================
String getSendURL() {
  return "http://" + tarjeta.servidorComandos + "/MiAlarma3.0/ABM_LORA/Suscripcion_AZCAPOTZALCO.php?id=" + tarjeta.tarjetaID + "&Vecino=";
}

// ==================== Codificación URL ====================
String urlEncode(const char* str) {
  String encoded = "";
  char c;
  char code0, code1;
  for (int i = 0; str[i] != '\0'; i++) {
    c = str[i];
    if (isalnum(c)) {
      encoded += c;
    } else if (c == ' ') {
      encoded += "%20";
    } else {
      code1 = (c & 0xf) + '0';
      if ((c & 0xf) > 9) code1 = (c & 0xf) - 10 + 'A';
      code0 = ((c >> 4) & 0xf) + '0';
      if (((c >> 4) & 0xf) > 9) code0 = ((c >> 4) & 0xf) - 10 + 'A';
      encoded += '%';
      encoded += code0;
      encoded += code1;
    }
  }
  return encoded;
}

// ==================== Tarea: Reconexión WiFi ====================
void wifiTask(void * parameter) {
  const unsigned long reconnectInterval = 10000; // 10 s
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

// ==================== Tarea: Envío de comandos HTTP ====================
void enviarComandoWebTask(void * parameter) {
  ComandoWeb comando;
  for (;;) {
    if (xQueueReceive(comandoQueue, &comando, portMAX_DELAY) == pdTRUE) {
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("No hay conexión STA para enviar: " + String(comando.nombre));
        continue;
      }
      esp_task_wdt_reset();

      char telefonoSinEspacios[20];
      int j = 0;
      for (int i = 0; comando.telefono[i] != '\0' && j < 19; i++) {
        if (comando.telefono[i] != ' ') {
          telefonoSinEspacios[j++] = comando.telefono[i];
        }
      }
      telefonoSinEspacios[j] = '\0';

      char comandoCompleto[54];
      snprintf(comandoCompleto, sizeof(comandoCompleto), "%s_%s", comando.nombre, telefonoSinEspacios);
      String SEND_URL = getSendURL() + urlEncode(comando.nombre ) + "&Tel="+telefonoSinEspacios;
      Serial.println("URL enviada: " + SEND_URL);

      const int maxReintentos = 3;
      for (int reintentos = 0; reintentos < maxReintentos; reintentos++) {
        WiFiClient client;  // 👉 cliente TCP dedicado
        HTTPClient http;
        http.setTimeout(5000);
        http.setReuse(false);
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

        int sendHttpCode = -1;
        if (http.begin(client, SEND_URL)) {
          sendHttpCode = http.GET();
        }

        if (sendHttpCode > 0 && WiFi.status() == WL_CONNECTED) {
          Serial.println("Comando enviado: " + String(comando.nombre) + ", Telefono: " + String(comando.telefono));
          if (comandoF2Enviado) {
            ultimoComandoWeb = String(comando.nombre);
          }
          http.end();
          break;
        } else {
          Serial.println("Error: " + String(comando.nombre) + " -> " + http.errorToString(sendHttpCode));
          http.end();
          vTaskDelay(200 / portTICK_PERIOD_MS);
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

  Heltec.begin(true, false, true);
  Heltec.display->clear();
  Heltec.display->setTextAlignment(TEXT_ALIGN_CENTER);
  Heltec.display->setFont(ArialMT_Plain_16);
  Heltec.display->drawString(64, 25, "AZCAPOTZALCO");
  Heltec.display->display();

  digitalWrite(LED_PIN, HIGH);
  delay(200);
  digitalWrite(LED_PIN, LOW);

  if (!SPIFFS.begin(true)) {
    Serial.println("Error al montar SPIFFS");
    return;
  }

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

  WiFi.mode(WIFI_AP_STA);

  if (!WiFi.softAPConfig(local_IP, gateway, subnet)) {
    Serial.println("Error IP estática AP");
    return;
  }
  if (!WiFi.softAP(ssidAP)) {
    Serial.println("Error iniciar AP");
    return;
  }
  Serial.println("AP iniciado en IP: " + WiFi.softAPIP().toString());

  // Solo intenta conectar si hay datos guardados
  if (ssidSTA.length() > 0 && passwordSTA.length() > 0) {
    WiFi.begin(ssidSTA.c_str(), passwordSTA.c_str());
    Serial.print("Conectando a STA...");
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
      Serial.print(".");
      delay(500);
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("");
      Serial.println("STA conectada en IP: " + WiFi.localIP().toString());
    } else {
      Serial.println("");
      Serial.println("No se pudo conectar STA (Internet)");
    }
  } else {
    Serial.println("No hay datos de WiFi STA guardados.");
  }

  dnsServer.start(53, "*", local_IP);

  comandoQueue = xQueueCreate(5, sizeof(ComandoWeb));
  if (comandoQueue == NULL) {
    Serial.println("Error al crear la cola de comandos");
    while (1) delay(1000);
  }

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!SPIFFS.exists("/interfaz.html.gz")) {
      request->send(404, "text/plain", "Archivo principal no encontrado");
      return;
    }
    auto resp = request->beginResponse(SPIFFS, "/interfaz.html.gz", "text/html");
    resp->addHeader("Content-Encoding", "gzip");
    request->send(resp);
  });

  server.on("/enviar", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("comando") && request->hasParam("telefono")) {
      String comando = request->getParam("comando")->value();
      String telefono = request->getParam("telefono")->value();

      Serial.println("Recibido comando: " + comando);
      Serial.println("Recibido telefono: " + telefono);

      ComandoWeb cmd;
      comando.toCharArray(cmd.nombre, sizeof(cmd.nombre));
      telefono.toCharArray(cmd.telefono, sizeof(cmd.telefono));
      if (xQueueSend(comandoQueue, &cmd, 100) != pdTRUE) {
        Serial.println("Cola de comandos llena, no se pudo enviar");
      }

      String nombreOriginal = comando;
      nombreOriginal.replace("_", " ");
      request->send(200, "text/plain", "Bienvenido Vecino: " + nombreOriginal);
    } else {
      request->send(400, "text/plain", "Parametros 'comando' o 'telefono' faltantes");
    }
  });

// ...existing code...

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
            width: 93%;
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
        <form class="wifi-form" action='/Guardar-Wifi' method='POST'>
          <h2>Registrar Red WiFi</h2>
          <label for="ssid">SSID:</label>
          <input type='text' id='ssid' name='ssid' required autocomplete="off">
          <label for="password">Contraseña:</label>
          <input type='password' id='password' name='password' required autocomplete="off">
          <input type='submit' value='Guardar'>
        </form>
      </body>
      </html>
    )rawliteral";
    request->send(200, "text/html", html);
  });

// ...existing code...

  server.on("/Guardar-Wifi", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("ssid", true) && request->hasParam("password", true)) {
      String ssid = request->getParam("ssid", true)->value();
      String password = request->getParam("password", true)->value();

      File f = SPIFFS.open("/wifi.txt", FILE_WRITE);
      if (f) {
        f.println(ssid);
        f.println(password);
        f.close();
        request->send(200, "text/html", "<h3>Datos guardados. Reinicie el dispositivo.</h3>");
      } else {
        request->send(500, "text/plain", "Error guardando datos");
      }
    } else {
      request->send(400, "text/plain", "Faltan parámetros");
    }
  });

  server.onNotFound([](AsyncWebServerRequest *request) {
    if (!SPIFFS.exists("/interfaz.html.gz")) {
      request->send(404, "text/plain", "Archivo principal no encontrado");
      return;
    }
    auto resp = request->beginResponse(SPIFFS, "/interfaz.html.gz", "text/html");
    resp->addHeader("Content-Encoding", "gzip");
    request->send(resp);
  });

  server.begin();
  Serial.println("Servidor HTTP iniciado");

  xTaskCreate(wifiTask, "WiFi Task", 6144, NULL, 1, NULL);          // 6 KB
  xTaskCreate(enviarComandoWebTask, "HTTP Task", 8192, NULL, 1, NULL);  // 12 KB
}

// ==================== LOOP ====================
void loop() {
  dnsServer.processNextRequest();
  unsigned long currentMillis = millis();
  if (!ledState && currentMillis - previousMillis >= offTime) {
    ledState = true;
    previousMillis = currentMillis;
    digitalWrite(LED_PIN, HIGH);
  } else if (ledState && currentMillis - previousMillis >= onTime) {
    ledState = false;
    previousMillis = currentMillis;
    digitalWrite(LED_PIN, LOW);
  }
}