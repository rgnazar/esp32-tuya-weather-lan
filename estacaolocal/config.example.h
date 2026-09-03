// =============================================================================
//  EstacaoLocal - Arquivo central de configuracao
//
//  Le a estacao meteorologica DIRETO NA REDE LOCAL (protocolo Tuya 3.4) e
//  publica no Weather Underground. Nao usa a nuvem Tuya.
// =============================================================================
//
//  COMO USAR
//  1. Copie este arquivo para "config.h" na mesma pasta.
//  2. Preencha os valores abaixo.
//  3. Compile e grave o sketch.
//
//  "config.h" contem SENHAS e esta no .gitignore. NUNCA o envie para um
//  repositorio publico. Este "config.example.h" e' o unico versionado.
//
//  O protocolo e o mapa de DPs estao documentados em
//  docs/PROTOCOLO-E-DESCOBERTAS.md.
// =============================================================================

#ifndef CONFIG_H
#define CONFIG_H

// -----------------------------------------------------------------------------
//  1. REDE WI-FI
// -----------------------------------------------------------------------------
//  O ESP32 so enxerga redes de 2,4 GHz. A estacao precisa estar na MESMA rede.

#define WIFI_SSID       "NOME_DA_SUA_REDE"
#define WIFI_PASSWORD   "SENHA_DA_SUA_REDE"

// -----------------------------------------------------------------------------
//  2. A ESTACAO NA REDE LOCAL
// -----------------------------------------------------------------------------
//  STATION_IP:  IP da estacao na sua LAN.
//  LOCAL_KEY:   chave local do dispositivo, obtida na Tuya Cloud em
//               GET /v1.0/devices/{device_id} (campo "local_key").
//
//  Para descobrir o IP e confirmar qual dispositivo e' a estacao, rode:
//      powershell -File tools/tuya_lan_identify.ps1
//  Ele decifra os broadcasts UDP 6667 e mostra o gwId de cada IP. Isso importa
//  quando ha varios dispositivos Tuya na rede - adivinhar pelo IP da errado.
//
//  Reserve o IP no DHCP do roteador: se ele mudar, o firmware para de ler.

#define STATION_IP      "192.168.1.100"
#define STATION_PORT    6668
#define LOCAL_KEY       "SUA_LOCAL_KEY"

// -----------------------------------------------------------------------------
//  3. WEATHER UNDERGROUND (PWS)
// -----------------------------------------------------------------------------
//  My Profile > My Devices em wunderground.com.
//  STATION_KEY e' a chave gerada pelo site, NAO a senha da sua conta.

#define WU_STATION_ID   "SEU_STATION_ID"
#define WU_STATION_KEY  "SUA_STATION_KEY"

// -----------------------------------------------------------------------------
//  4. LOCAL DA ESTACAO
// -----------------------------------------------------------------------------
//  Altitude em metros. A estacao mede pressao ABSOLUTA no local; o Weather
//  Underground espera a pressao reduzida ao nivel do mar. Sem a altitude
//  correta, a pressao publicada fica errada.

#define STATION_ALTITUDE_M   0

// -----------------------------------------------------------------------------
//  5. MAPEAMENTO DOS DPs
// -----------------------------------------------------------------------------
//  Na leitura local os sensores sao identificados por NUMERO, nao por nome.
//  Os valores abaixo sao do modelo EM3395TY-2 (categoria "qxj"). Se a sua
//  estacao for outro modelo, os numeros mudam - a secao 4 de
//  docs/PROTOCOLO-E-DESCOBERTAS.md explica como redescobri-los.

#define DP_TEMP_OUTDOOR      "38"    // decimos de grau C
#define DP_HUMIDITY_OUTDOOR  "39"    // % inteiro
#define DP_DEW_POINT         "64"    // decimos de grau C
#define DP_TEMP_INDOOR       "1"     // decimos de grau C
#define DP_HUMIDITY_INDOOR   "2"     // % inteiro
#define DP_PRESSURE          "54"    // hPa absoluto, inteiro
#define DP_WIND_AVG          "56"    // decimos de km/h
#define DP_WIND_GUST         "57"    // decimos de km/h
#define DP_RAIN_1H           "59"    // decimos de mm
#define DP_RAIN_24H          "60"    // decimos de mm
#define DP_RAIN_MONTH        "138"   // decimos de mm  (nao declarado na spec)
#define DP_RAIN_EVENT        "127"   // decimos de mm  (nao declarado; so log)
#define DP_UV_INDEX          "62"    // indice inteiro
#define DP_LIGHT             "135"   // decimos de W/m2 (nao declarado na spec)
#define DP_SUNLIGHT_TIME     "137"   // minutos (nao declarado; so log)
#define DP_BATTERY           "4"     // % (so log)

//  Direcao do vento: DP do tipo Raw, em base64. Nao vem na consulta - chega
//  espontaneamente a cada ~30s. Ver a secao 5 do documento do protocolo.
#define DP_WIND_DIRECTION    "134"

//  Valor que os DPs de temperatura reportam quando o sensor esta sem sinal.
#define DP_TEMP_INVALID      (-600)

// -----------------------------------------------------------------------------
//  6. AJUSTES DE OPERACAO
// -----------------------------------------------------------------------------

//  Intervalo entre publicacoes no Weather Underground, em segundos.
//  Diferente da versao que usava a nuvem, aqui NAO ha cota de API: a estacao
//  empurra os dados sozinha a cada ~30s, um sensor por mensagem, e o firmware
//  acumula tudo. A publicacao envia a ULTIMA leitura de cada sensor, entao
//  espacar as publicacoes nao perde dado nenhum - so evita sobrecarregar o WU.
//  Faixa recomendada: 180 a 300 s.
#define UPDATE_INTERVAL_S    240

//  Intervalo entre consultas completas a estacao, em segundos.
//  A estacao so empurra um DP quando ele MUDA - umidade parada em 98% pode
//  ficar horas sem aparecer. Ja a consulta devolve todos os DPs numericos de uma
//  vez, tenham mudado ou nao. E' ela que mantem o estado comprovadamente vivo e
//  que da sentido a VALUE_MAX_AGE_S abaixo. Deve ser bem menor que ela.
#define REFRESH_INTERVAL_S   120

//  Validade de uma leitura, em segundos. Como o estado e' acumulativo, um sensor
//  que parasse de reportar - pilha do modulo externo acabando, por exemplo -
//  continuaria sendo publicado com o ultimo valor, como se fosse atual. Passado
//  este tempo sem atualizacao, a grandeza e' descartada e deixa de ser enviada.
//  Com a reconsulta acima, isto so dispara quando a estacao realmente para de
//  responder - o equivalente a varias consultas seguidas sem resposta.
#define VALUE_MAX_AGE_S      900

//  Heartbeat do protocolo local. Sem ele o dispositivo encerra a conexao.
#define HEARTBEAT_INTERVAL_S 10

//  Reconexao ao dispositivo apos queda.
#define RECONNECT_DELAY_S    15

//  Se nenhuma mensagem chegar neste tempo, a conexao e' considerada morta.
#define STALE_TIMEOUT_S      120

//  Vento minimo (mph) para publicar a direcao. Com o anemometro parado a
//  direcao fica congelada no ultimo valor e nao significa nada.
#define WIND_DIR_MIN_MPH     0.1f

//  Servidor NTP. Usado apenas para carimbar o log: o protocolo local nao depende
//  do relogio, e o Weather Underground carimba a hora sozinho (dateutc=now).
#define NTP_SERVER           "pool.ntp.org"

//  LED de status (GPIO2 na maioria das DevKit v1). -1 desabilita.
#define STATUS_LED_PIN       2

#define SERIAL_BAUD          115200

//  DIAGNOSTICO - mantenha em 0. Em 1, o ESP32 nao valida o certificado do
//  Weather Underground. Serve so para isolar problemas de cadeia. Nao deixe
//  ligado: a WU_STATION_KEY viaja em texto claro na query string.
#define TLS_ALLOW_INSECURE   0

//  Imprime cada mensagem recebida da estacao. Util para descobrir DPs novos.
#define LOG_RAW_MESSAGES     0

#endif  // CONFIG_H
