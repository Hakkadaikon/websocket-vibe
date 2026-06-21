#!/usr/bin/env python3
"""エンドツーエンドテスト: freestanding C サーバを起動し、stdlib の WebSocket
クライアントで駆動して RFC6455 の挙動を検証する。stdlib のみ — pip 依存なし。"""
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SERVER = os.path.join(ROOT, "build", "ws_server")

sys.path.insert(0, HERE)
from ws_client import WSClient, OP_TEXT, OP_BIN, OP_PING, OP_PONG, OP_CLOSE, OP_CONT

PORT = 9001
PASS, FAIL = 0, 0


def check(name, cond):
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"  PASS {name}")
    else:
        FAIL += 1
        print(f"  FAIL {name}")


def connect():
    c = WSClient(port=PORT)
    c.handshake()
    return c


def test_echo_text():
    c = connect()
    c.send_frame(OP_TEXT, b"hello world")
    fin, op, pl = c.recv_frame()
    check("echo_text", fin and op == OP_TEXT and pl == b"hello world")
    c.close()


def test_echo_binary():
    c = connect()
    data = bytes(range(256)) * 8
    c.send_frame(OP_BIN, data)
    fin, op, pl = c.recv_frame()
    check("echo_binary", fin and op == OP_BIN and pl == data)
    c.close()


def test_ping_pong():
    c = connect()
    c.send_frame(OP_PING, b"ping-payload")
    fin, op, pl = c.recv_frame()
    check("ping_pong", op == OP_PONG and pl == b"ping-payload")
    c.close()


def test_fragmented_text():
    c = connect()
    c.send_frame(OP_TEXT, b"Hel", fin=False)
    c.send_frame(OP_CONT, b"lo!", fin=True)
    fin, op, pl = c.recv_frame()
    check("fragmented_text", fin and op == OP_TEXT and pl == b"Hello!")
    c.close()


def test_large_message():
    c = connect()
    data = b"x" * 100000  # 8 バイト長形式を強制する
    c.send_frame(OP_BIN, data)
    fin, op, pl = c.recv_frame()
    check("large_message", op == OP_BIN and pl == data)
    c.close()


def test_close_handshake():
    c = connect()
    c.send_frame(OP_CLOSE, b"\x03\xe8")  # コード 1000
    fin, op, pl = c.recv_frame()
    code = (pl[0] << 8 | pl[1]) if len(pl) >= 2 else None
    check("close_handshake", op == OP_CLOSE and code == 1000)
    c.close()


def test_bad_utf8_rejected():
    c = connect()
    c.send_frame(OP_TEXT, b"\xc0\x80")  # 冗長符号化 -> プロトコルエラー
    fin, op, pl = c.recv_frame()
    code = (pl[0] << 8 | pl[1]) if len(pl) >= 2 else None
    check("bad_utf8_rejected", op == OP_CLOSE and code == 1002)
    c.close()


def test_concurrent_clients():
    # 複数クライアントを同時に開く。epoll で多重化したサーバは各クライアントを
    # 独立して処理しなければならない(交互の送信が混線してはならない)。
    n = 8
    clients = [connect() for _ in range(n)]
    for i, c in enumerate(clients):
        c.send_frame(OP_TEXT, f"client-{i}".encode())
    ok = True
    for i, c in enumerate(clients):
        fin, op, pl = c.recv_frame()
        ok = ok and fin and op == OP_TEXT and pl == f"client-{i}".encode()
    for c in clients:
        c.close()
    check("concurrent_clients", ok)


def test_interleaved_clients():
    # 交互にやり取りする 2 つの長命クライアントは集約状態を共有しない。
    a, b = connect(), connect()
    ok = True
    for r in range(5):
        a.send_frame(OP_TEXT, f"a{r}".encode())
        b.send_frame(OP_TEXT, f"b{r}".encode())
        _, _, pa = a.recv_frame()
        _, _, pb = b.recv_frame()
        ok = ok and pa == f"a{r}".encode() and pb == f"b{r}".encode()
    a.close()
    b.close()
    check("interleaved_clients", ok)


def main():
    if not os.path.exists(SERVER):
        print(f"server binary missing: {SERVER} (run `just build` first)")
        return 1
    proc = subprocess.Popen([SERVER])
    try:
        # ポートが接続を受け付けるまで待つ
        import socket
        for _ in range(50):
            try:
                socket.create_connection(("127.0.0.1", PORT), timeout=0.2).close()
                break
            except OSError:
                time.sleep(0.05)
        for t in (
            test_echo_text, test_echo_binary, test_ping_pong, test_fragmented_text,
            test_large_message, test_close_handshake, test_bad_utf8_rejected,
            test_concurrent_clients, test_interleaved_clients,
        ):
            try:
                t()
            except Exception as e:  # noqa: BLE001
                check(t.__name__, False)
                print(f"    exception: {e}")
    finally:
        proc.terminate()
        proc.wait(timeout=2)
    print(f"\nE2E: {PASS} passed, {FAIL} failed")
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
