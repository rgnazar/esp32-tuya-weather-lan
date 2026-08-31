#include "readings.h"

#include <ArduinoJson.h>
#include <ctype.h>
#include <math.h>
#include <mbedtls/base64.h>

#include "config.h"

namespace {

enum Unit {
  UNIT_CELSIUS,  // decimos de grau C -> Fahrenheit
  UNIT_KMH,      // decimos de km/h   -> mph
  UNIT_MM,       // decimos de mm     -> polegadas
  UNIT_MM_RAW,   // decimos de mm     -> mm (so log)
  UNIT_DIRECT,   // sem conversao (%, indice UV, W/m2, minutos)
};

struct DpMapping {
  const char*      id;     // numero do DP, como string (chave do JSON)
  float            scale;  // divisor aplicado ao inteiro da estacao
  Unit             unit;
  Value Readings::*field;
};

// Acrescentar um sensor = acrescentar uma linha aqui.
// O mapa de numeros esta em docs/PROTOCOLO-E-DESCOBERTAS.md, secao 4.
const DpMapping kMappings[] = {
    {DP_TEMP_OUTDOOR,     10, UNIT_CELSIUS, &Readings::tempF},
    {DP_HUMIDITY_OUTDOOR,  1, UNIT_DIRECT,  &Readings::humidity},
    {DP_DEW_POINT,        10, UNIT_CELSIUS, &Readings::dewPointF},
    {DP_TEMP_INDOOR,      10, UNIT_CELSIUS, &Readings::indoorTempF},
    {DP_HUMIDITY_INDOOR,   1, UNIT_DIRECT,  &Readings::indoorHumidity},
    {DP_WIND_AVG,         10, UNIT_KMH,     &Readings::windMph},
    {DP_WIND_GUST,        10, UNIT_KMH,     &Readings::gustMph},
    {DP_RAIN_1H,          10, UNIT_MM,      &Readings::rainIn},
    {DP_RAIN_24H,         10, UNIT_MM,      &Readings::dailyRainIn},
    {DP_RAIN_MONTH,       10, UNIT_MM,      &Readings::monthlyRainIn},
    {DP_RAIN_EVENT,       10, UNIT_MM_RAW,  &Readings::rainEventMm},
    {DP_UV_INDEX,          1, UNIT_DIRECT,  &Readings::uv},
    {DP_LIGHT,             1, UNIT_DIRECT,  &Readings::solarRadiation},
    {DP_SUNLIGHT_TIME,     1, UNIT_DIRECT,  &Readings::sunlightMin},
    {DP_BATTERY,           1, UNIT_DIRECT,  &Readings::batteryPct},
};

// Grandezas sujeitas a expiracao, com nome legivel. Sao as que a consulta
// periodica reconfirma: se uma delas some da consulta, sumiu de verdade.
//
// A direcao do vento fica DE FORA de proposito. Ela e' um DP Raw e nao volta na
// consulta, entao nao ha como distinguir "direcao constante" de "sensor morto" -
// e uma direcao constante e' informacao legitima, nao dado velho. A protecao
// contra anemometro morto vem de graca: a direcao so e' publicada quando ha
// vento, e a velocidade do vento essa sim expira.
struct NamedValue {
  const char*   name;
  Value Readings::*field;
};

const NamedValue kAllValues[] = {
    {"temperatura",       &Readings::tempF},
    {"umidade",           &Readings::humidity},
    {"ponto de orvalho",  &Readings::dewPointF},
    {"temperatura int.",  &Readings::indoorTempF},
    {"umidade int.",      &Readings::indoorHumidity},
    {"pressao",           &Readings::rawPressureHpa},
    {"pressao reduzida",  &Readings::baromIn},
    {"vento",             &Readings::windMph},
    {"rajada",            &Readings::gustMph},
    {"chuva 1h",          &Readings::rainIn},
    {"chuva 24h",         &Readings::dailyRainIn},
    {"chuva do mes",      &Readings::monthlyRainIn},
    {"indice UV",         &Readings::uv},
    {"radiacao solar",    &Readings::solarRadiation},
    {"bateria",           &Readings::batteryPct},
    {"evento de chuva",   &Readings::rainEventMm},
    {"minutos de sol",    &Readings::sunlightMin},
};

float applyUnit(Unit unit, float value) {
  switch (unit) {
    case UNIT_CELSIUS: return value * 9.0f / 5.0f + 32.0f;
    case UNIT_KMH:     return value / 1.609344f;
    case UNIT_MM:      return value / 25.4f;
    default:           return value;
  }
}

// A direcao do vento chega num DP do tipo Raw, em base64, que so aparece nas
// mensagens espontaneas. O payload tem sempre 9 bytes e traz o angulo como
// texto ASCII alinhado a direita, seguido do byte 0xB0 (o simbolo de grau):
//
//    20 graus -> 00 00 '2' '0' B0 00 14 00 00
//     8 graus -> 00 00 00  '8' B0 00 08 00 00
//   171 graus -> 00 '1' '7' '1' B0 00 AB 00 00
//
// Lemos o texto, nao o byte binario: o numero de digitos varia (o que desloca
// os campos) e o byte de 8 bits estouraria acima de 255 graus.
bool decodeWindDirection(const char* b64, float& degrees) {
  if (b64 == nullptr) return false;

  unsigned char raw[32];
  size_t rawLen = 0;
  if (mbedtls_base64_decode(raw, sizeof(raw), &rawLen,
                            reinterpret_cast<const unsigned char*>(b64),
                            strlen(b64)) != 0) {
    return false;
  }

  for (size_t i = 0; i < rawLen; i++) {
    if (raw[i] != 0xB0) continue;

    size_t start = i;
    while (start > 0 && isdigit(raw[start - 1])) start--;
    if (start == i) return false;

    char text[8] = {0};
    const size_t n = min(i - start, sizeof(text) - 1);
    memcpy(text, raw + start, n);

    degrees = atof(text);
    return degrees >= 0 && degrees < 360;
  }

  return false;
}

bool applyDp(const char* id, JsonVariantConst value, Readings& r) {
  if (strcmp(id, DP_WIND_DIRECTION) == 0) {
    float degrees = 0;
    if (!decodeWindDirection(value.as<const char*>(), degrees)) return false;
    r.windDirDeg.set(degrees);
    return true;
  }

  if (strcmp(id, DP_PRESSURE) == 0) {
    r.rawPressureHpa.set(value.as<float>());
    return true;
  }

  for (const DpMapping& m : kMappings) {
    if (strcmp(id, m.id) != 0) continue;

    const int raw = value.as<int>();
    if (strcmp(id, DP_TEMP_OUTDOOR) == 0) r.rawOutdoorTemp = raw;

    // A estacao reporta o valor sentinela quando perde contato com um sensor de
    // temperatura. Publicar isso gravaria -60 C no historico do PWS.
    if (m.unit == UNIT_CELSIUS && raw == DP_TEMP_INVALID) {
      Serial.printf("[dp] %s sem sinal, descartado\n", id);
      return false;
    }

    (r.*(m.field)).set(applyUnit(m.unit, raw / m.scale));
    return true;
  }

  return false;  // DP desconhecido
}

}  // namespace

float deciCelsiusToF(int deciCelsius) {
  return applyUnit(UNIT_CELSIUS, deciCelsius / 10.0f);
}

float stationHpaToSeaLevelInHg(float hPa, float outdoorCelsius, int altitudeM) {
  // Formula barometrica padrao, gradiente termico de 0,0065 K/m.
  const float lapse = 0.0065f * altitudeM;
  const float ratio = 1.0f - lapse / (outdoorCelsius + lapse + 273.15f);
  const float seaLevelHpa = hPa * powf(ratio, -5.257f);

  return seaLevelHpa * 0.02952998751f;  // hPa -> inHg
}

String formatNumber(float value, int decimals) {
  String formatted = String(value, decimals);
  formatted.trim();
  return formatted;
}

int applyMessage(const String& json, Readings& r) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) return 0;

  // Resposta de consulta:  {"dps": {...}}
  // Mensagem espontanea:   {"protocol":4,"t":...,"data":{"dps":{...}}}
  JsonObjectConst dps = doc["dps"].as<JsonObjectConst>();
  if (dps.isNull()) dps = doc["data"]["dps"].as<JsonObjectConst>();
  if (dps.isNull()) return 0;

  int applied = 0;
  for (JsonPairConst entry : dps) {
    if (applyDp(entry.key().c_str(), entry.value(), r)) applied++;
  }

  // A reducao ao nivel do mar depende da temperatura externa, que pode ter
  // chegado numa mensagem anterior ou posterior - por isso recalculamos sempre
  // que houver as duas pecas. Sem temperatura, nao publicamos pressao: um valor
  // errado e' pior que um campo ausente.
  if (r.rawPressureHpa.has && r.tempF.has) {
    const float outdoorC = (r.tempF.v - 32.0f) * 5.0f / 9.0f;
    r.baromIn.set(stationHpaToSeaLevelInHg(r.rawPressureHpa.v, outdoorC,
                                           STATION_ALTITUDE_M));

    // A pressao publicada nao pode se dizer mais nova que a mais velha das duas
    // entradas: senao ela sobreviveria a expiracao de qualquer uma delas, ja
    // que e' recalculada a cada mensagem.
    const Value& older =
        r.rawPressureHpa.ageMs() > r.tempF.ageMs() ? r.rawPressureHpa : r.tempF;
    r.baromIn.atMs = older.atMs;
  }

  return applied;
}

int expireStale(Readings& r, uint32_t maxAgeMs) {
  int expired = 0;

  for (const NamedValue& nv : kAllValues) {
    Value& value = r.*(nv.field);
    if (!value.has || value.ageMs() <= maxAgeMs) continue;

    Serial.printf("[expira] %s sem atualizacao ha %lu s, descartado\n",
                  nv.name, static_cast<unsigned long>(value.ageMs() / 1000UL));
    value.has = false;
    expired++;
  }

  return expired;
}

bool isPublishable(const Readings& r) {
  return r.tempF.has && r.humidity.has;
}

String describe(const Readings& r) {
  String s;
  s.reserve(260);

  // Nenhum campo e' garantido: a expiracao pode ter esvaziado qualquer um deles.
  if (r.tempF.has) {
    s += "temp=" + formatNumber(r.tempF.v, 1) + "F(cru " +
         String(r.rawOutdoorTemp) + ")";
  }
  if (r.humidity.has)       s += " umid=" + formatNumber(r.humidity.v, 0) + "%";
  if (r.dewPointF.has)      s += " orvalho=" + formatNumber(r.dewPointF.v, 1) + "F";
  if (r.baromIn.has)        s += " pressao=" + formatNumber(r.baromIn.v, 2) + "inHg";
  if (r.windMph.has)        s += " vento=" + formatNumber(r.windMph.v, 1) + "mph";
  if (r.windDirDeg.has)     s += " dir=" + formatNumber(r.windDirDeg.v, 0) + "deg";
  if (r.gustMph.has)        s += " rajada=" + formatNumber(r.gustMph.v, 1) + "mph";
  if (r.rainIn.has)         s += " chuva1h=" + formatNumber(r.rainIn.v, 2) + "in";
  if (r.dailyRainIn.has)    s += " chuva24h=" + formatNumber(r.dailyRainIn.v, 2) + "in";
  if (r.monthlyRainIn.has)  s += " chuvames=" + formatNumber(r.monthlyRainIn.v, 2) + "in";
  if (r.uv.has)             s += " uv=" + formatNumber(r.uv.v, 0);
  if (r.solarRadiation.has) s += " sol=" + formatNumber(r.solarRadiation.v, 0) + "W/m2";
  if (r.indoorTempF.has)    s += " int=" + formatNumber(r.indoorTempF.v, 1) + "F";
  if (r.indoorHumidity.has) s += "/" + formatNumber(r.indoorHumidity.v, 0) + "%";
  if (r.batteryPct.has)     s += " bat=" + formatNumber(r.batteryPct.v, 0) + "%";

  s.trim();
  return s.length() > 0 ? s : String("(nenhuma leitura valida)");
}
