// ホットパス(フレームヘッダ parse + unmask)のローカル性能測定。
// スループット(MiB/s)と ns/op を出力し、実行ごとの退行を可視化する。
// ハーネスでのみホストの clock_gettime を使う。測定対象のコードは SDK 本体。
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "../src/core/frame.c"
#include "../src/core/mask.c"

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double) ts.tv_sec + (double) ts.tv_nsec * 1e-9;
}

// 1 つのペイロードサイズをベンチする。マスク済みフレームを一度組み立て、
// 以後はヘッダ parse とペイロードの unmask を繰り返す(サーバのフレーム単位の受信処理)。
static void bench_size(size_t payload, long iters) {
    static u8 buf[1 << 20];
    u8 key[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    size_t hn = ws_frame_build_header(buf, sizeof buf, true, WS_OP_BINARY, true, key, payload);
    for (size_t i = 0; i < payload; i++)
        buf[hn + i] = (u8) i;
    ws_mask(buf + hn, payload, key);

    volatile u64 sink = 0;
    double t0 = now_s();
    for (long it = 0; it < iters; it++) {
        ws_frame_header h;
        ws_frame_parse_header(buf, hn + payload, &h);
        ws_mask(buf + hn, payload, key); // unmask(対合なのでデータは安定したまま)
        sink += h.payload_len + buf[hn];
    }
    double t1 = now_s();
    double secs = t1 - t0;
    double bytes = (double) (hn + payload) * (double) iters;
    double nsop = secs / (double) iters * 1e9;
    printf("  payload=%7zu  %8.1f MiB/s  %8.1f ns/op  (sink=%llu)\n", payload,
           bytes / secs / (1024.0 * 1024.0), nsop, (unsigned long long) sink);
}

int main(void) {
    printf("bench_frame: parse+unmask throughput\n");
    bench_size(8, 20000000);
    bench_size(125, 5000000);
    bench_size(1024, 2000000);
    bench_size(65536, 50000);
    bench_size(1 << 20, 3000);
    return 0;
}
