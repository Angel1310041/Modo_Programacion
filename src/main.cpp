#include <Arduino.h>
#include <heltec.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include "esp_task_wdt.h"

// ====================
// CONFIGURACIÓN AP
// ====================
IPAddress local_IP(192, 168, 1, 1);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
const char* ssidAP = "PARQUES AZCAPOTZALCO";

// ====================
// CONFIGURACIÓN STA (Internet)
// ====================
const char* ssidSTA = "OFICINAABM";      // <-- PON TU RED AQUÍ
const char* passwordSTA = "T3mp0r4lABM123"; // <-- PON TU CONTRASEÑA AQUÍ

// ====================
// SERVIDOR Y DNS
// ====================
AsyncWebServer server(80);
DNSServer dnsServer;

// ====================
// LED
// ====================
const int LED_PIN = 35;
unsigned long previousMillis = 0;
const long onTime = 500;
const long offTime = 2000;
bool ledState = false;

// ====================
// VARIABLES TARJETA / ENVÍO WEB
// ====================
struct Tarjeta {
  String servidorComandos = "alarmasvecinales.online"; // <-- IP o dominio real de tu servidor remoto
  String tarjetaID = "SV33333333";                // <-- ID de tarjeta
} tarjeta;

String ultimoComandoWeb = "";
bool comandoF2Enviado = true; // usa tu lógica real

// ====================
// URL de envío remoto
// ====================
String getSendURL() {
  return "http://" + tarjeta.servidorComandos + "/MiAlarma3.0/Ejemplos/Envio.php?id=" + tarjeta.tarjetaID + "&Comando=";
}

// ====================
// Función auxiliar
// ==================== 
void imprimirSerial(String texto) {
  Serial.println(texto);
  Heltec.display->clear();
  Heltec.display->setTextAlignment(TEXT_ALIGN_CENTER);
  Heltec.display->drawString(64, 25, texto);
  Heltec.display->display();
}

// ====================
// Envía comando por HTTP a servidor remoto
// ====================
void enviarComandoWeb(String comandoEnv) {
  if (WiFi.status() != WL_CONNECTED) {
    imprimirSerial("No hay conexión a Internet: " + comandoEnv);
    return;
  }

  esp_task_wdt_reset();
  HTTPClient http;
  String SEND_URL = getSendURL() + comandoEnv;

  http.setTimeout(5000);
  http.setReuse(true);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  WiFiClient client;
  client.setTimeout(5000);

  const int maxReintentos = 3;

  for (int reintentos = 0; reintentos < maxReintentos; reintentos++) {
    http.begin(SEND_URL);
    int sendHttpCode = http.GET();

    if (sendHttpCode > 0 && WiFi.status() == WL_CONNECTED) {
      imprimirSerial("Comando enviado: " + comandoEnv);
      if (comandoF2Enviado) {
        ultimoComandoWeb = comandoEnv;
      }
      http.end();
      return;
    } else {
      imprimirSerial("Error: " + comandoEnv + " -> " + http.errorToString(sendHttpCode));
      vTaskDelay(200 / portTICK_PERIOD_MS);
    }

    http.end();
  }
  imprimirSerial("No se pudo enviar después de 3 intentos.");
}

// ====================
// SETUP
// ====================
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

  // === Modo AP + STA ===
  WiFi.mode(WIFI_AP_STA);

  // Configura AP
  if (!WiFi.softAPConfig(local_IP, gateway, subnet)) {
    Serial.println("Error IP estática AP");
    return;
  }
  if (!WiFi.softAP(ssidAP)) {
    Serial.println("Error iniciar AP");
    return;
  }
  Serial.println("AP iniciado en IP: " + WiFi.softAPIP().toString());

  // Conecta STA
  WiFi.begin(ssidSTA, passwordSTA);
  Serial.print("Conectando a WiFi STA...");
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    Serial.print(".");
    delay(500);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("Conectado STA en IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("");
    Serial.println("No se pudo conectar STA (Internet)");
  }

  // DNS
  dnsServer.start(53, "*", local_IP);

  // Página principal
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!SPIFFS.exists("/interfaz.html.gz")) {
      request->send(404, "text/plain", "Archivo principal no encontrado");
      return;
    }
    auto resp = request->beginResponse(SPIFFS, "/interfaz.html.gz", "text/html");
    resp->addHeader("Content-Encoding", "gzip");
    request->send(resp);
  });

  // Ruta para recibir comandos desde JS
  server.on("/enviar", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("comando")) {
      String comando = request->getParam("comando")->value();
      enviarComandoWeb(comando);
      request->send(200, "text/plain", "Comando enviado: " + comando);
    } else {
      request->send(400, "text/plain", "Parametro 'comando' faltante");
    }
  });

  // Not Found: siempre sirve la interfaz
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
}

// ====================
// LOOP
// ====================
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
