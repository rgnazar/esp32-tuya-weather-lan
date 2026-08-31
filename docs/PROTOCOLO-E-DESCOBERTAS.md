# EstacaoLocal — protocolo Tuya LAN e tudo que foi descoberto

Documento de referência do projeto. Reúne o que só se descobriu experimentando,
com a estação real, e que não está em nenhuma documentação oficial.

Data das medições: 2026-08-31.

---

## 1. O dispositivo

Tudo neste documento foi levantado com a estação abaixo. Os valores próprios da
instalação — device ID, IP, chave, PWS, altitude — ficam em `config.h` e
`tools/config.py`, fora do controle de versão.

| Campo | Valor |
|---|---|
| Modelo | `EM3395TY-2` (vendida como FJ3395TY) |
| Categoria Tuya | `qxj` (estação meteorológica) |
| Porta | `6668` |
| Protocolo local | **3.4** |

O `local_key` fica em `estacaolocal/config.h` e `tools/config.py`, fora do
controle de versão. É a única coisa que se obtém pela internet, uma única vez:
`GET /v1.0/devices/{device_id}` no console de IoT da Tuya. Depois disso o
projeto funciona sem conexão externa alguma, exceto o envio ao Weather
Underground.

### Como redescobrir o IP se ele mudar

Rode `tools/tuya_lan_identify.ps1`. Dispositivos Tuya se anunciam por broadcast UDP na porta 6667, com payload
cifrado em AES-128-ECB usando uma chave pública comum a todo o ecossistema:

```
chave_udp = MD5("yGAdlopoPVldABfn")
```

Decifrando o broadcast obtém-se o `gwId`, que identifica a estação sem
ambiguidade. Isso importa: a rede onde o projeto foi desenvolvido tem **11
dispositivos Tuya**, e adivinhar pelo IP daria errado.

---

## 2. Protocolo Tuya 3.4

O 3.4 difere das versões anteriores em dois pontos que quebram qualquer
implementação escrita para 3.1/3.3:

1. Cada mensagem é autenticada com **HMAC-SHA256 (32 bytes)** no lugar do CRC32.
2. Antes de qualquer leitura é obrigatório **negociar uma chave de sessão**.

### Formato da mensagem

```
  0x000055AA        (4 bytes, big-endian)   prefixo
  seq               (4 bytes, big-endian)   número de sequência
  cmd               (4 bytes, big-endian)   comando
  len               (4 bytes, big-endian)   len(payload) + 32 + 4
  payload           (len - 36 bytes)        cifrado em AES-128-ECB
  hmac              (32 bytes)              HMAC-SHA256 do cabeçalho + payload
  0x0000AA55        (4 bytes, big-endian)   sufixo
```

Algumas respostas trazem um **código de retorno de 4 bytes** no início do
payload. Ele é sempre um inteiro pequeno, então o teste
`retcode & 0xFFFFFF00 == 0` o distingue de dados reais.

### Comandos usados

| Valor | Nome | Uso |
|---|---|---|
| `0x03` | `SESS_KEY_NEG_START` | envia o nosso nonce |
| `0x04` | `SESS_KEY_NEG_RESP` | recebe o nonce do dispositivo + HMAC |
| `0x05` | `SESS_KEY_NEG_FINISH` | confirma |
| `0x08` | `STATUS` | **mensagens espontâneas** do dispositivo |
| `0x09` | `HEART_BEAT` | mantém a conexão viva |
| `0x10` | `DP_QUERY_NEW` | consulta o estado atual |

### Negociação da chave de sessão

```
1. local_nonce = 16 bytes (usamos "0123456789abcdef", fixo e reproduzível)
   envia cmd 0x03, payload = AES-ECB(local_key, PKCS7(local_nonce))
   HMAC da mensagem usa local_key

2. recebe cmd 0x04
   plain = AES-ECB-decrypt(local_key, payload)     <- SEM remover padding
   remote_nonce = plain[0:16]
   remote_hmac  = plain[16:48]
   confere: remote_hmac == HMAC-SHA256(local_key, local_nonce)
   -> se não conferir, o local_key está errado

3. envia cmd 0x05,
   payload = AES-ECB(local_key, HMAC-SHA256(local_key, remote_nonce))  sem padding

4. session_key = AES-ECB(local_key, local_nonce XOR remote_nonce)      sem padding
```

A partir daí, **session_key é usada tanto para o AES quanto para o HMAC** de
todas as mensagens.

### Cabeçalho de protocolo

Comandos que carregam JSON de protocolo levam `b"3.4" + 12 bytes zero` antes do
payload. Os comandos que usamos (`DP_QUERY_NEW`, negociação, heartbeat) **não**
levam esse cabeçalho, mas algumas respostas vêm com ele — daí o parser removê-lo
quando o texto decifrado começa com `3.4`.

---

## 3. A descoberta que define a arquitetura

**`DP_QUERY` não devolve os DPs do tipo Raw.**

A consulta devolve 29 DPs de valor e enum. Nenhum Raw. Foi o que, num primeiro
momento, me fez concluir erradamente que a direção do vento não existia
localmente.

**Os DPs Raw chegam de forma assíncrona**, em mensagens `STATUS` (`cmd 0x08`)
não solicitadas, a cada ~30 segundos. E o formato delas é diferente do da
resposta de consulta:

```
resposta de consulta:   {"dps": {"38": 155}}
mensagem espontânea:    {"protocol":4,"t":1788199890,"data":{"dps":{"134":"..."}}}
```

Os DPs ficam aninhados em **`data.dps`**, não em `dps` no nível de cima. Ler o
lugar errado faz as mensagens parecerem vazias — foi exatamente o bug que
escondeu a descoberta na primeira tentativa.

**Consequência de projeto:** o firmware mantém a conexão TCP aberta e acumula
estado a partir das mensagens espontâneas, em vez de consultar periodicamente.
A consulta inicial serve só para semear os valores.

---

## 4. Mapa de DPs

Na LAN os sensores vêm identificados por **número** (`"38": 155`), sem nome.
Esta tabela é o resultado de casar esses números com os nomes que a Tuya usa,
lendo a mesma estação pelas duas vias no mesmo instante e comparando os valores.
As ambiguidades — vários sensores com o mesmo valor no momento da leitura —
foram resolvidas pela ordem crescente dos números de DP, que acompanha a ordem
das propriedades do dispositivo.

**Este mapeamento já está feito e não precisa ser refeito**, a menos que a
estação seja de outro modelo. Nesse caso, o caminho é rodar `tools/listen_raw.py`
para coletar os números e comparar com o que o aplicativo da Tuya mostra na tela,
sensor a sensor.

| DP | Nome do sensor | Unidade | Escala | Vai para o WU |
|---|---|---|---|---|
| `1` | `temp_current` | °C | ÷10 | `indoortempf` |
| `2` | `humidity_value` | % | ÷1 | `indoorhumidity` |
| `4` | `battery_percentage` | % | ÷1 | — (só log) |
| `9` | `temp_unit_convert` | enum | — | — |
| `10` | `windspeed_unit_convert` | enum | — | — |
| `11` | `pressure_unit_convert` | enum | — | — |
| `12` | `rain_unit_convert` | enum | — | — |
| `13` | `bright_unit_convert` | enum | — | — |
| `38` | `temp_current_external` | °C | ÷10 | `tempf` |
| `39` | `humidity_outdoor` | % | ÷1 | `humidity` |
| `54` | `atmospheric_pressture` | hPa | ÷1 | `baromin` (reduzida) |
| `55` | `pressure_drop` | hPa | ÷1 | — |
| `56` | `windspeed_avg` | km/h | ÷10 | `windspeedmph` |
| `57` | `windspeed_gust` | km/h | ÷10 | `windgustmph` |
| `59` | `rain_1h` | mm | ÷10 | `rainin` |
| `60` | `rain_24h` | mm | ÷10 | `dailyrainin` |
| `61` | `rain_rate` | mm | ÷10 | — |
| `62` | `uv_index` | — | ÷1 | `UV` |
| `64` | `dew_point_temp` | °C | ÷10 | `dewptf` |
| `65` | `feellike_temp` | °C | ÷10 | — (o WU calcula) |
| `66` | `heat_index` | °C | ÷10 | — (o WU calcula) |
| `101` | `Time_Format` | enum | — | — |
| `102` | `DM` | enum | — | — |
| `109` | `direc_format` | enum | — | — |
| `113` | `sensor_line` | **Raw** | — | — |
| `127` | `Rain_event` | mm | ÷10 | — (só log) |
| `131` | `Wind_speed` | ? | ? | — |
| `134` | **direção do vento** | graus | ver §5 | `winddir` |
| `135` | `Light_intensity` | W/m² | ÷1 | `solarradiation` |
| `137` | `sunlight_time` | min | ÷1 | — |
| `138` | `rain_month` | mm | ÷10 | `monthlyrainin` |

### Sensores que a `specification` não declara

`Light_intensity`, `rain_month`, `Rain_event`, `Time_Format`, `DM`, `Wind_speed`
e `sunlight_time` **não aparecem** na especificação publicada do dispositivo
(`GET /v1.0/iot-03/devices/{id}/specification`). Existem apenas na leitura real.
Escalas e unidades deles são **inferidas**, não confirmadas.

**`Light_intensity` é W/m², não klux.** O DP `bright_unit_convert` reporta
`klux`, mas a lista de funções admite apenas `wm2`, e a contradição se resolve
pela magnitude: os valores observados foram 163–235 numa tarde chuvosa. Como
klux seria fisicamente impossível (a luz solar plena fica em torno de 100 klux);
como W/m² é exatamente o esperado.

**`rain_month` = 650 → 65,0 mm**, por analogia com `rain_1h` e `rain_24h`, que
têm escala ÷10 confirmada.

### Sentinela de sensor ausente

Temperaturas reportam **`-600`** (−60,0 °C) quando o sensor correspondente está
sem sinal. Os canais `temp_current_external_1/2/3` ficam permanentemente nesse
valor porque não há sensores remotos extras pareados.

---

## 5. O DP 134 — direção do vento

DP do tipo **Raw**, entregue em base64. O fabricante o chama de
`Wing_direction` (com esse erro de digitação). O payload tem **sempre 9 bytes**:

```
 20° ->  00 00 32 30 B0 00 14 00 00     ASCII "20",  0x14 = 20
  8° ->  00 00 00 38 B0 00 08 00 00     ASCII  "8",  0x08 = 8
171° ->  00 31 37 31 B0 00 AB 00 00     ASCII "171", 0xAB = 171
```

Estrutura: o ângulo aparece como **texto ASCII alinhado à direita** nos índices
1–3, seguido do byte `0xB0` (o símbolo `°`) no índice 4, e repetido em binário
no índice 6. O DP `109` (`direc_format`) vale `angle`, confirmando a unidade.

**O parser lê o texto ASCII, não o byte binário.** Procura o marcador `0xB0` e
recolhe os dígitos para trás, parando no `0x00` do preenchimento. Duas razões:

1. O número de dígitos varia, o que desloca os campos — posição fixa quebraria.
2. O byte binário é de 8 bits e estouraria acima de 255°. O texto não tem
   esse limite.

Validado empiricamente com ângulos de 1, 2 e 3 dígitos.

**Regra de publicação:** `winddir` só vai para o Weather Underground quando o
vento medido atinge `WIND_DIR_MIN_MPH`. Com o anemômetro parado a direção fica
congelada no último valor e não significa nada; publicá-la distorceria a rosa
dos ventos do PWS.

---

## 6. Conversões para o Weather Underground

O WU aceita apenas unidades imperiais. A estação entrega tudo em métrico —
os DPs `*_unit_convert` mudam apenas o que o console mostra, **não** o que a API
devolve.

```
°F      = °C × 9/5 + 32
mph     = km/h / 1,609344
polegada = mm / 25,4
```

### Pressão

O WU espera `baromin` **reduzida ao nível do mar**; a estação entrega pressão
absoluta na altitude. Com a fórmula barométrica padrão (gradiente 0,0065 K/m),
altitude de 850 m e a temperatura externa do momento:

```
p_mar  = p_estacao × (1 − 0,0065·h / (T + 0,0065·h + 273,15)) ^ −5,257
baromin = p_mar × 0,02952998751
```

Conferência: 911 hPa a 850 m dão ≈1008 hPa ao nível do mar, o que confirma que
o DP é pressão de estação. A pressão só é publicada quando há temperatura
externa válida — sem ela a redução ficaria imprecisa, e pressão errada é pior
que pressão ausente.

---

## 7. Armadilhas já pagas

**`String(float, casas)` do Arduino insere espaço à esquerda.** Ele usa
`dtostrf` com largura mínima de `casas + 2`; com 0 casas, o valor `0` vira `" 0"`.
Numa query string isso vira `%20` e o Weather Underground responde **HTTP 400**.
Sempre passar por `formatNumber()`, que faz `trim()`.

**`HTTPClient::errorToString()` diz "connection refused" para qualquer falha**,
inclusive erro de certificado. O motivo real vem de
`WiFiClientSecure::lastError()`.

**O ESP32 não tem repositório de CAs.** O `certificates.h` embute as raízes.
Confirmado por Certificate Transparency: `*.wunderground.com` alterna entre
DigiCert, **Amazon RSA 2048 M04**, Sectigo e Let's Encrypt — por isso todas
essas raízes ficam embutidas.

**Antivírus com inspeção de HTTPS falsifica a cadeia.** Nesta máquina o Avast
intercepta TLS, então `openssl s_client` mostra o certificado do Avast, não o
real. Para descobrir o emissor verdadeiro, consulte:
`https://api.certspotter.com/v1/issuances?domain=<dominio>&include_subdomains=true&expand=issuer`

**`pip` falha nesta máquina** pelo mesmo motivo. A solução correta não é
desabilitar a verificação, e sim exportar a raiz do Avast do armazenamento do
Windows e usar `pip install --cert tools/avast_root.pem`.

**O heartbeat também vai cifrado.** No 3.4 *todo* payload é cifrado com a chave
de sessão, e o do heartbeat é vazio — logo ele vira um bloco inteiro de
preenchimento PKCS7 (16 bytes de `0x10`) cifrado, não um payload de tamanho
zero. Enviar zero bytes faz a estação encerrar a conexão cerca de 2 s depois do
primeiro heartbeat. O sintoma no log é uma reconexão a cada ~12 s
(`HEARTBEAT_INTERVAL_S` + o tempo de reconexão), com a negociação de sessão
sempre bem-sucedida — o que engana, porque os dados continuam chegando graças à
consulta que cada reconexão dispara.

**A estação aceita uma conexão local por vez.** Abrir uma segunda derruba a
primeira, e a reconexão imediata pode falhar com "dispositivo fechou a conexão".

**Gravar o ESP32 por `arduino-cli` falha nesta placa** com
`Wrong boot mode detected (0x13)`. Pela IDE do Arduino funciona. O fluxo de
trabalho é: o usuário compila e grava pela IDE, e a leitura do serial é feita
por `tools/serial_monitor.ps1 -Port COM3`.

---

## 8. Por que ler na LAN

A mesma estação pode ser lida pela API de nuvem do fabricante. Ler na rede local
custou o trabalho de decifrar o protocolo — documentado acima — e paga por isso:

- **Sem custo e sem cota.** Nada de plano, token ou limite de chamadas.
- **Sem dependência de internet** para a leitura. A única saída para fora é a
  publicação no Weather Underground.
- **Funciona com o fabricante fora do ar.** A estação e o ESP32 conversam
  diretamente.
- **Dados a cada ~30 s** em vez de por consulta periódica, e a estação avisa
  quando algo muda em vez de ser perguntada.

O modelo de push é o que molda o firmware: o estado é acumulativo, cada sensor
guarda a própria hora de chegada, e a publicação envia a última leitura de cada
um — descartando o que envelheceu além de `VALUE_MAX_AGE_S`.
