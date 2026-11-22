#include <Arduino.h>
#include <WiFi.h>
#include <BiaCameraUVC.h>

const char *ssid = "Rogerio 2.4";
const char *password = "36874980";
const char *SERVIDOR_IP = "192.168.18.22";
const int PORTA_VIDEO_PYTHON = 5005;

void setup() {
  Serial.begin(115200);
  delay(1000);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n[+] Wi-Fi conectado! IP: %s\n", WiFi.localIP().toString().c_str());

  // Inicia em Standby automatico: so liga a camera quando o Python abrir!
  camBia.begin(SERVIDOR_IP, PORTA_VIDEO_PYTHON);
}

void loop() {
  // Nada aqui! O proprio Python liga e desliga a camera no ESP32 automaticamente.
  delay(1000);
}