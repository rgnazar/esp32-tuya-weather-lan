#include "wu_client.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "config.h"
#include "tls_setup.h"

namespace {

const char kUploadUrl[] =
    "https://weatherstation.wunderground.com/weatherstation/updateweatherstation.php";

// Acrescenta o parametro so quando a estacao reportou aquele sensor. Enviar um
// campo zerado faria o Weather Underground registrar um dado falso.
void addParam(String& url, const char* name, const Value& value, int decimals) {
  if (!value.has) return;
  url += "&";
  url += name;
  url += "=";
  url += formatNumber(value.v, decimals);  // ver o porque em readings.h
}

}  // namespace

namespace WuClient {

bool publish(const Readings& r) {
  if (!isPublishable(r)) {
    Serial.println("[wu] leituras incompletas, publicacao suprimida");
    return false;
  }

  // dateutc=now faz o proprio Weather Underground carimbar a hora, o que evita
  // que um relogio local em fuso errado desloque o ponto no grafico.
  String url = String(kUploadUrl) + "?ID=" + WU_STATION_ID +
               "&PASSWORD=" + WU_STATION_KEY +
               "&dateutc=now";

  addParam(url, "tempf",          r.tempF,          1);
  addParam(url, "humidity",       r.humidity,       0);
  addParam(url, "dewptf",         r.dewPointF,      1);
  addParam(url, "baromin",        r.baromIn,        2);
  addParam(url, "windspeedmph",   r.windMph,        1);
  addParam(url, "windgustmph",    r.gustMph,        1);
  addParam(url, "rainin",         r.rainIn,         2);
  addParam(url, "dailyrainin",    r.dailyRainIn,    2);
  addParam(url, "monthlyrainin",  r.monthlyRainIn,  2);
  addParam(url, "UV",             r.uv,             0);
  addParam(url, "solarradiation", r.solarRadiation, 0);
  addParam(url, "indoortempf",    r.indoorTempF,    1);
  addParam(url, "indoorhumidity", r.indoorHumidity, 0);

  // Com o anemometro parado a direcao fica congelada no ultimo valor lido, o
  // que nao representa nada: so publicamos winddir quando ha vento de fato.
  const bool windBlowing = r.windMph.has && r.windMph.v >= WIND_DIR_MIN_MPH;
  if (windBlowing) addParam(url, "winddir", r.windDirDeg, 0);

  url += "&action=updateraw&softwaretype=ESP32-EstacaoLocal";

  WiFiClientSecure secure;
  applyTlsSettings(secure);

  HTTPClient http;
  if (!http.begin(secure, url)) {
    Serial.println("[wu] falha ao iniciar conexao");
    return false;
  }

  const int status = http.GET();
  if (status <= 0) {
    // errorToString() reporta "connection refused" para qualquer falha de
    // conexao, inclusive erro de certificado. O motivo real vem do mbedTLS.
    char tlsError[128] = {0};
    secure.lastError(tlsError, sizeof(tlsError));
    Serial.printf("[wu] erro HTTP: %s | tls: %s\n",
                  http.errorToString(status).c_str(),
                  tlsError[0] ? tlsError : "(sem erro de TLS registrado)");
    http.end();
    return false;
  }

  String body = http.getString();
  http.end();
  body.trim();

  if (status == 200 && body.indexOf("success") >= 0) {
    Serial.printf("[wu] publicado%s\n",
                  windBlowing && r.windDirDeg.has ? " (com direcao do vento)" : "");
    return true;
  }

  Serial.printf("[wu] recusado (HTTP %d): %s\n", status, body.c_str());
  return false;
}

}  // namespace WuClient
