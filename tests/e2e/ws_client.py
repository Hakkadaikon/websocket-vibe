"""Minimal RFC6455 client using only the Python stdlib (no pip deps).

Used by the E2E harness to drive the freestanding C server. Implements the
client side: masked frames, handshake, fragmentation, control frames.
"""
import base64
import hashlib
import os
import socket
import struct

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

OP_CONT, OP_TEXT, OP_BIN, OP_CLOSE, OP_PING, OP_PONG = 0x0, 0x1, 0x2, 0x8, 0x9, 0xA


class WSClient:
    def __init__(self, host="127.0.0.1", port=9001, timeout=5.0):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)
        self._buf = b""

    def handshake(self):
        key = base64.b64encode(os.urandom(16)).decode()
        req = (
            f"GET /chat HTTP/1.1\r\n"
            f"Host: localhost\r\n"
            f"Upgrade: websocket\r\n"
            f"Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            f"Sec-WebSocket-Version: 13\r\n\r\n"
        )
        self.sock.sendall(req.encode())
        resp = self._read_until(b"\r\n\r\n")
        expect = base64.b64encode(hashlib.sha1((key + GUID).encode()).digest()).decode()
        assert b"101 Switching Protocols" in resp, resp
        assert expect.encode() in resp, (expect, resp)
        return True

    def _read_until(self, marker):
        while marker not in self._buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                break
            self._buf += chunk
        idx = self._buf.find(marker) + len(marker)
        out, self._buf = self._buf[:idx], self._buf[idx:]
        return out

    def _recv_exact(self, n):
        while len(self._buf) < n:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("server closed")
            self._buf += chunk
        out, self._buf = self._buf[:n], self._buf[n:]
        return out

    def send_frame(self, opcode, payload=b"", fin=True):
        b0 = (0x80 if fin else 0) | opcode
        n = len(payload)
        if n <= 125:
            header = struct.pack("!BB", b0, 0x80 | n)
        elif n <= 0xFFFF:
            header = struct.pack("!BBH", b0, 0x80 | 126, n)
        else:
            header = struct.pack("!BBQ", b0, 0x80 | 127, n)
        mask = os.urandom(4)
        masked = bytes(c ^ mask[i % 4] for i, c in enumerate(payload))
        self.sock.sendall(header + mask + masked)

    def recv_frame(self):
        b0, b1 = self._recv_exact(2)
        fin = bool(b0 & 0x80)
        opcode = b0 & 0x0F
        n = b1 & 0x7F
        if n == 126:
            (n,) = struct.unpack("!H", self._recv_exact(2))
        elif n == 127:
            (n,) = struct.unpack("!Q", self._recv_exact(8))
        assert not (b1 & 0x80), "server must not mask"
        payload = self._recv_exact(n)
        return fin, opcode, payload

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass
