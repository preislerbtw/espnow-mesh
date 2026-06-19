#include <WiFi.h>
#include <esp_now.h>
#include <DHT.h>
#include <esp_wifi.h>

#define DHTPIN 18
#define DHTTYPE DHT11
#define TIMEOUT_ACK_MS 2000
#define MAX_TENTATIVAS 3

DHT dht(DHTPIN, DHTTYPE);

#pragma pack(push, 1)
typedef struct {
  char nome[20];
  float temperatura;
  float umidade;
  uint32_t contador;
} DataPacket;

typedef struct {
  char nome[20];
  uint32_t contador;
} AckPacket;
#pragma pack(pop)

DataPacket data_global = {};
uint8_t broadcastAddress[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

volatile bool ackRecebido = false;

void onSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  Serial.print("📤 ENVIO STATUS: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "OK" : "FALHA");
}

void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {

  // Recebeu um ACK
  if (len == sizeof(AckPacket)) {
    AckPacket ack;
    memcpy(&ack, data, sizeof(ack));
    Serial.printf("✅ ACK recebido de %s — contador %u confirmado\n", ack.nome, ack.contador);
    if (ack.contador == data_global.contador) {
      ackRecebido = true;
    }
    return;
  }

  // Recebeu dados de outras equipes
  if (len == sizeof(DataPacket)) {
    DataPacket pkt;
    memcpy(&pkt, data, sizeof(pkt));
    if (strcmp(pkt.nome, "EQUIPE03") == 0) return; // ignora o próprio
    Serial.println("======== Pacote recebido ========");
    Serial.printf("Nome       : %s\n",     pkt.nome);
    Serial.printf("Temperatura: %.1f C\n", pkt.temperatura);
    Serial.printf("Umidade    : %.1f %%\n", pkt.umidade);
    Serial.println("=================================");
  }
}

void setup() {
  Serial.begin(115200);

  dht.begin();
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ Erro ao iniciar ESP-NOW");
    return;
  }

  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onReceive); // ← novo

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, broadcastAddress, 6);
  peer.channel = 0;
  peer.encrypt = false;

  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("❌ Erro ao adicionar peer");
    return;
  }

  strcpy(data_global.nome, "EQUIPE03");
  Serial.println("🚀 ESP-NOW PRONTO");
}

void loop() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (isnan(t) || isnan(h)) {
    Serial.println("❌ ERRO DHT");
    delay(2000);
    return;
  }

  data_global.temperatura = round(t * 10) / 10.0;
  data_global.umidade     = round(h * 10) / 10.0;
  data_global.contador++;

  Serial.println("📡 ENVIANDO DADOS:");
  Serial.print("Nome: "); Serial.println(data_global.nome);
  Serial.print("Temp: "); Serial.println(data_global.temperatura, 1);
  Serial.print("Umid: "); Serial.println(data_global.umidade, 1);

  // Sistema ACK com reenvio
  ackRecebido = false;
  int tentativas = 0;

  while (!ackRecebido && tentativas < MAX_TENTATIVAS) {
    tentativas++;
    Serial.printf("📡 Tentativa %d de %d...\n", tentativas, MAX_TENTATIVAS);
    esp_now_send(broadcastAddress, (uint8_t*)&data_global, sizeof(data_global));

    unsigned long inicio = millis();
    while (!ackRecebido && millis() - inicio < TIMEOUT_ACK_MS) {
      delay(10);
    }
  }

  if (ackRecebido) {
    Serial.printf("✅ Pacote %u confirmado após %d tentativa(s)\n", data_global.contador, tentativas);
  } else {
    Serial.printf("❌ Pacote %u NÃO confirmado após %d tentativas\n", data_global.contador, tentativas);
  }

  delay(2000);
}