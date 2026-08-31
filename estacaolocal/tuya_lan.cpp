#include "tuya_lan.h"

#include <WiFi.h>
#include <mbedtls/aes.h>
#include <mbedtls/md.h>

#include "config.h"

namespace {

const uint32_t PREFIX = 0x000055AAUL;
const uint32_t SUFFIX = 0x0000AA55UL;

const uint32_t CMD_SESS_KEY_NEG_START = 0x03;
const uint32_t CMD_SESS_KEY_NEG_RESP = 0x04;
const uint32_t CMD_SESS_KEY_NEG_FINISH = 0x05;
const uint32_t CMD_STATUS = 0x08;
const uint32_t CMD_HEART_BEAT = 0x09;
const uint32_t CMD_DP_QUERY_NEW = 0x10;

// Nonce fixo do nosso lado. Nao precisa ser aleatorio: ele so entra na derivacao
// da chave de sessao junto com o nonce do dispositivo, que muda a cada conexao.
const uint8_t LOCAL_NONCE[16] = {'0', '1', '2', '3', '4', '5', '6', '7',
                                 '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

const size_t RX_CAPACITY = 4096;
const size_t MSG_CAPACITY = 2048;

WiFiClient client;

uint8_t  sessionKey[16];
bool     hasSession = false;
uint32_t seqNumber = 1;

uint8_t  rxBuffer[RX_CAPACITY];
size_t   rxLength = 0;

uint32_t lastMessageMs = 0;
uint32_t lastBeatMs = 0;

void putBE32(uint8_t* dst, uint32_t value) {
  dst[0] = value >> 24;
  dst[1] = value >> 16;
  dst[2] = value >> 8;
  dst[3] = value;
}

uint32_t getBE32(const uint8_t* src) {
  return (uint32_t(src[0]) << 24) | (uint32_t(src[1]) << 16) |
         (uint32_t(src[2]) << 8) | uint32_t(src[3]);
}

void aesEcb(const uint8_t* key, const uint8_t* in, size_t len, uint8_t* out,
            bool encrypt) {
  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  if (encrypt) {
    mbedtls_aes_setkey_enc(&ctx, key, 128);
  } else {
    mbedtls_aes_setkey_dec(&ctx, key, 128);
  }
  for (size_t off = 0; off + 16 <= len; off += 16) {
    mbedtls_aes_crypt_ecb(&ctx,
                          encrypt ? MBEDTLS_AES_ENCRYPT : MBEDTLS_AES_DECRYPT,
                          in + off, out + off);
  }
  mbedtls_aes_free(&ctx);
}

void hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* data,
                size_t dataLen, uint8_t out[32]) {
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
  mbedtls_md_hmac_starts(&ctx, key, keyLen);
  mbedtls_md_hmac_update(&ctx, data, dataLen);
  mbedtls_md_hmac_finish(&ctx, out);
  mbedtls_md_free(&ctx);
}

const uint8_t* localKeyBytes() {
  return reinterpret_cast<const uint8_t*>(LOCAL_KEY);
}

// Monta e envia uma mensagem. O payload ja deve estar cifrado, quando for o caso.
bool sendMessage(uint32_t cmd, const uint8_t* payload, size_t payloadLen,
                 const uint8_t* hmacKey) {
  uint8_t frame[16 + 128];
  if (payloadLen > sizeof(frame) - 16) return false;

  putBE32(frame + 0, PREFIX);
  putBE32(frame + 4, seqNumber++);
  putBE32(frame + 8, cmd);
  putBE32(frame + 12, payloadLen + 32 + 4);
  if (payloadLen > 0) memcpy(frame + 16, payload, payloadLen);

  const size_t bodyLen = 16 + payloadLen;

  uint8_t digest[32];
  hmacSha256(hmacKey, 16, frame, bodyLen, digest);

  uint8_t tail[4];
  putBE32(tail, SUFFIX);

  return client.write(frame, bodyLen) == bodyLen &&
         client.write(digest, sizeof(digest)) == sizeof(digest) &&
         client.write(tail, sizeof(tail)) == sizeof(tail);
}

void drainSocket() {
  while (client.available() > 0 && rxLength < RX_CAPACITY) {
    int c = client.read();
    if (c < 0) break;
    rxBuffer[rxLength++] = uint8_t(c);
  }
  // Se o buffer encher sem uma mensagem valida, descartamos: e' lixo de sincronia.
  if (rxLength >= RX_CAPACITY) rxLength = 0;
}

// Extrai uma mensagem completa do buffer. Devolve false se ainda nao ha uma.
bool takeMessage(uint32_t& cmd, uint8_t* payload, size_t& payloadLen) {
  size_t start = 0;
  while (start + 16 <= rxLength && getBE32(rxBuffer + start) != PREFIX) start++;

  if (start + 16 > rxLength) {
    // Nao ha nem cabecalho: preserva um resto pequeno para o proximo pedaco.
    if (start > 0 && start < rxLength) {
      memmove(rxBuffer, rxBuffer + start, rxLength - start);
      rxLength -= start;
    } else if (start >= rxLength) {
      rxLength = 0;
    }
    return false;
  }

  cmd = getBE32(rxBuffer + start + 8);
  const uint32_t length = getBE32(rxBuffer + start + 12);
  const size_t total = 16 + length;

  if (length < 36 || total > RX_CAPACITY) {  // quadro impossivel: ressincroniza
    rxLength = 0;
    return false;
  }
  if (start + total > rxLength) return false;  // ainda incompleto

  const uint8_t* body = rxBuffer + start + 16;
  size_t bodyLen = length - 32 - 4;

  // Algumas respostas trazem um codigo de retorno de 4 bytes antes do payload.
  // Ele e' sempre um inteiro pequeno, o que o distingue de dados cifrados.
  if (bodyLen >= 4 && (getBE32(body) & 0xFFFFFF00UL) == 0) {
    body += 4;
    bodyLen -= 4;
  }

  payloadLen = bodyLen > MSG_CAPACITY ? 0 : bodyLen;
  if (payloadLen > 0) memcpy(payload, body, payloadLen);

  const size_t consumed = start + total;
  memmove(rxBuffer, rxBuffer + consumed, rxLength - consumed);
  rxLength -= consumed;
  return true;
}

bool waitForMessage(uint32_t& cmd, uint8_t* payload, size_t& payloadLen,
                    uint32_t timeoutMs) {
  const uint32_t deadline = millis() + timeoutMs;
  while (int32_t(millis() - deadline) < 0) {
    drainSocket();
    if (takeMessage(cmd, payload, payloadLen)) return true;
    if (!client.connected()) return false;
    delay(10);
  }
  return false;
}

bool negotiate() {
  uint8_t padded[32];
  memcpy(padded, LOCAL_NONCE, 16);
  memset(padded + 16, 16, 16);  // PKCS7: bloco inteiro de preenchimento

  uint8_t encrypted[32];
  aesEcb(localKeyBytes(), padded, sizeof(padded), encrypted, true);

  if (!sendMessage(CMD_SESS_KEY_NEG_START, encrypted, sizeof(encrypted),
                   localKeyBytes())) {
    Serial.println("[lan] falha ao enviar inicio da negociacao");
    return false;
  }

  uint32_t cmd = 0;
  uint8_t payload[MSG_CAPACITY];
  size_t payloadLen = 0;
  if (!waitForMessage(cmd, payload, payloadLen, 5000)) {
    Serial.println("[lan] sem resposta a negociacao");
    return false;
  }
  if (cmd != CMD_SESS_KEY_NEG_RESP || payloadLen < 48) {
    Serial.printf("[lan] resposta inesperada: cmd=0x%02x len=%u\n",
                  cmd, unsigned(payloadLen));
    return false;
  }

  uint8_t plain[MSG_CAPACITY];
  aesEcb(localKeyBytes(), payload, payloadLen & ~size_t(15), plain, false);

  const uint8_t* remoteNonce = plain;
  const uint8_t* remoteHmac = plain + 16;

  uint8_t expected[32];
  hmacSha256(localKeyBytes(), 16, LOCAL_NONCE, sizeof(LOCAL_NONCE), expected);
  if (memcmp(remoteHmac, expected, 32) != 0) {
    Serial.println("[lan] HMAC do dispositivo nao confere: LOCAL_KEY errada");
    return false;
  }

  uint8_t finish[32];
  hmacSha256(localKeyBytes(), 16, remoteNonce, 16, finish);
  uint8_t finishEnc[32];
  aesEcb(localKeyBytes(), finish, sizeof(finish), finishEnc, true);
  if (!sendMessage(CMD_SESS_KEY_NEG_FINISH, finishEnc, sizeof(finishEnc),
                   localKeyBytes())) {
    return false;
  }

  uint8_t mixed[16];
  for (int i = 0; i < 16; i++) mixed[i] = LOCAL_NONCE[i] ^ remoteNonce[i];
  aesEcb(localKeyBytes(), mixed, sizeof(mixed), sessionKey, true);

  hasSession = true;
  Serial.println("[lan] chave de sessao estabelecida");
  return true;
}

}  // namespace

namespace TuyaLan {

bool connected() {
  return client.connected() && hasSession;
}

void disconnect() {
  client.stop();
  hasSession = false;
  rxLength = 0;
}

bool connect() {
  disconnect();
  seqNumber = 1;

  Serial.printf("[lan] conectando em %s:%d\n", STATION_IP, STATION_PORT);
  if (!client.connect(STATION_IP, STATION_PORT, 5000)) {
    Serial.println("[lan] conexao recusada");
    return false;
  }
  client.setNoDelay(true);

  if (!negotiate()) {
    disconnect();
    return false;
  }

  lastMessageMs = millis();
  lastBeatMs = millis();
  return true;
}

bool requestSnapshot() {
  if (!connected()) return false;

  // Payload "{}" com preenchimento PKCS7 ate 16 bytes.
  uint8_t plain[16] = {'{', '}'};
  memset(plain + 2, 14, 14);

  uint8_t encrypted[16];
  aesEcb(sessionKey, plain, sizeof(plain), encrypted, true);

  return sendMessage(CMD_DP_QUERY_NEW, encrypted, sizeof(encrypted), sessionKey);
}

void keepAlive() {
  if (!connected()) return;
  if (millis() - lastBeatMs < HEARTBEAT_INTERVAL_S * 1000UL) return;

  // No 3.4 todo payload vai cifrado, mesmo o do heartbeat, que e' vazio: ele
  // vira um bloco inteiro de preenchimento PKCS7. Enviar zero bytes faz a
  // estacao encerrar a conexao.
  uint8_t plain[16];
  memset(plain, 16, sizeof(plain));

  uint8_t encrypted[16];
  aesEcb(sessionKey, plain, sizeof(plain), encrypted, true);

  sendMessage(CMD_HEART_BEAT, encrypted, sizeof(encrypted), sessionKey);
  lastBeatMs = millis();
}

uint32_t sinceLastMessage() {
  return millis() - lastMessageMs;
}

bool nextMessage(String& json) {
  if (!client.connected()) return false;

  drainSocket();

  uint32_t cmd = 0;
  uint8_t payload[MSG_CAPACITY];
  size_t payloadLen = 0;
  if (!takeMessage(cmd, payload, payloadLen)) return false;

  lastMessageMs = millis();

  if (cmd == CMD_HEART_BEAT || payloadLen == 0) return false;

  const size_t blocks = payloadLen & ~size_t(15);
  if (blocks == 0) return false;

  static uint8_t plain[MSG_CAPACITY];
  aesEcb(sessionKey, payload, blocks, plain, false);

  size_t len = blocks;

  // Remove o preenchimento PKCS7.
  const uint8_t pad = plain[len - 1];
  if (pad > 0 && pad <= 16 && pad <= len) len -= pad;

  // Comandos com JSON de protocolo vem prefixados por "3.4" + 12 bytes zero.
  size_t offset = 0;
  if (len > 15 && memcmp(plain, "3.4", 3) == 0) offset = 15;

  if (offset >= len) return false;

  plain[len] = 0;
  json = reinterpret_cast<const char*>(plain + offset);
  json.trim();

  return json.length() > 0 && json[0] == '{';
}

}  // namespace TuyaLan
