// Estado dos sensores e conversao de unidades.
//
// As leituras chegam PICADAS: cada mensagem espontanea da estacao traz um ou
// dois DPs. Por isso o Readings e' acumulativo - vive entre mensagens e vai
// sendo preenchido, e a publicacao envia a ultima leitura de cada sensor.
//
// Este modulo nao toca em rede.

#ifndef READINGS_H
#define READINGS_H

#include <Arduino.h>

// Grandeza opcional: a estacao nem sempre reporta todos os sensores, e um
// sensor sem sinal deve ficar ausente em vez de virar zero.
struct Value {
  bool     has  = false;
  float    v    = 0;
  uint32_t atMs = 0;  // quando esta leitura chegou

  void set(float value) {
    v = value;
    has = true;
    atMs = millis();
  }

  // Subtracao sem sinal: continua correta quando millis() transborda.
  uint32_t ageMs() const { return millis() - atMs; }
};

struct Readings {
  // Externo
  Value tempF;
  Value humidity;
  Value dewPointF;

  // Interno
  Value indoorTempF;
  Value indoorHumidity;

  // Pressao ja reduzida ao nivel do mar, em polegadas de mercurio.
  Value baromIn;

  // Vento
  Value windMph;
  Value gustMph;
  Value windDirDeg;

  // Chuva
  Value rainIn;         // ultima hora
  Value dailyRainIn;    // dia
  Value monthlyRainIn;  // mes

  Value uv;
  Value solarRadiation;  // W/m2

  // So para o log - o Weather Underground nao tem campo para estes.
  Value batteryPct;
  Value rainEventMm;
  Value sunlightMin;

  // Pressao absoluta como a estacao reporta. Guardada porque a reducao ao
  // nivel do mar depende da temperatura externa, que pode chegar depois.
  Value rawPressureHpa;

  // Valor cru da temperatura externa, para conferencia no log.
  int rawOutdoorTemp = 0;
};

float deciCelsiusToF(int deciCelsius);
float stationHpaToSeaLevelInHg(float hPa, float outdoorCelsius, int altitudeM);

// Formata sem o espaco a esquerda que String(float, casas) insere: ele usa
// dtostrf com largura minima de (casas + 2), entao com 0 casas o "0" vira " 0".
// Um espaco na query string faz o Weather Underground responder HTTP 400.
String formatNumber(float value, int decimals);

// Aplica uma mensagem da estacao (resposta de consulta ou STATUS espontanea).
// Devolve quantos DPs conhecidos foram atualizados.
int applyMessage(const String& json, Readings& r);

// Descarta as leituras que passaram de maxAgeMs sem atualizacao. Sem isso, um
// sensor que parou de reportar - pilha do modulo externo acabando, por exemplo -
// continuaria sendo publicado com o ultimo valor, como se fosse atual.
// Devolve quantas foram descartadas.
int expireStale(Readings& r, uint32_t maxAgeMs);

// Ha o minimo necessario para publicar no Weather Underground?
bool isPublishable(const Readings& r);

// Resumo de uma linha para o Serial Monitor.
String describe(const Readings& r);

#endif  // READINGS_H
