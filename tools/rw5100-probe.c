/*
 * ifd-rw5100: SHARP RW-5100 driver for pcsc-lite
 * Copyright (C) 2026 KASHIWAYA Rikuto
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * 試験の道具: RW-5100 を開いて、版・カードの状態・ATR を表示する。
 * カードには APDU を送らない（中身は読まない）。
 *
 * 使い方: rw5100-probe [-v] [-w 秒]
 *   -v  送受信した枠を表示する
 *   -w  指定した秒数、カードの状態を 0.5 秒ごとに見て、変化を表示する
 *   -r  ATR を読んだあと、ウォームリセット（D1 03）をして ATR を読み直す
 */
#include "rw5100.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void print_atr(const uint8_t *atr, size_t len)
{
	printf("ATR:");
	for (size_t i = 0; i < len; i++)
		printf(" %02X", atr[i]);
	putchar('\n');
}

static void stamp(void)
{
	char buf[16];
	time_t t = time(NULL);
	strftime(buf, sizeof(buf), "%H:%M:%S", localtime(&t));
	printf("[%s] ", buf);
}

static const char *status_name(uint8_t st)
{
	return (st & RW5100_STATUS_PRESENT) ? "カードあり" : "カードなし";
}

int main(int argc, char **argv)
{
	libusb_context *ctx = NULL;
	rw5100_t rw;
	char version[64];
	uint8_t st = 0, state = 0, last = 0xFF;
	uint8_t atr[64];
	size_t atr_len = 0;
	int opt, r, watch = 0, debug = 0, warm = 0;

	while ((opt = getopt(argc, argv, "vw:r")) != -1) {
		switch (opt) {
		case 'v': debug = 1; break;
		case 'w': watch = atoi(optarg); break;
		case 'r': warm = 1; break;
		default:
			fprintf(stderr, "使い方: %s [-v] [-w 秒] [-r]\n", argv[0]);
			return 2;
		}
	}

	if (libusb_init(&ctx) != 0) {
		fprintf(stderr, "libusb を初期化できない\n");
		return 1;
	}
	r = rw5100_open(&rw, ctx, 0, 0);
	if (r != RW5100_OK) {
		fprintf(stderr, "RW-5100 を開けない（挿さっていない、権限がない、ほかのプログラムが使っている）\n");
		libusb_exit(ctx);
		return 1;
	}
	rw.debug = debug;

	r = rw5100_init(&rw, version, sizeof(version));
	if (r != RW5100_OK) {
		fprintf(stderr, "初期化に失敗: %s\n", rw5100_strerror(r));
		goto out;
	}
	printf("版: %s\n", version);

	for (int tick = 0; ; tick++) {
		r = rw5100_status(&rw, &st, &state);
		if (r != RW5100_OK) {
			fprintf(stderr, "状態を聞けない: %s\n", rw5100_strerror(r));
			goto out;
		}
		if (st != last) {
			stamp();
			printf("状態: %02X（%s）、状態バイト %02X\n", st, status_name(st), state);
			if ((st & RW5100_STATUS_PRESENT) && !(last != 0xFF && (last & RW5100_STATUS_PRESENT))) {
				r = rw5100_power_on(&rw, RW5100_POWER_COLD, atr, &atr_len);
				stamp();
				if (r == RW5100_OK)
					print_atr(atr, atr_len);
				else
					printf("ATR を読めない: %s\n", rw5100_strerror(r));
				if (r == RW5100_OK && warm) {
					r = rw5100_power_on(&rw, RW5100_POWER_WARM, atr, &atr_len);
					stamp();
					printf("ウォームリセット: ");
					if (r == RW5100_OK)
						print_atr(atr, atr_len);
					else
						printf("失敗: %s\n", rw5100_strerror(r));
				}
			}
			last = st;
		}
		if (tick >= watch * 2)
			break;
		usleep(500000);
	}
	r = RW5100_OK;
out:
	rw5100_close(&rw);
	libusb_exit(ctx);
	return r == RW5100_OK ? 0 : 1;
}
