#include <WiFi.h>
#include <esp_now.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

#define WLAN_SSID "iPhone"
#define WLAN_PASS "Felipe27"
#define AIO_USERNAME "SEU_USERNAME_AQUI" // ← Substitua pelo seu username do Adafruit IO
#define AIO_KEY      "SUA_CHAVE_AQUI" // ← Substitua pela sua chave do Adafruit IO
#define INTERVALO_PUBLICACAO_MS 600
#define FILA_TAMANHO 10

#pragma pack(push, 1)
typedef struct {
  char     nome[20];
  float    temperatura;
  float    umidade;
  uint32_t contador;
} DataPacket;

typedef struct {
  char nome[20];
  uint32_t contador;
} AckPacket;
#pragma pack(pop)

WiFiClient client;
Adafruit_MQTT_Client mqtt(&client, "io.adafruit.com", 1883, AIO_USERNAME, AIO_KEY);
Adafruit_MQTT_Publish tempFeed(&mqtt, AIO_USERNAME "/feeds/Temperatura");
Adafruit_MQTT_Publish umidFeed(&mqtt, AIO_USERNAME "/feeds/Umidade");

DataPacket fila[FILA_TAMANHO];
volatile int filaInicio = 0;
volatile int filaFim    = 0;

bool filaVazia() {
  return filaInicio == filaFim;
}

void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(DataPacket)) {
    Serial.println("[ESP-NOW] Pacote com tamanho invalido, ignorado.");
    return;
  }

  DataPacket pkt;
  memcpy(&pkt, data, sizeof(pkt));

  if (strcmp(pkt.nome, "EQUIPE03") != 0) {
    Serial.printf("[ESP-NOW] Ignorado (nome: %s)\n", pkt.nome);
    return;
  }

  int prox = (filaFim + 1) % FILA_TAMANHO;
  if (prox == filaInicio) {
    Serial.println("[ESP-NOW] Fila cheia, pacote descartado.");
    return;
  }
  fila[filaFim] = pkt;
  filaFim = prox;

  // ← Envia ACK de volta para o sender
  AckPacket ack;
  strcpy(ack.nome, "GATEWAY");
  ack.contador = pkt.contador;
  esp_now_send(info->src_addr, (uint8_t*)&ack, sizeof(ack));
  Serial.printf("[ACK] Enviado — contador %u confirmado\n", pkt.contador);
}

void conectarWiFi() {
  Serial.printf("[WiFi] Conectando a \"%s\"...", WLAN_SSID);
  WiFi.begin(WLAN_SSID, WLAN_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.printf("[WiFi] Conectado! IP: %s\n", WiFi.localIP().toString().c_str());
}

void conectarMQTT() {
  if (mqtt.connected()) return;
  Serial.print("[MQTT] Conectando ao Adafruit IO... ");
  uint8_t tentativas = 5;
  int8_t ret;
  while ((ret = mqtt.connect()) != 0) {
    Serial.println(mqtt.connectErrorString(ret));
    mqtt.disconnect();
    delay(3000);
    if (--tentativas == 0) {
      Serial.println("[MQTT] Falha. Tentando novamente no proximo ciclo.");
      return;
    }
  }
  Serial.println("conectado!");
}

void processarPacote(const DataPacket& pkt) {
  Serial.println("======== Pacote recebido ========");
  Serial.printf("Nome       : %s\n",    pkt.nome);
  Serial.printf("Contador   : %u\n",    pkt.contador);
  Serial.printf("Temperatura: %.1f C\n", pkt.temperatura);
  Serial.printf("Umidade    : %.1f %%\n", pkt.umidade);
  Serial.println("=================================");
  conectarMQTT();
  if (tempFeed.publish(pkt.temperatura)) {
    Serial.printf("[MQTT] -> temperatura = %.1f\n", pkt.temperatura);
  } else {
    Serial.println("[MQTT] Falha ao publicar temperatura.");
  }
  delay(INTERVALO_PUBLICACAO_MS);
  conectarMQTT();
  if (umidFeed.publish(pkt.umidade)) {
    Serial.printf("[MQTT] -> umidade = %.1f\n", pkt.umidade);
  } else {
    Serial.println("[MQTT] Falha ao publicar umidade.");
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[INICIO] Gateway ESP32");
  conectarWiFi();
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] Erro ao iniciar!");
    return;
  }
  esp_now_register_recv_cb(onReceive);
  Serial.println("[ESP-NOW] Pronto para receber.");
  conectarMQTT();
}

void loop() {
  conectarMQTT();
  mqtt.processPackets(10);
  if (!mqtt.ping()) {
    mqtt.disconnect();
  }
  while (!filaVazia()) {
    processarPacote(fila[filaInicio]);
    filaInicio = (filaInicio + 1) % FILA_TAMANHO;
  }
  delay(50);
}