/*
 * ifd-rw5100: SHARP RW-5100 driver for pcsc-lite
 * Copyright (C) 2026 KASHIWAYA Rikuto
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * RW-5100 との通信（枠の組み立てと解釈）。pcscd にも試験の道具にも依存しない。
 * 通信方式は docs/protocol.md を参照。
 */
#ifndef RW5100_H
#define RW5100_H

#include <stddef.h>
#include <stdint.h>
#include <libusb.h>

#define RW5100_VID 0x04dd
#define RW5100_PID 0x9259

/* 命令（docs/protocol.md の「命令の一覧」） */
#define RW5100_CMD_VERSION  0xC0
#define RW5100_CMD_STATUS   0x31
#define RW5100_CMD_UNKNOWN41 0x41
#define RW5100_CMD_INIT     0xD0
#define RW5100_CMD_POWER_ON 0xD1
#define RW5100_CMD_GET_ATR  0x35

/* 31 の返事のビット（たぶん。docs/protocol.md） */
#define RW5100_STATUS_READY   0x20 /* 初期化（D0・41）のあとに立つ */
#define RW5100_STATUS_PRESENT 0x10 /* カードあり */

/* 返事の状態バイト */
#define RW5100_STATE_POWERED   0xF0
#define RW5100_STATE_UNPOWERED 0xF1

#define RW5100_MAX_FRAME 300

typedef struct rw5100 {
	libusb_context *ctx;
	libusb_device_handle *dev;
	uint8_t cmd_seq;      /* 命令（12/21）の通し番号（0x00 か 0x40） */
	uint8_t apdu_seq;     /* APDU（52/25）の通し番号（0x00 か 0x40） */
	unsigned timeout_ms;
	int debug;            /* 0 以外なら送受信を stderr に出す */
	uint8_t last_reject;  /* 最後に受け付けなかった時の印（0x80 以上） */
} rw5100_t;

/* 返事の解釈結果 */
typedef struct rw5100_reply {
	uint8_t data[RW5100_MAX_FRAME]; /* 命令の返事: 命令の後ろから状態の手前まで。APDU: レスポンス全体 */
	size_t len;
	uint8_t state;                  /* 命令の返事の状態バイト（APDU では 0） */
} rw5100_reply_t;

/* 戻り値: 0 は成功、負は失敗 */
#define RW5100_OK          0
#define RW5100_ERR_USB    -1
#define RW5100_ERR_FRAME  -2
#define RW5100_ERR_CHECK  -3
#define RW5100_ERR_SEQ    -4
#define RW5100_ERR_ARG    -5
#define RW5100_ERR_NODEV  -6 /* 機器が抜かれた */
#define RW5100_ERR_TIMEOUT -7
#define RW5100_ERR_REJECT -8 /* リーダーが枠を受け付けなかった（返事の通し番号の場所に 0x80） */

/* 開く。bus と addr が 0 なら最初に見つかった RW-5100 を開く。 */
int rw5100_open(rw5100_t *rw, libusb_context *ctx, uint8_t bus, uint8_t addr);
void rw5100_close(rw5100_t *rw);

/* 命令を送って返事を受け取る。 */
int rw5100_command(rw5100_t *rw, uint8_t cmd, const uint8_t *data, size_t len,
		   rw5100_reply_t *reply);

/* APDU を送ってレスポンスを受け取る。 */
int rw5100_apdu(rw5100_t *rw, const uint8_t *apdu, size_t len, rw5100_reply_t *reply);

/* リーダーが返事をしなくなった時に立て直す。USB をリセットして開き直し、初期化し直す。
 * 公式ドライバーも、返事が来ない時は USB をリセットしていた。 */
int rw5100_recover(rw5100_t *rw);

/* 接続直後の手順（公式ドライバーと同じ: C0, 31, D0, 41, D0, 41）。version は NULL 可。 */
int rw5100_init(rw5100_t *rw, char *version, size_t version_size);

/* カードの状態を聞く。*status に 31 の返事、*state に状態バイトを入れる。 */
int rw5100_status(rw5100_t *rw, uint8_t *status, uint8_t *state);

/* D1 の引数（公式ドライバーの動きから。docs/protocol.md） */
#define RW5100_POWER_COLD 0x00 /* 電源投入（PC/SC の接続、電源を切ってからのつなぎ直し） */
#define RW5100_POWER_WARM 0x03 /* ウォームリセット（PC/SC のリセット） */

/* カードの電源を入れて（またはリセットして）ATR を読む。atr は 36 バイト以上。*atr_len に ATR の長さ。 */
int rw5100_power_on(rw5100_t *rw, uint8_t mode, uint8_t *atr, size_t *atr_len);

/* ATR の構造から長さを求める（後ろの 00 埋めを取り除くため）。分からなければ 0。 */
size_t rw5100_atr_length(const uint8_t *buf, size_t max);

const char *rw5100_strerror(int err);

#endif
