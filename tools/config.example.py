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
DEVICE_ID = "SEU_DEVICE_ID"
LOCAL_KEY = "SUA_LOCAL_KEY"

# Endereco na rede local. Descubra com tools/tuya_lan_identify.ps1, que decifra
# os broadcasts UDP 6667 e mostra o gwId de cada IP.
HOST = "192.168.1.100"
PORT = 6668

PROTOCOL_VERSION = "3.4"

TIMEOUT_S = 8
