# echo サンプル

受信したテキストメッセージをそのまま返す WebSocket サーバである。
sans-IO コア（`ws.h`）を通常の libc ソケットから駆動する例で、
freestanding なデモサーバ（`src/sdk/server.c`）とは別に、SDK を普通の C アプリへ組み込む形を示す。

## ビルドと実行

```sh
just example          # build/echo を生成する
./build/echo          # ws://127.0.0.1:9002 で待ち受ける
```

Nix からビルドする場合は次のとおり。

```sh
nix build .#echo      # result/bin/ws-echo を生成する
./result/bin/ws-echo
```

## 動作確認

リポジトリの Python クライアントで接続して echo を確かめられる。

```sh
python3 - <<'PY'
import sys; sys.path.insert(0, "tests/e2e")
from ws_client import WSClient, OP_TEXT
c = WSClient(port=9002); c.handshake()
c.send_frame(OP_TEXT, b"hello echo")
print(c.recv_frame())   # (True, 1, b'hello echo')
c.close()
PY
```

text の echo に加え、ping には pong を返し、close と不正フレームには close で応答する。
