#include <Arduino.h>
#include <heltec.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Ticker.h>

IPAddress local_IP(192, 168, 8, 28);
IPAddress gateway(192, 168, 8, 1);
IPAddress subnet(255, 255, 255, 0);
const char* ssidAP = "PARQUES AZCAPOTZALCO";

AsyncWebServer server(80);

const int LED_PIN = 35;
unsigned long previousMillis = 0;
const long onTime = 500;
const long offTime = 2000;
bool ledState = false;

void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Heltec.begin(true, false, true);

  Heltec.display->clear();
  Heltec.display->setTextAlignment(TEXT_ALIGN_CENTER);
  Heltec.display->setFont(ArialMT_Plain_10);
  Heltec.display->drawString(64, 27, "AZCAPOTZALCO");
  Heltec.display->display();

  digitalWrite(LED_PIN, HIGH);
  delay(200);
  digitalWrite(LED_PIN, LOW);

  if (!SPIFFS.begin(true)) {
    Serial.println("Error al montar SPIFFS");
    return;
  }

  WiFi.mode(WIFI_AP);
  if (!WiFi.softAPConfig(local_IP, gateway, subnet)) {
    Serial.println("Error al configurar IP estática");
    return;
  }

  if (!WiFi.softAP(ssidAP)) {
    Serial.println("Error al iniciar el AP");
    return;
  }

  Serial.println("Punto de acceso creado con IP: " + WiFi.softAPIP().toString());

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!SPIFFS.exists("/interfaz.html.gz")) {
      request->send(404, "text/plain", "Archivo no encontrado");
      return;
    }
    auto response = request->beginResponse(SPIFFS, "/interfaz.html.gz", "text/html");
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
  });

  server.begin();
  Serial.println("Servidor iniciado");
}

void loop() {
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
