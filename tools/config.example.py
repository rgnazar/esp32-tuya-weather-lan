"""Configuracao dos scripts de diagnostico.

Copie para config.py e preencha. O config.py esta no .gitignore.

Estes scripts sao a prova de conceito que validou o protocolo antes da porta
para o ESP32, e continuam uteis para diagnosticar a estacao a partir do PC:
tuya_local.py (leitura pontual) e listen_raw.py (mensagens espontaneas).

Atencao: a estacao aceita UMA conexao local por vez. Rodar estes scripts derruba
o ESP32 ate a proxima reconexao dele.
"""

# Identificacao do dispositivo. O LOCAL_KEY e' secreto: quem o tem consegue
# falar com o dispositivo na LAN.
#
# O DEVICE_ID tem 22 caracteres e comeca por "eb". Esta no console de IoT em
# Devices -> All Devices, coluna Device ID, e e' o mesmo gwId que o
# tuya_lan_identify.ps1 mostra para cada IP da rede. Conferir vale a pena: com
# o ID errado a nuvem responde "permission deny" (1106), que parece falta de
# permissao e nao engano de digitacao.
#
# O LOCAL_KEY tem 16 caracteres, costuma incluir pontuacao (copie sem aparar
# nada) e MUDA toda vez que a estacao e' repareada no aplicativo. Quando o
# firmware passar a registrar "HMAC do dispositivo nao confere", e' isso:
# tuya_cloud_localkey.ps1 busca a chave atual na nuvem.
DEVICE_ID = "SEU_DEVICE_ID"
LOCAL_KEY = "SUA_LOCAL_KEY"

# Endereco na rede local. Descubra com tools/tuya_lan_identify.ps1, que decifra
# os broadcasts UDP 6667 e mostra o gwId de cada IP.
HOST = "192.168.1.100"
PORT = 6668

PROTOCOL_VERSION = "3.4"

# Credenciais do projeto de nuvem da Tuya (Cloud -> Development -> projeto ->
# Overview -> Authorization Key). Usadas apenas por tools/tuya_cloud_localkey.ps1,
# que busca a local_key na nuvem quando ela muda - a leitura da estacao nao
# passa por aqui. Deixe em branco para digitar no prompt do script.
TUYA_ACCESS_ID = ""
TUYA_ACCESS_SECRET = ""

# Data center do projeto de nuvem: us (Western America, o usual no Brasil),
# us-e, eu, eu-w, cn ou in. Tem que ser o mesmo escolhido ao criar o projeto.
TUYA_REGION = "us"

TIMEOUT_S = 8
