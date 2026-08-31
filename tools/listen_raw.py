"""Escuta mensagens assincronas da estacao para ver se os DPs Raw aparecem.

A consulta local (DP_QUERY) devolve apenas DPs de valor e enum - os do tipo Raw
ficam de fora. A hipotese e' que o dispositivo os envie espontaneamente quando
mudam, em mensagens de STATUS nao solicitadas. Este script mantem a conexao
aberta, envia heartbeats para nao ser desconectado, e imprime tudo que chegar.

Uso:  python -u listen_raw.py [segundos]
"""
import base64
import json
import socket
import struct
import sys
import time

import config
from tuya_local import (DP_QUERY_NEW, PREFIX, PROTOCOL_34_HEADER, aes_decrypt,
                        negotiate_session_key, pack, query)

HEART_BEAT = 0x09
HEARTBEAT_EVERY_S = 10


def take_messages(buffer):
    """Extrai mensagens completas do buffer. Devolve (mensagens, resto)."""
    messages = []
    marker = struct.pack(">I", PREFIX)

    while True:
        start = buffer.find(marker)
        if start < 0 or len(buffer) - start < 16:
            break
        _, _, cmd, length = struct.unpack(">IIII", buffer[start:start + 16])
        total = 16 + length
        if len(buffer) - start < total:
            break

        body = buffer[start + 16:start + 16 + length - 32 - 4]
        if len(body) >= 4:
            retcode = struct.unpack(">I", body[:4])[0]
            if retcode & 0xFFFFFF00 == 0:
                body = body[4:]

        messages.append((cmd, body))
        buffer = buffer[start + total:]

    return messages, buffer


def decode(session_key, body):
    if not body:
        return None
    try:
        plain = aes_decrypt(session_key, body)
    except Exception:
        return None
    if plain.startswith(b"3.4"):
        plain = plain[len(PROTOCOL_34_HEADER):]
    text = plain.decode("utf-8", errors="replace").strip("\x00")
    try:
        return json.loads(text)
    except Exception:
        return text or None


def looks_raw(value):
    """DPs Raw chegam como base64; distingue-os dos enums curtos como 'kmph'."""
    if not isinstance(value, str) or len(value) < 8:
        return False
    try:
        base64.b64decode(value, validate=True)
        return True
    except Exception:
        return False


def main():
    seconds = int(sys.argv[1]) if len(sys.argv) > 1 else 120

    sock = socket.create_connection((config.HOST, config.PORT),
                                    timeout=config.TIMEOUT_S)
    sock.settimeout(1.0)
    try:
        print(f"Conectado em {config.HOST}:{config.PORT}")
        session_key = negotiate_session_key(sock, config.LOCAL_KEY.encode())

        baseline = (query(sock, 3, session_key, {}, DP_QUERY_NEW) or {}).get("dps", {})
        print(f"\nConsulta inicial: {len(baseline)} DPs "
              f"(Raw entre eles: {sum(1 for v in baseline.values() if looks_raw(v))})")
        print(f"Escutando mensagens espontaneas por {seconds}s...\n")

        buffer = b""
        seq = 100
        deadline = time.time() + seconds
        next_beat = time.time() + HEARTBEAT_EVERY_S
        raw_seen = {}
        events = 0

        while time.time() < deadline:
            if time.time() >= next_beat:
                sock.sendall(pack(seq, HEART_BEAT, b"", session_key))
                seq += 1
                next_beat = time.time() + HEARTBEAT_EVERY_S

            try:
                chunk = sock.recv(4096)
            except socket.timeout:
                continue
            if not chunk:
                print("dispositivo encerrou a conexao")
                break

            buffer += chunk
            messages, buffer = take_messages(buffer)

            for cmd, body in messages:
                if cmd == HEART_BEAT:
                    continue
                payload = decode(session_key, body)
                if payload is None:
                    continue

                events += 1
                stamp = time.strftime("%H:%M:%S")

                # Respostas de consulta trazem {"dps": {...}}, mas as mensagens
                # de STATUS espontaneas aninham em {"protocol":4,"data":{"dps":{...}}}.
                dps = None
                if isinstance(payload, dict):
                    dps = payload.get("dps")
                    if dps is None:
                        dps = (payload.get("data") or {}).get("dps")
                if dps:
                    print(f"[{stamp}] cmd=0x{cmd:02x} dps={dps}")
                    for dp_id, value in dps.items():
                        if looks_raw(value):
                            raw_seen[dp_id] = value
                            decoded = base64.b64decode(value)
                            hexed = " ".join(f"{b:02X}" for b in decoded)
                            print(f"           *** DP RAW {dp_id}: {value}")
                            print(f"               bytes = {hexed}")
                else:
                    print(f"[{stamp}] cmd=0x{cmd:02x} {payload}")

        print(f"\n=== resultado apos {seconds}s ===")
        print(f"mensagens espontaneas recebidas: {events}")
        if raw_seen:
            print("DPs Raw capturados de forma assincrona:")
            for dp_id, value in raw_seen.items():
                print(f"  {dp_id} = {value}")
            print("\nO caminho local consegue receber DPs Raw.")
        else:
            print("Nenhum DP Raw chegou.")
            print("A leitura local nao entrega os DPs do tipo Raw neste periodo.")
        return 0

    finally:
        sock.close()


if __name__ == "__main__":
    sys.exit(main())
