# EstacaoLocal

Firmware para ESP32 que lê uma estação meteorológica Tuya **direto na rede
local** e publica no [Weather Underground](https://www.wunderground.com/).

Não há nuvem no caminho da leitura: o ESP32 conversa com a estação por TCP na
própria LAN, falando o protocolo Tuya 3.4. A única conexão para fora da rede é
o envio ao Weather Underground.

Desenvolvido e testado com uma **FJ3395TY** (modelo `EM3395TY-2`, categoria
Tuya `qxj`).

## O que é publicado

Treze grandezas: temperatura, umidade e ponto de orvalho externos; temperatura e
umidade internos; pressão reduzida ao nível do mar; velocidade, rajada e direção
do vento; chuva na última hora, no dia e no mês; índice UV; radiação solar.

Três desses sensores não constam da especificação publicada do dispositivo e só
existem na leitura real — entre eles a **direção do vento**, que chega num campo
binário com formato próprio. A seção 5 do documento de protocolo explica como
foi decifrado.

## Como funciona

A estação **empurra** os dados: manda um sensor por vez, espontaneamente, a cada
~30 s. O firmware escuta e vai acumulando o estado; a cada 4 minutos publica a
última leitura de cada sensor.

Só que a estação empurra um sensor **apenas quando o valor muda** — umidade
parada em 98% pode ficar horas calada. Por isso o firmware também reconsulta a
estação a cada 2 minutos: a consulta devolve todos os sensores numéricos, tenham
mudado ou não.

Assim, **todas as grandezas vão ao PWS a cada 4 minutos**, tenham mudado ou não:
temperatura estável é informação meteorológica, não dado velho.

O que não é publicado é o que a estação **deixou de confirmar**. Cada leitura
carrega a hora da última confirmação; passados 15 minutos sem nenhuma, ela é
descartada — assim um sensor que morre (a pilha do módulo externo acabando, por
exemplo) some do PWS em vez de congelar o último valor como se ainda fosse atual.

## Instalação

Requisitos: **Arduino IDE** com suporte a ESP32 e a biblioteca **ArduinoJson v7**.
Placa: ESP32 DevKit v1 (WROOM-32) — selecione *ESP32 Dev Module*.

1. Copie `estacaolocal/config.example.h` para `estacaolocal/config.h`.
2. Preencha: Wi-Fi, IP e `local_key` da estação, credenciais do PWS e a
   **altitude de instalação** (usada na redução da pressão ao nível do mar).
   A seção seguinte mostra onde obter cada chave.
3. Abra `estacaolocal/estacaolocal.ino` na IDE, compile e grave — ou use
   `tools/gravar.ps1`, que faz o mesmo pela linha de comando com o `arduino-cli`
   embutido na IDE. Placas sem auto-reset confiável pedem a sequência do botão
   BOOT, descrita no cabeçalho do script.

O `config.h` contém senhas e está no `.gitignore`. `config.example.h` é o único
versionado, e cada parâmetro está comentado nele.

## Obtendo as chaves da estação

Para falar com a estação na LAN são necessários três valores: **IP**,
**device ID** e **local key**. Os dois últimos vêm do console de IoT da Tuya —
é a única vez em que a internet entra na leitura. Depois de anotados, o projeto
funciona sem a nuvem.

A `local_key` é a chave de criptografia do dispositivo. Quem a tem consegue
falar com a estação na rede local: trate-a como senha.

### 1. Pré-requisito: a estação já pareada no aplicativo

A estação precisa estar funcionando no **Smart Life** ou **Tuya Smart**, na
mesma rede de 2,4 GHz em que o ESP32 vai ficar. É o pareamento pelo aplicativo
que gera a `local_key`.

### 2. Criar um projeto no console de IoT

Em [iot.tuya.com](https://iot.tuya.com/), crie uma conta e depois
**Cloud → Development → Create Cloud Project**.

No formulário, o campo que costuma dar errado é o **Data Center**: ele tem que
ser o da região em que a *conta do aplicativo* foi registrada, não onde você
mora. Se errar, o projeto é criado normalmente mas a estação simplesmente não
aparece na lista de dispositivos. Para o Brasil, o data center é em geral
**Western America**.

Em **Service API**, confirme que `IoT Core` está habilitado.

### 3. Vincular a conta do aplicativo

No projeto criado, vá em **Devices → Link App Account → Add App Account** e leia
o QR code exibido com o próprio aplicativo (menu **Perfil → ícone de leitura de
QR code**, no canto superior).

Feito isso, a aba **Devices → All Devices** lista a estação. A coluna
**Device ID** é o valor de `TUYA_DEVICE_ID` / `DEVICE_ID`.

### 4. Pegar a local key

A `local_key` **não aparece na interface** do console — só pela API. Vá em
**Cloud → API Explorer**, escolha o projeto e o mesmo data center, e chame:

```
GET /v1.0/devices/{device_id}
```

A resposta traz, entre outros campos:

```json
{
  "result": {
    "id": "eb0000000000000000xxxx",
    "local_key": "A1b2C3d4E5f6G7h8",
    "product_name": "Weather Station",
    "ip": "189.x.x.x",
    "online": true
  }
}
```

Copie `local_key` para `LOCAL_KEY` no `config.h`. Ela tem 16 caracteres e
costuma incluir pontuação — copie exatamente, sem aparar nada.

O mesmo pode ser feito da linha de comando, sem abrir o navegador:

```
powershell -ExecutionPolicy Bypass -File tools/tuya_cloud_localkey.ps1
```

O script pede o **Access ID** e o **Access Secret** do projeto de nuvem
(*Cloud → Development → seu projeto → Overview → Authorization Key*) — ou os lê
de `tools/config.py`, junto com o device ID e o data center. O secret digitado
no prompt não aparece na tela nem no histórico do shell.

O campo `ip` dessa resposta é o **IP externo** da sua internet, não o da estação
na LAN. Para esse, veja o passo seguinte.

> **Readicionar a estação no aplicativo troca a `local_key` — e o device ID
> junto.** Se o firmware passar a registrar `HMAC do dispositivo nao confere`,
> é quase certo que foi isso.
>
> Refazer só a chave não basta, porque o device ID anotado deixa de existir: a
> consulta acima passa a devolver o erro **1106**, que a Tuya descreve como
> falta de permissão e por isso manda procurar no lugar errado. Comece
> listando os dispositivos do projeto:
>
> ```
> powershell -ExecutionPolicy Bypass -File tools/tuya_cloud_localkey.ps1 -List
> ```
>
> A listagem sai do endpoint `/v1.0/iot-01/associated-users/devices` e traz id,
> nome, categoria, estado e `local_key` de tudo que está vinculado ao projeto —
> a estação é a de categoria `qxj`. Anote o id e a chave novos, e confira o IP
> pelo passo 5: ele também costuma mudar.

### 5. Descobrir o IP na rede local

```
powershell -ExecutionPolicy Bypass -File tools/tuya_lan_identify.ps1
```

Dispositivos Tuya se anunciam por broadcast UDP na porta 6667, com o payload
cifrado por uma chave pública comum a todo o ecossistema. O script decifra esses
anúncios e mostra o `gwId` de cada IP — o `gwId` é o mesmo device ID do passo 3.

Isso importa: numa rede com vários dispositivos Tuya, adivinhar pelo IP dá
errado. A rede onde este projeto foi desenvolvido tem onze deles.

O script também testa a porta 6668. Se ela estiver fechada, a leitura local não
é possível naquele dispositivo.

Vale fixar o IP no DHCP do roteador, ou o endereço pode mudar depois de uma
queda de energia.

### E as credenciais do Weather Underground?

São outras, e nada têm a ver com a Tuya: saem de
[wunderground.com](https://www.wunderground.com/) → **My Profile → My Devices**,
onde cada PWS cadastrado mostra seu *Station ID* e *Station Key*.

## Estrutura

```
estacaolocal/          firmware
  estacaolocal.ino     ciclo principal: conexão, escuta, publicação
  tuya_lan.*           protocolo 3.4 — sessão, AES, HMAC, enquadramento
  readings.*           estado acumulativo, conversão de unidades, expiração
  wu_client.*          envio ao Weather Underground
  certificates.h       raízes de CA embutidas (gerado)
  config.example.h     modelo de configuração
docs/
  PROTOCOLO-E-DESCOBERTAS.md    referência completa do protocolo
tools/                 diagnóstico e manutenção
```

`docs/PROTOCOLO-E-DESCOBERTAS.md` é a fonte da verdade sobre o protocolo: o mapa
de DPs, o formato das mensagens, a negociação da chave de sessão, as conversões
e as armadilhas já pagas. Vale ler antes de mexer no firmware.

## Ferramentas

| Arquivo | Para quê |
|---|---|
| `tuya_lan_identify.ps1` | acha a estação na LAN pelos broadcasts UDP |
| `tuya_cloud_localkey.ps1` | busca a `local_key` na Tuya Cloud; `-List` mostra todos os dispositivos do projeto |
| `gravar.ps1` | compila e grava o firmware pela linha de comando |
| `serial_monitor.ps1` | lê o monitor serial (`-Port COM3 -Seconds 90`) |
| `gen_certificates.ps1` | regera `certificates.h` se o WU trocar de CA |
| `tuya_local.py` | leitura pontual da estação pelo PC |
| `listen_raw.py` | escuta as mensagens espontâneas, úteis para achar DPs novos |

Os scripts Python precisam de `pycryptodome` e de um `tools/config.py` (modelo em
`config.example.py`).

**A estação aceita uma conexão local por vez.** Rodar os scripts derruba o ESP32
até a reconexão automática dele.

## Diagnóstico

O serial em 115200 mostra o estado completo a cada publicação:

```
=== EstacaoLocal ===
[wifi] ok, IP 10.0.0.57
[lan] conectando em 10.0.0.22:6668
[lan] chave de sessao estabelecida
[estado] temp=60.6F(cru 159) umid=98% orvalho=59.9F pressao=29.59inHg ...
[estado] 27 mensagens desde a ultima publicacao
[wu] publicado
```

Sinais de que algo está errado:

- **Reconexões frequentes** — a estação encerra a conexão. Verifique se outro
  cliente (um script Python, o app) está falando com ela ao mesmo tempo.
- **`HMAC do dispositivo nao confere`** — `LOCAL_KEY` errada.
- **`[lan] conexao recusada`, repetidamente, com o IP certo e a porta 6668
  aberta** — suspeite de **isolamento de clientes** no ponto de acesso, não do
  firmware. Veja a seção seguinte.
- **`[wifi] nao conectou (status N)`** — o firmware varre o 2,4 GHz em seguida e
  lista as redes que enxerga, marcando a sua com `*`. Se o seu SSID **não
  aparece**, o problema não é a senha: a rede está só em 5 GHz (faixa que o
  ESP32 não enxerga), oculta, ou fora de alcance. Se aparece, olhe a senha e o
  modo de segurança.
- **`[expira] ... descartado`** — aquele sensor parou de reportar.
- **`[wu] recusado`** — o Weather Underground rejeitou; a resposta dele vem na
  mesma linha.

### Isolamento de clientes no ponto de acesso

Este projeto depende de dois clientes Wi-Fi conversarem entre si: o ESP32 e a
estação. Muitos pontos de acesso trazem ligado um recurso — *AP Isolation*,
*Client Isolation*, *Isolamento sem fio* — que bloqueia exatamente isso. O
sintoma é o ESP32 associar no Wi-Fi normalmente e então levar `conexao recusada`
em toda tentativa na porta 6668, indefinidamente.

Confunde porque parece problema de chave ou de IP, e não é. **O teste que
resolve a dúvida em um minuto:** rode `tools/tuya_local.py` de um PC ligado por
**cabo**, com o ESP32 ainda tentando. Se o PC conecta e negocia a sessão com a
mesma `local_key` e o mesmo IP que o ESP32 está usando, o que separa os dois é
só o caminho — e a diferença é o rádio. É isolamento.

A correção é no ponto de acesso, não no firmware: desligue o isolamento, ou
coloque os dois na mesma rede se um deles caiu num SSID de convidados ou numa
VLAN de IoT.

## Autoria

Projeto de um astrônomo amador brasileiro, desenvolvido em par com o
**Claude Opus 5** (Anthropic) através do **Claude Code**.

O protocolo Tuya 3.4 não é documentado publicamente pelo fabricante. Tudo o que
está em `docs/PROTOCOLO-E-DESCOBERTAS.md` — o formato das mensagens, a
negociação da chave de sessão, o mapa de DPs e a decodificação da direção do
vento — foi obtido experimentando com a estação real, e cada afirmação de lá foi
verificada em hardware antes de virar código.

## Licença

[MIT](LICENSE).
