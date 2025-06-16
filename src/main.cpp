#include <Arduino.h>
#include <heltec.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>

// Config IP
IPAddress local_IP(192, 168, 1, 1);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
const char* ssidAP = "PARQUES AZCAPOTZALCO";

// Objetos servidor y DNS
AsyncWebServer server(80);
DNSServer dnsServer;

const int LED_PIN = 35;
unsigned long previousMillis = 0;
const long onTime = 500;
const long offTime = 2000;
bool ledState = false;

void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Mostrar texto en OLED
  Heltec.begin(true, false, true);
  Heltec.display->clear();
  Heltec.display->setTextAlignment(TEXT_ALIGN_CENTER);
  Heltec.display->setFont(ArialMT_Plain_16);
  Heltec.display->drawString(64, 25, "AZCAPOTZALCO");
  Heltec.display->display();

  // Blink inicial
  digitalWrite(LED_PIN, HIGH);
  delay(200);
  digitalWrite(LED_PIN, LOW);

  // Montar SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("Error al montar SPIFFS");
    return;
  }

  // Configurar AP
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAPConfig(local_IP, gateway, subnet)) {
    Serial.println("Error al configurar IP estática");
    return;
  }
  if (!WiFi.softAP(ssidAP)) {
    Serial.println("Error al iniciar AP");
    return;
  }
  Serial.println("AP iniciado en IP: " + WiFi.softAPIP().toString());

  // Iniciar DNS: responde todo a la IP local
  dnsServer.start(53, "*", local_IP);

  // Página principal: verifica cookie
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasHeader("Cookie") ||
        request->header("Cookie").indexOf("registered=true") == -1) {
      request->redirect("http://192.168.1.1/registro");
      return;
    }
    if (!SPIFFS.exists("/interfaz.html.gz")) {
      request->send(404, "text/plain", "Archivo principal no encontrado");
      return;
    }
    auto resp = request->beginResponse(SPIFFS, "/interfaz.html.gz", "text/html");
    resp->addHeader("Content-Encoding", "gzip");
    request->send(resp);
  });

  // Formulario registro GET
  server.on("/registro", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!SPIFFS.exists("/registro.html.gz")) {
      request->send(404, "text/plain", "Formulario no disponible");
      return;
    }
    auto resp = request->beginResponse(SPIFFS, "/registro.html.gz", "text/html");
    resp->addHeader("Content-Encoding", "gzip");
    request->send(resp);
  });

  // Formulario registro POST: responde con cookie y redirige a IP local
  server.on("/registro", HTTP_POST, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(200, "text/html",
      "<meta http-equiv='refresh' content='2;url=http://192.168.1.1/' />"
      "<h2>Gracias por registrarte. Redirigiendo...</h2>");
    response->addHeader("Set-Cookie", "registered=true; Path=/;");
    request->send(response);
  });

  // Captive portal: toda ruta desconocida → redirige a IP local
  server.onNotFound([](AsyncWebServerRequest *request) {
    if (!request->hasHeader("Cookie") ||
        request->header("Cookie").indexOf("registered=true") == -1) {
      request->redirect("http://192.168.1.1/registro");
    } else {
      request->redirect("http://192.168.1.1/");
    }
  });

  // Iniciar servidor HTTP
  server.begin();
  Serial.println("Servidor HTTP iniciado");
}

void loop() {
  // Mantener DNS operativo para captive portal
  dnsServer.processNextRequest();

  // LED parpadeo
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
