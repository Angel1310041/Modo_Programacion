/*#include <Arduino.h>
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
const char* ssidSTA = "OFICINAABM";
const char* passwordSTA = "T3mp0r4lABM123";

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
        WiFi.begin(ssidSTA, passwordSTA);
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

  WiFi.begin(ssidSTA, passwordSTA);
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
}*/
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
const char* ssidSTA = "ABMSOPORTE";
const char* passwordSTA = "T3mp0r4lABM123";

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
        WiFi.begin(ssidSTA, passwordSTA);
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

  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAPConfig(local_IP, gateway, subnet)) { Serial.println("Error IP estática AP"); return; }
  if (!WiFi.softAP(ssidAP)) { Serial.println("Error iniciar AP"); return; }
  Serial.println("AP iniciado en IP: " + WiFi.softAPIP().toString());

  WiFi.begin(ssidSTA, passwordSTA);
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) { delay(500); Serial.print("."); }
  if (WiFi.status() == WL_CONNECTED)
    Serial.println("\nSTA conectada en IP: " + WiFi.localIP().toString());
  else Serial.println("\nNo se pudo conectar STA");

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
    if (!SPIFFS.exists("/interfaz.html.gz")) return request->send(404, "text/plain", "Archivo no encontrado");
    auto resp = request->beginResponse(SPIFFS, "/interfaz.html.gz", "text/html");
    resp->addHeader("Content-Encoding", "gzip");
    request->send(resp);
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
