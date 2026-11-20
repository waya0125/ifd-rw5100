/*
 * ifd-rw5100: SHARP RW-5100 driver for pcsc-lite
 * Copyright (C) 2026 KASHIWAYA Rikuto
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * RW-5100 との通信（枠の組み立てと解釈）。通信方式は docs/protocol.md を参照。
 */
#include "rw5100.h"

#include <stdio.h>
#include <string.h>

#define EP_OUT 0x02
#define EP_IN  0x81
#define PACKET 64

#define FRAME_CMD_OUT  0x12
#define FRAME_CMD_IN   0x21
#define FRAME_APDU_OUT 0x52
#define FRAME_APDU_IN  0x25

static void dump(const rw5100_t *rw, const char *dir, const uint8_t *buf, size_t len)
{
	if (!rw->debug)
		return;
	fprintf(stderr, "rw5100 %s", dir);
	for (size_t i = 0; i < len; i++)
		fprintf(stderr, " %02X", buf[i]);
	fputc('\n', stderr);
}

static int usb_err(int r)
{
	if (r == LIBUSB_ERROR_NO_DEVICE)
		return RW5100_ERR_NODEV;
	if (r == LIBUSB_ERROR_TIMEOUT)
		return RW5100_ERR_TIMEOUT;
	return RW5100_ERR_USB;
}

static uint8_t xor_sum(const uint8_t *buf, size_t len)
{
	uint8_t x = 0;
	for (size_t i = 0; i < len; i++)
		x ^= buf[i];
	return x;
}

/* 読み残しを捨てる（前の通信が途中で切れた時のため） */
static void drain(rw5100_t *rw)
{
	uint8_t buf[PACKET];
	int got;
	for (int i = 0; i < 8; i++) {
		got = 0;
		if (libusb_bulk_transfer(rw->dev, EP_IN, buf, sizeof(buf), &got, 20) != 0 || got == 0)
			break;
		dump(rw, "<< (捨てた)", buf, (size_t)got);
	}
}

int rw5100_open(rw5100_t *rw, libusb_context *ctx, uint8_t bus, uint8_t addr)
{
	libusb_device **list;
	ssize_t n;
	int ret = RW5100_ERR_USB;

	memset(rw, 0, sizeof(*rw));
	rw->ctx = ctx;
	rw->timeout_ms = 3000;

	n = libusb_get_device_list(ctx, &list);
	if (n < 0)
		return RW5100_ERR_USB;

	for (ssize_t i = 0; i < n; i++) {
		struct libusb_device_descriptor desc;
		if (libusb_get_device_descriptor(list[i], &desc) != 0)
			continue;
		if (desc.idVendor != RW5100_VID || desc.idProduct != RW5100_PID)
			continue;
		if (bus && libusb_get_bus_number(list[i]) != bus)
			continue;
		if (addr && libusb_get_device_address(list[i]) != addr)
			continue;
		if (libusb_open(list[i], &rw->dev) == 0)
			ret = RW5100_OK;
		break;
	}
	libusb_free_device_list(list, 1);
	if (ret != RW5100_OK)
		return ret;

	libusb_set_auto_detach_kernel_driver(rw->dev, 1);
	if (libusb_claim_interface(rw->dev, 0) != 0) {
		libusb_close(rw->dev);
		rw->dev = NULL;
		return RW5100_ERR_USB;
	}
	drain(rw);
	return RW5100_OK;
}

int rw5100_recover(rw5100_t *rw)
{
	int r;

	if (!rw->dev)
		return RW5100_ERR_USB;
	libusb_release_interface(rw->dev, 0);
	r = libusb_reset_device(rw->dev);
	if (r == LIBUSB_ERROR_NO_DEVICE || r == LIBUSB_ERROR_NOT_FOUND)
		return RW5100_ERR_NODEV; /* 抜かれたか、別の機器として出直した */
	if (r != 0)
		return RW5100_ERR_USB;
	if (libusb_claim_interface(rw->dev, 0) != 0)
		return RW5100_ERR_USB;
	rw->cmd_seq = 0;
	rw->apdu_seq = 0;
	drain(rw);
	return rw5100_init(rw, NULL, 0);
}

void rw5100_close(rw5100_t *rw)
{
	if (!rw->dev)
		return;
	libusb_release_interface(rw->dev, 0);
	libusb_close(rw->dev);
	rw->dev = NULL;
}

/* 枠を送って返事の枠を受け取る。payload は送る枠の中身（長さの後ろからチェックサムの手前まで）。 */
static int exchange(rw5100_t *rw, uint8_t type_out, uint8_t type_in, uint8_t *seqp,
		    const uint8_t *payload, size_t plen, uint8_t *in, size_t *in_len)
{
	uint8_t out[RW5100_MAX_FRAME];
	size_t olen = 0, have = 0, need = 0;
	uint8_t seq = *seqp;
	int got, r;

	if (plen > 0xFF || plen + 4 > sizeof(out))
		return RW5100_ERR_ARG;

	out[olen++] = type_out;
	out[olen++] = seq;
	out[olen++] = (uint8_t)plen;
	memcpy(out + olen, payload, plen);
	olen += plen;
	out[olen] = xor_sum(out, olen);
	olen++;

	/* 通し番号は送ったら進める（返事が壊れていても、リーダーは受け取っている） */
	*seqp ^= 0x40;

	dump(rw, ">>", out, olen);
	r = libusb_bulk_transfer(rw->dev, EP_OUT, out, (int)olen, &got, rw->timeout_ms);
	if (r != 0)
		return usb_err(r);
	if ((size_t)got != olen)
		return RW5100_ERR_USB;

	for (;;) {
		got = 0;
		if (have + PACKET > RW5100_MAX_FRAME)
			return RW5100_ERR_FRAME;
		r = libusb_bulk_transfer(rw->dev, EP_IN, in + have, PACKET, &got, rw->timeout_ms);
		if (r != 0)
			return usb_err(r);
		have += (size_t)got;
		if (have >= 3)
			need = 3 + (size_t)in[2] + 1;
		if (need && have >= need)
			break;
	}
	dump(rw, "<<", in, have);

	if (have != need)
		return RW5100_ERR_FRAME;
	if (xor_sum(in, need - 1) != in[need - 1])
		return RW5100_ERR_CHECK;
	if (in[0] != type_in)
		return RW5100_ERR_FRAME;
	/* 通し番号の場所に 0x80 が立った返事は、リーダーが枠を受け付けなかった印。
	 * このときリーダーは通し番号を進めないので、こちらも戻す。 */
	if (in[1] & 0x80) {
		*seqp = seq;
		rw->last_reject = in[1];
		fprintf(stderr, "rw5100: リーダーが受け付けなかった: 送った %02X %02X %02X、返事 %02X %02X %02X\n",
			out[0], out[1], out[2], in[0], in[1], in[2]);
		return RW5100_ERR_REJECT;
	}
	if (in[1] != seq) {
		/* 中身（鍵などが入りうる）は出さず、枠の頭だけを出す */
		fprintf(stderr, "rw5100: 通し番号が合わない: 送った %02X %02X %02X（%zu バイト）、返事 %02X %02X %02X（%zu バイト）\n",
			out[0], out[1], out[2], olen, in[0], in[1], in[2], need);
		return RW5100_ERR_SEQ;
	}
	*in_len = need;
	return RW5100_OK;
}

int rw5100_command(rw5100_t *rw, uint8_t cmd, const uint8_t *data, size_t len,
		   rw5100_reply_t *reply)
{
	uint8_t payload[RW5100_MAX_FRAME];
	uint8_t in[RW5100_MAX_FRAME];
	size_t in_len = 0, plen;
	int r;

	if (len + 2 > sizeof(payload))
		return RW5100_ERR_ARG;
	payload[0] = 0x01;
	payload[1] = cmd;
	if (len)
		memcpy(payload + 2, data, len);

	r = exchange(rw, FRAME_CMD_OUT, FRAME_CMD_IN, &rw->cmd_seq, payload, len + 2, in, &in_len);
	if (r != RW5100_OK)
		return r;

	/* in: 21 seq len | 01 cmd data... state 00 | xor */
	plen = in[2];
	if (plen < 4 || in[3] != 0x01 || in[4] != cmd)
		return RW5100_ERR_FRAME;
	reply->state = in[3 + plen - 2];
	reply->len = plen - 4;
	memcpy(reply->data, in + 5, reply->len);
	return RW5100_OK;
}

int rw5100_apdu(rw5100_t *rw, const uint8_t *apdu, size_t len, rw5100_reply_t *reply)
{
	uint8_t in[RW5100_MAX_FRAME];
	size_t in_len = 0;
	int r;

	uint8_t buf[RW5100_MAX_FRAME];

	if (len > sizeof(buf))
		return RW5100_ERR_ARG;
	memcpy(buf, apdu, len);
	r = exchange(rw, FRAME_APDU_OUT, FRAME_APDU_IN, &rw->apdu_seq, buf, len, in, &in_len);
	if (r != RW5100_OK)
		return r;
	reply->state = 0;
	reply->len = in[2];
	memcpy(reply->data, in + 3, reply->len);
	return RW5100_OK;
}

int rw5100_init(rw5100_t *rw, char *version, size_t version_size)
{
	rw5100_reply_t rep;
	uint8_t st, state;
	int r;

	r = rw5100_command(rw, RW5100_CMD_VERSION, NULL, 0, &rep);
	if (r != RW5100_OK)
		return r;
	if (version && version_size) {
		size_t n = rep.len < version_size - 1 ? rep.len : version_size - 1;
		memcpy(version, rep.data, n);
		version[n] = '\0';
	}
	r = rw5100_status(rw, &st, &state);
	if (r != RW5100_OK)
		return r;
	for (int i = 0; i < 2; i++) {
		r = rw5100_command(rw, RW5100_CMD_INIT, NULL, 0, &rep);
		if (r != RW5100_OK)
			return r;
		r = rw5100_command(rw, RW5100_CMD_UNKNOWN41, NULL, 0, &rep);
		if (r != RW5100_OK)
			return r;
	}
	return RW5100_OK;
}

int rw5100_status(rw5100_t *rw, uint8_t *status, uint8_t *state)
{
	rw5100_reply_t rep;
	int r = rw5100_command(rw, RW5100_CMD_STATUS, NULL, 0, &rep);
	if (r != RW5100_OK)
		return r;
	if (rep.len < 1)
		return RW5100_ERR_FRAME;
	*status = rep.data[0];
	*state = rep.state;
	return RW5100_OK;
}

int rw5100_power_on(rw5100_t *rw, uint8_t mode, uint8_t *atr, size_t *atr_len)
{
	static const uint8_t get_atr[] = { 0x0A, 0x00 };
	rw5100_reply_t rep;
	int r;

	r = rw5100_command(rw, RW5100_CMD_POWER_ON, &mode, 1, &rep);
	if (r != RW5100_OK)
		return r;
	rw->apdu_seq = 0;
	r = rw5100_command(rw, RW5100_CMD_GET_ATR, get_atr, sizeof(get_atr), &rep);
	if (r != RW5100_OK)
		return r;
	if (rep.state != RW5100_STATE_POWERED)
		return RW5100_ERR_FRAME;
	*atr_len = rw5100_atr_length(rep.data, rep.len);
	if (*atr_len == 0)
		return RW5100_ERR_FRAME;
	memcpy(atr, rep.data, *atr_len);
	return RW5100_OK;
}

size_t rw5100_atr_length(const uint8_t *buf, size_t max)
{
	size_t i = 1;
	uint8_t y, k;
	int tck = 0;

	if (max < 2 || (buf[0] != 0x3B && buf[0] != 0x3F))
		return 0;
	y = buf[1] >> 4;
	k = buf[1] & 0x0F;
	i = 2;
	for (;;) {
		if (y & 0x1) i++; /* TA */
		if (y & 0x2) i++; /* TB */
		if (y & 0x4) i++; /* TC */
		if (!(y & 0x8))
			break;
		if (i >= max)
			return 0;
		uint8_t td = buf[i++];
		if ((td & 0x0F) != 0)
			tck = 1;
		y = td >> 4;
	}
	i += k;
	if (tck)
		i++;
	return i <= max ? i : 0;
}

const char *rw5100_strerror(int err)
{
	switch (err) {
	case RW5100_OK:         return "成功";
	case RW5100_ERR_USB:    return "USB の転送に失敗";
	case RW5100_ERR_FRAME:  return "返事の枠の形が違う";
	case RW5100_ERR_CHECK:  return "返事のチェックサムが合わない";
	case RW5100_ERR_SEQ:    return "返事の通し番号が合わない";
	case RW5100_ERR_ARG:    return "引数が大きすぎる";
	case RW5100_ERR_NODEV:  return "機器が抜かれた";
	case RW5100_ERR_TIMEOUT: return "返事が来ない";
	case RW5100_ERR_REJECT: return "リーダーが受け付けなかった";
	default:                return "不明なエラー";
	}
}
