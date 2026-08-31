// Configuracao de TLS do cliente HTTPS.
//
// So o Weather Underground usa TLS aqui: a leitura da estacao e' local e nao
// cifra por certificado (o protocolo Tuya usa AES com chave compartilhada).

#ifndef TLS_SETUP_H
#define TLS_SETUP_H

#include <WiFiClientSecure.h>

#include "certificates.h"
#include "config.h"

inline void applyTlsSettings(WiFiClientSecure& secure) {
#if TLS_ALLOW_INSECURE
  Serial.println("[tls] AVISO: validacao de certificado DESLIGADA");
  secure.setInsecure();
#else
  secure.setCACert(ROOT_CA_BUNDLE);
#endif
}

#endif  // TLS_SETUP_H
