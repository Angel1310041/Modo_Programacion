#include <Arduino.h>
#include <heltec.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

IPAddress local_IP(192, 168, 1, 1);
IPAddress gateway(192, 168, 8, 1);
IPAddress subnet(255, 255, 255, 0);
const char* ssidAP = "PARQUES AZCAPOTZALCO";

AsyncWebServer server(80);

const int LED_PIN = 35;
unsigned long previousMillis = 0;
const long onTime = 500;
const long offTime = 2000;
bool ledState = false;

// Estado para saber si el usuario ya se registró
bool userRegistered = false;

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

  // Redirigir todas las peticiones GET a "/" si no está registrado
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!userRegistered) {
      request->redirect("/registro");
      return;
    }
    if (!SPIFFS.exists("/interfaz.html.gz")) {
      request->send(404, "text/plain", "Archivo principal no encontrado");
      return;
    }
    auto response = request->beginResponse(SPIFFS, "/interfaz.html.gz", "text/html");
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
  });

  // Mostrar formulario de registro (comprimido)
  server.on("/registro", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!SPIFFS.exists("/registro.html.gz")) {
      request->send(404, "text/plain", "Formulario de registro no disponible");
      return;
    }
    auto response = request->beginResponse(SPIFFS, "/registro.html.gz", "text/html");
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
  });

  // Procesar envío de formulario de registro con redirección automática
  server.on("/registro", HTTP_POST, [](AsyncWebServerRequest *request) {
    // Aquí puedes validar parámetros si quieres
    userRegistered = true;

    request->send(200, "text/html",
      "<meta http-equiv='refresh' content='2;url=/' />"
      "<h2>Gracias por registrarte. Redirigiendo...</h2>");
  });

  // Capturar cualquier otra petición no manejada
  server.onNotFound([](AsyncWebServerRequest *request) {
    if (!userRegistered) {
      request->redirect("/registro");
    } else {
      request->send(404, "text/plain", "Página no encontrada");
    }
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
