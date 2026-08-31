// =============================================================================
//  EstacaoLocal - le a estacao meteorologica DIRETO NA REDE LOCAL, pelo
//                 protocolo Tuya 3.4, e publica no Weather Underground.
//
//  Placa: ESP32 DevKit v1 (WROOM-32) - selecione "ESP32 Dev Module" na IDE.
//  Biblioteca a instalar: ArduinoJson (v7).
//
//  Antes de compilar: copie config.example.h para config.h e preencha.
//  Protocolo e mapa de DPs: docs/PROTOCOLO-E-DESCOBERTAS.md
//
//  O modelo aqui e' de PUSH, nao de polling: a estacao envia os sensores
//  espontaneamente a cada ~30 s. O loop apenas escuta, acumula estado e publica
//  em intervalos regulares.
// =============================================================================

#include <WiFi.h>
#include <time.h>

#include "config.h"
#include "readings.h"
#include "tuya_lan.h"
#include "wu_client.h"

namespace {

Readings station;

uint32_t nextPublishAtMs = 0;
uint32_t nextReconnectAtMs = 0;
uint32_t messagesSincePublish = 0;

void blink(int times, int onMs) {
  if (STATUS_LED_PIN < 0) return;
  for (int i = 0; i < times; i++) {
    digitalWrite(STATUS_LED_PIN, HIGH);
    delay(onMs);
    digitalWrite(STATUS_LED_PIN, LOW);
    delay(onMs);
  }
}

bool ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  Serial.printf("[wifi] conectando em %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[wifi] nao conectou");
    return false;
  }
  Serial.printf("[wifi] ok, IP %s\n", WiFi.localIP().toString().c_str());
  return true;
}

// O protocolo local nao depende do relogio, e o Weather Underground carimba a
// hora sozinho (dateutc=now). Aqui a hora serve so para o log.
void syncClock() {
  configTime(0, 0, NTP_SERVER);
  for (int i = 0; i < 20 && time(nullptr) < 1700000000; i++) delay(250);
}

bool wasConnected = false;

void ensureStation() {
  if (TuyaLan::connected()) {
    wasConnected = true;
    return;
  }

  // Distingue "ainda nao conectou" de "a estacao nos derrubou": sem isso o log
  // so mostra a reconexao, e a queda passa despercebida.
  if (wasConnected) {
    Serial.printf("[lan] conexao caiu (%lu s desde a ultima mensagem)\n",
                  TuyaLan::sinceLastMessage() / 1000UL);
    wasConnected = false;
  }

  if (int32_t(millis() - nextReconnectAtMs) < 0) return;

  if (!TuyaLan::connect()) {
    nextReconnectAtMs = millis() + RECONNECT_DELAY_S * 1000UL;
    return;
  }

  // A consulta semeia os valores. Ela NAO devolve os DPs do tipo Raw (entre
  // eles a direcao do vento), que chegam depois, espontaneamente.
  TuyaLan::requestSnapshot();
}

void publishNow() {
  expireStale(station, VALUE_MAX_AGE_S * 1000UL);

  Serial.printf("[estado] %s\n", describe(station).c_str());
  Serial.printf("[estado] %u mensagens desde a ultima publicacao\n",
                unsigned(messagesSincePublish));
  messagesSincePublish = 0;

  const bool ok = WuClient::publish(station);
  blink(ok ? 1 : 3, ok ? 80 : 400);
}

}  // namespace

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(200);
  Serial.println("\n=== EstacaoLocal ===");

  if (STATUS_LED_PIN >= 0) {
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);
  }

  ensureWifi();
  syncClock();

  // Primeira publicacao so depois de acumular algumas mensagens: a estacao
  // manda um DP por vez, e publicar de imediato enviaria um retrato incompleto.
  nextPublishAtMs = millis() + 45000UL;
}

void loop() {
  if (!ensureWifi()) {
    delay(1000);
    return;
  }

  ensureStation();

  if (TuyaLan::connected()) {
    TuyaLan::keepAlive();

    String json;
    while (TuyaLan::nextMessage(json)) {
#if LOG_RAW_MESSAGES
      Serial.printf("[msg] %s\n", json.c_str());
#endif
      const int applied = applyMessage(json, station);
      if (applied > 0) messagesSincePublish++;
    }

    // Silencio prolongado significa conexao morta sem FIN - acontece quando a
    // estacao aceita outra conexao local (ela so atende uma por vez).
    if (TuyaLan::sinceLastMessage() > STALE_TIMEOUT_S * 1000UL) {
      Serial.println("[lan] sem mensagens ha muito tempo, reconectando");
      TuyaLan::disconnect();
      nextReconnectAtMs = millis() + RECONNECT_DELAY_S * 1000UL;
    }
  }

  // Comparacao por diferenca com sinal: continua correta quando millis()
  // transborda, o que acontece depois de ~49 dias ligado.
  if (int32_t(millis() - nextPublishAtMs) >= 0) {
    publishNow();
    nextPublishAtMs = millis() + UPDATE_INTERVAL_S * 1000UL;
  }

  delay(20);
}
