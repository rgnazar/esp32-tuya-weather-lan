// Cliente do protocolo Tuya local (LAN), versao 3.4.
//
// Mantem uma conexao TCP persistente com a estacao. O modelo e' de PUSH: apos a
// consulta inicial, o dispositivo envia mensagens espontaneas quando um sensor
// muda, a cada ~30 s. Nao ha polling nem cota de API.
//
// O protocolo esta documentado em docs/PROTOCOLO-E-DESCOBERTAS.md.

#ifndef TUYA_LAN_H
#define TUYA_LAN_H

#include <Arduino.h>

namespace TuyaLan {

// Abre a conexao e negocia a chave de sessao. Falha se o local_key estiver
// errado (o dispositivo autentica a negociacao com HMAC-SHA256).
bool connect();

bool connected();
void disconnect();

// Pede o estado atual de todos os DPs de valor. Serve para semear as leituras:
// a consulta NAO devolve os DPs do tipo Raw, que so chegam espontaneamente.
bool requestSnapshot();

// Devolve a proxima mensagem decifrada, se houver. Nao bloqueia.
bool nextMessage(String& json);

// Envia o heartbeat quando devido. Sem ele o dispositivo encerra a conexao.
void keepAlive();

// Milissegundos desde a ultima mensagem recebida - detecta conexao morta.
uint32_t sinceLastMessage();

}  // namespace TuyaLan

#endif  // TUYA_LAN_H
