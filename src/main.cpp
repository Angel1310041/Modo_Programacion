#include <Arduino.h>
#include <heltec.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include "esp_task_wdt.h"
//C:\Users\angel\.platformio\packages\toolchain-xtensa-esp32s3\bin\xtensa-esp32s3-elf-addr2line.exe -pfiaC -e .pio/build/esp32dev/firmware.elf Backtrace: 0x403771e2:0x3fcf3d60 0x4037c49d:0x3fcf3d80 0x40382511:0x3fcf3da0 0x42029cba:0x3fcf3ed0 0x42029d4e:0x3fcf3ef0 0x4208cdaa:0x3fcf3f10 0x42026da9:0x3fcf3f30
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
const char* ssidSTA = "OFICINAABM";
const char* passwordSTA = "T3mp0r4lABM123";

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
  String servidorComandos = "alarmasvecinales.online";
  String tarjetaID = "SV33333333";
} tarjeta;

String ultimoComandoWeb = "";
bool comandoF2Enviado = true;

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
  // YA NO toca el display, solo Serial.
}

// ====================
// Envía comando por HTTP a servidor remoto
// ====================
// Ahora recibe nombre + telefono
String urlEncode(const String& str) {
  String encoded = "";
  char c;
  char code0, code1;
  for (int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
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

void enviarComandoWeb(String comandoEnv, String telefonoEnv) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("No hay conexión a Internet: " + comandoEnv);
    return;
  }

  // Elimina espacios y concatena nombre_telefono en Comando
  telefonoEnv.replace(" ", "");
  String comandoCompleto = comandoEnv + "_" + telefonoEnv;
  String SEND_URL = getSendURL() + urlEncode(comandoCompleto);
  Serial.println("URL enviada: " + SEND_URL);

  const int maxReintentos = 3;

  for (int reintentos = 0; reintentos < maxReintentos; reintentos++) {
    HTTPClient http; // 🔑 Crear dentro del bucle para evitar reutilización corrupta
    http.setTimeout(5000);
    http.setReuse(false); // 👈 Importante: NO reutilices conexiones fallidas
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    int sendHttpCode = -1;

    if (http.begin(SEND_URL)) {
      sendHttpCode = http.GET();
    }

    if (sendHttpCode > 0 && WiFi.status() == WL_CONNECTED) {
      Serial.println("Comando enviado: " + comandoEnv + ", Telefono: " + telefonoEnv);
      if (comandoF2Enviado) {
        ultimoComandoWeb = comandoEnv;
      }
      http.end();
      return;
    } else {
      Serial.println("Error: " + comandoEnv + " -> " + http.errorToString(sendHttpCode));
      http.end();
      vTaskDelay(200 / portTICK_PERIOD_MS);
    }
  }

  Serial.println("No se pudo enviar después de 3 intentos.");
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

  dnsServer.start(53, "*", local_IP);

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

    // 💡 Ahora pásalos los 2:
    enviarComandoWeb(comando, telefono);

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
