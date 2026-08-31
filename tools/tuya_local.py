"""Le a estacao meteorologica pela LAN, sem passar pela nuvem Tuya.

Implementa o protocolo local Tuya 3.4, que difere dos anteriores em dois pontos:
  - toda mensagem e' autenticada com HMAC-SHA256 no lugar do CRC32;
  - antes de qualquer leitura e' preciso negociar uma chave de sessao, derivada
    de dois nonces (um nosso, um do dispositivo) cifrados com o local_key.

Uso:  python -u tuya_local.py
"""
import hashlib
import hmac
import json
import socket
import struct
import sys

from Crypto.Cipher import AES

import config

PREFIX = 0x000055AA
SUFFIX = 0x0000AA55

SESS_KEY_NEG_START = 0x03
SESS_KEY_NEG_RESP = 0x04
SESS_KEY_NEG_FINISH = 0x05
DP_QUERY = 0x0A
DP_QUERY_NEW = 0x10

# Nonce fixo do nosso lado. Nao precisa ser aleatorio para uma leitura de
# diagnostico, e um valor fixo torna o trafego reproduzivel na depuracao.
LOCAL_NONCE = b"0123456789abcdef"

# Cabecalho que o 3.4 insere em comandos que carregam JSON de protocolo.
PROTOCOL_34_HEADER = b"3.4" + b"\x00" * 12


def pkcs7_pad(data):
    n = 16 - (len(data) % 16)
    return data + bytes([n]) * n


def pkcs7_unpad(data):
    if not data:
        return data
    n = data[-1]
    return data[:-n] if 0 < n <= 16 and len(data) >= n else data


def aes_encrypt(key, data, pad=True):
    payload = pkcs7_pad(data) if pad else data
    return AES.new(key, AES.MODE_ECB).encrypt(payload)


def aes_decrypt(key, data, unpad=True):
    plain = AES.new(key, AES.MODE_ECB).decrypt(data)
    return pkcs7_unpad(plain) if unpad else plain


def pack(seq, cmd, payload, hmac_key):
    """Monta uma mensagem: cabecalho + payload + HMAC-SHA256 + sufixo."""
    header = struct.pack(">IIII", PREFIX, seq, cmd, len(payload) + 32 + 4)
    body = header + payload
    digest = hmac.new(hmac_key, body, hashlib.sha256).digest()
    return body + digest + struct.pack(">I", SUFFIX)


def unpack(data):
    """Extrai as mensagens de um buffer. Devolve [(cmd, payload_bruto)]."""
    messages = []
    while len(data) >= 24:
        start = data.find(struct.pack(">I", PREFIX))
        if start < 0:
            break
        data = data[start:]
        if len(data) < 16:
            break

        _, seq, cmd, length = struct.unpack(">IIII", data[:16])
        total = 16 + length
        if len(data) < total:
            break

        body = data[16:16 + length - 32 - 4]

        # Algumas respostas trazem um codigo de retorno de 4 bytes antes do
        # payload. Ele e' sempre um inteiro pequeno, o que o distingue de dados.
        if len(body) >= 4:
            retcode = struct.unpack(">I", body[:4])[0]
            if retcode & 0xFFFFFF00 == 0:
                body = body[4:]

        messages.append((cmd, body))
        data = data[total:]

    return messages


def recv_message(sock):
    data = sock.recv(4096)
    if not data:
        raise ConnectionError("dispositivo fechou a conexao")
    return unpack(data)


def negotiate_session_key(sock, local_key):
    """Troca de nonces do protocolo 3.4. Devolve a chave de sessao."""
    print("  -> SESS_KEY_NEG_START (enviando nosso nonce)")
    sock.sendall(pack(1, SESS_KEY_NEG_START,
                      aes_encrypt(local_key, LOCAL_NONCE), local_key))

    messages = recv_message(sock)
    if not messages:
        raise RuntimeError("sem resposta ao inicio da negociacao")

    cmd, body = messages[0]
    if cmd != SESS_KEY_NEG_RESP:
        raise RuntimeError(f"esperava SESS_KEY_NEG_RESP, veio cmd=0x{cmd:02x}")

    plain = aes_decrypt(local_key, body, unpad=False)
    remote_nonce = plain[:16]
    remote_hmac = plain[16:48]

    expected = hmac.new(local_key, LOCAL_NONCE, hashlib.sha256).digest()
    if remote_hmac != expected:
        raise RuntimeError(
            "HMAC do dispositivo nao confere: o local_key esta errado")
    print("  <- SESS_KEY_NEG_RESP (HMAC confere, local_key correto)")

    print("  -> SESS_KEY_NEG_FINISH")
    finish = hmac.new(local_key, remote_nonce, hashlib.sha256).digest()
    sock.sendall(pack(2, SESS_KEY_NEG_FINISH,
                      aes_encrypt(local_key, finish, pad=False), local_key))

    mixed = bytes(a ^ b for a, b in zip(LOCAL_NONCE, remote_nonce))
    session_key = aes_encrypt(local_key, mixed, pad=False)
    print(f"  chave de sessao derivada ({len(session_key)} bytes)")
    return session_key


def query(sock, seq, session_key, payload_obj, cmd=DP_QUERY_NEW):
    raw = b"" if payload_obj is None else json.dumps(
        payload_obj, separators=(",", ":")).encode()
    sock.sendall(pack(seq, cmd, aes_encrypt(session_key, raw), session_key))

    try:
        messages = recv_message(sock)
    except socket.timeout:
        return None

    for reply_cmd, body in messages:
        if not body:
            continue
        plain = aes_decrypt(session_key, body)
        if plain.startswith(b"3.4"):
            plain = plain[len(PROTOCOL_34_HEADER):]
        text = plain.decode("utf-8", errors="replace").strip("\x00")
        try:
            return json.loads(text)
        except Exception:
            print(f"     resposta cmd=0x{reply_cmd:02x} nao-JSON: {text!r}")
    return None


def main():
    local_key = config.LOCAL_KEY.encode()
    print(f"Conectando em {config.HOST}:{config.PORT} "
          f"(protocolo {config.PROTOCOL_VERSION})")

    sock = socket.create_connection((config.HOST, config.PORT),
                                    timeout=config.TIMEOUT_S)
    sock.settimeout(config.TIMEOUT_S)
    try:
        print("Negociando chave de sessao:")
        session_key = negotiate_session_key(sock, local_key)

        # O formato exato do pedido varia entre firmwares; tentamos os
        # candidatos conhecidos ate um devolver dados.
        candidates = [
            ("DP_QUERY_NEW payload vazio", {}, DP_QUERY_NEW),
            ("DP_QUERY_NEW com devId", {
                "gwId": config.DEVICE_ID, "devId": config.DEVICE_ID}, DP_QUERY_NEW),
            ("DP_QUERY classico", {
                "gwId": config.DEVICE_ID, "devId": config.DEVICE_ID}, DP_QUERY),
        ]

        seq = 3
        for label, payload, cmd in candidates:
            print(f"\nTentando: {label}")
            result = query(sock, seq, session_key, payload, cmd)
            seq += 1
            if result:
                print("\n=== DADOS RECEBIDOS DA ESTACAO (sem nuvem) ===")
                print(json.dumps(result, indent=2, ensure_ascii=False))
                return 0
            print("  sem dados utilizaveis")

        print("\nA estacao aceitou a conexao e a negociacao, mas nao devolveu "
              "os DPs em nenhum dos formatos testados.")
        return 1

    finally:
        sock.close()


if __name__ == "__main__":
    sys.exit(main())
