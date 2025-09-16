#include <WiFi.h>
#include <WiFiClient.h>
#include <HX711.h>
#include <DHT.h>

const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";

#define TS_HTTP_HOST     "api.thingspeak.com"
#define TS_HTTP_PORT     80
#define TS_WRITE_KEY     "8JHTITC5CKIDA6P0" 

#define PUBLISH_INTERVAL_MS 16000UL

#define DHT_PIN        15
#define DHTTYPE        DHT22
#define SOIL_ADC_PIN   34
#define HX711_DT_PIN   14
#define HX711_SCK_PIN  13   

DHT dht(DHT_PIN, DHTTYPE);
HX711 scale;

unsigned long lastPublish = 0;

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.print("Conectando ao WiFi: "); Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
    if (millis() - t0 > 15000) {
      Serial.println("\nTimeout WiFi, tentando de novo...");
      t0 = millis();
      WiFi.disconnect(true);
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
  }
  Serial.println();
  Serial.print("WiFi OK. IP: "); Serial.println(WiFi.localIP());
}

float readSoilMoisturePct() {
  int raw = analogRead(SOIL_ADC_PIN);    
  float pct = (raw / 4095.0f) * 100.0f; 
  if (pct < 0) pct = 0; if (pct > 100) pct = 100;
  return pct;
}

float readHardnessIndex() {
  long reading = scale.is_ready() ? scale.get_value(5) : 0; 
  if (reading < 0) reading = 0;
  float idx = (reading / 21000.0f) * 100.0f;
  if (idx > 100.0f) idx = 100.0f;
  return idx;
}

bool publishMetricsThingSpeakHTTP(float tempC, float humAirPct, float soilPct, float hardness) {
  WiFiClient client;

  if (!client.connect(TS_HTTP_HOST, TS_HTTP_PORT)) {
    Serial.println("Falha ao conectar ao ThingSpeak (HTTP).");
    return false;
  }

  String body = String("api_key=") + TS_WRITE_KEY +
                "&field1=" + String(tempC, 2) +
                "&field2=" + String(humAirPct, 2) +
                "&field3=" + String(soilPct, 2) +
                "&field4=" + String(hardness, 2) +
                "&status=online"; 

  uint32_t t0 = millis();
  while (client.connected() && !client.available() && millis() - t0 < 5000) {
    delay(10);
  }

  String resp;
  while (client.available()) {
    resp += client.readString();
  }
  client.stop();

  Serial.print("TS HTTP resp: "); Serial.println(resp);
  return (resp.indexOf("\r\n\r\n0") == -1 && !resp.endsWith("0\n"));
}

void publishMetrics() {
  float tempC = dht.readTemperature();
  float humAirPct = dht.readHumidity();
  if (isnan(tempC) || isnan(humAirPct)) {
    Serial.println("Leitura DHT falhou, tentando no próximo ciclo.");
    return;
  }

  float soilPct = readSoilMoisturePct();
  float hardness = readHardnessIndex();

  Serial.printf("Medidas -> T:%.2fC Ar:%.2f%% Solo:%.2f%% Hard:%.2f\n",
                tempC, humAirPct, soilPct, hardness);

  if (!publishMetricsThingSpeakHTTP(tempC, humAirPct, soilPct, hardness)) {
    Serial.println("Falha ao publicar no ThingSpeak (HTTP).");
  }
}

void setup() {
  Serial.begin(9600);
  delay(300);

  dht.begin();

  scale.begin(HX711_DT_PIN, HX711_SCK_PIN);
  scale.set_scale(1.0);
  scale.tare();

  connectWiFi();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();

  unsigned long now = millis();
  if (now - lastPublish >= PUBLISH_INTERVAL_MS) {
    lastPublish = now;
    publishMetrics();
  }
}
