/*
 * ifd-rw5100: SHARP RW-5100 driver for pcsc-lite
 * Copyright (C) 2026 KASHIWAYA Rikuto
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * pcscd から呼ばれる IFD handler（IFD Handler API 3.0）。
 * 環境変数 IFD_RW5100_DEBUG を 1 にすると、送受信した枠を stderr に出す
 * （pcscd -f -d で見える）。
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <PCSC/ifdhandler.h>
#include <PCSC/reader.h>

#include "rw5100.h"

#define MAX_READERS 4

typedef struct reader {
	int used;
	rw5100_t rw;
	uint8_t atr[MAX_ATR_SIZE];
	size_t atr_len;   /* 0 ならカードに電源が入っていない */
	pthread_mutex_t lock;
} reader_t;

static reader_t readers[MAX_READERS];
static libusb_context *usb_ctx;
static int usb_users;
static pthread_mutex_t global_lock = PTHREAD_MUTEX_INITIALIZER;

static int debug_enabled(void)
{
	const char *e = getenv("IFD_RW5100_DEBUG");
	return e && *e && *e != '0';
}

#define LOG(...) do { fprintf(stderr, "ifd-rw5100: " __VA_ARGS__); fputc('\n', stderr); } while (0)

static reader_t *get_reader(DWORD Lun)
{
	DWORD idx = Lun >> 16;
	if (idx >= MAX_READERS || !readers[idx].used)
		return NULL;
	return &readers[idx];
}

static RESPONSECODE to_ifd(int r)
{
	switch (r) {
	case RW5100_OK:          return IFD_SUCCESS;
	case RW5100_ERR_NODEV:   return IFD_NO_SUCH_DEVICE;
	case RW5100_ERR_TIMEOUT: return IFD_RESPONSE_TIMEOUT;
	default:                 return IFD_COMMUNICATION_ERROR;
	}
}

/* DeviceName の例: "usb:04dd/9259:libudev:0:/dev/bus/usb/001/014" */
static void parse_device_name(const char *name, uint8_t *bus, uint8_t *addr)
{
	unsigned b = 0, a = 0;
	const char *p = name ? strstr(name, "/dev/bus/usb/") : NULL;

	*bus = *addr = 0;
	if (p && sscanf(p, "/dev/bus/usb/%u/%u", &b, &a) == 2 && b < 256 && a < 256) {
		*bus = (uint8_t)b;
		*addr = (uint8_t)a;
	}
}

static RESPONSECODE open_reader(DWORD Lun, const char *name)
{
	DWORD idx = Lun >> 16;
	uint8_t bus, addr;
	reader_t *rd;
	char version[64];
	int r;

	if (idx >= MAX_READERS)
		return IFD_COMMUNICATION_ERROR;

	pthread_mutex_lock(&global_lock);
	rd = &readers[idx];
	if (rd->used) {
		pthread_mutex_unlock(&global_lock);
		return IFD_COMMUNICATION_ERROR;
	}
	if (usb_users == 0 && libusb_init(&usb_ctx) != 0) {
		pthread_mutex_unlock(&global_lock);
		return IFD_COMMUNICATION_ERROR;
	}
	usb_users++;

	parse_device_name(name, &bus, &addr);
	r = rw5100_open(&rd->rw, usb_ctx, bus, addr);
	if (r == RW5100_OK) {
		rd->rw.debug = debug_enabled();
		r = rw5100_init(&rd->rw, version, sizeof(version));
		if (r != RW5100_OK)
			rw5100_close(&rd->rw);
	}
	if (r != RW5100_OK) {
		LOG("開けない（%s）: %s", name ? name : "名前なし", rw5100_strerror(r));
		if (--usb_users == 0) {
			libusb_exit(usb_ctx);
			usb_ctx = NULL;
		}
		pthread_mutex_unlock(&global_lock);
		return to_ifd(r);
	}

	rd->used = 1;
	rd->atr_len = 0;
	pthread_mutex_init(&rd->lock, NULL);
	pthread_mutex_unlock(&global_lock);
	LOG("開いた（%s）。版: %s", name ? name : "名前なし", version);
	return IFD_SUCCESS;
}

RESPONSECODE IFDHCreateChannelByName(DWORD Lun, LPSTR DeviceName)
{
	return open_reader(Lun, DeviceName);
}

RESPONSECODE IFDHCreateChannel(DWORD Lun, DWORD Channel)
{
	(void)Channel;
	return open_reader(Lun, NULL);
}

RESPONSECODE IFDHCloseChannel(DWORD Lun)
{
	reader_t *rd;

	pthread_mutex_lock(&global_lock);
	rd = get_reader(Lun);
	if (!rd) {
		pthread_mutex_unlock(&global_lock);
		return IFD_COMMUNICATION_ERROR;
	}
	pthread_mutex_lock(&rd->lock);
	rw5100_close(&rd->rw);
	rd->used = 0;
	rd->atr_len = 0;
	pthread_mutex_unlock(&rd->lock);
	pthread_mutex_destroy(&rd->lock);
	if (--usb_users == 0) {
		libusb_exit(usb_ctx);
		usb_ctx = NULL;
	}
	pthread_mutex_unlock(&global_lock);
	return IFD_SUCCESS;
}

static RESPONSECODE put_value(PDWORD Length, PUCHAR Value, const void *src, DWORD n)
{
	if (*Length < n)
		return IFD_ERROR_INSUFFICIENT_BUFFER;
	memcpy(Value, src, n);
	*Length = n;
	return IFD_SUCCESS;
}

RESPONSECODE IFDHGetCapabilities(DWORD Lun, DWORD Tag, PDWORD Length, PUCHAR Value)
{
	reader_t *rd = get_reader(Lun);
	UCHAR one = 1, zero = 0, max = MAX_READERS;
	RESPONSECODE rc;

	if (!rd)
		return IFD_COMMUNICATION_ERROR;

	switch (Tag) {
	case TAG_IFD_ATR:
	case SCARD_ATTR_ATR_STRING:
		pthread_mutex_lock(&rd->lock);
		rc = put_value(Length, Value, rd->atr, (DWORD)rd->atr_len);
		pthread_mutex_unlock(&rd->lock);
		return rc;
	case TAG_IFD_SIMULTANEOUS_ACCESS:
		return put_value(Length, Value, &max, 1);
	case TAG_IFD_SLOTS_NUMBER:
		return put_value(Length, Value, &one, 1);
	case TAG_IFD_THREAD_SAFE:
	case TAG_IFD_SLOT_THREAD_SAFE:
		/* 1 台の中ではロックで直列にしているが、pcscd にも直列にしてもらう */
		return put_value(Length, Value, &zero, 1);
	default:
		return IFD_ERROR_TAG;
	}
}

RESPONSECODE IFDHSetCapabilities(DWORD Lun, DWORD Tag, DWORD Length, PUCHAR Value)
{
	(void)Lun; (void)Tag; (void)Length; (void)Value;
	return IFD_NOT_SUPPORTED;
}

RESPONSECODE IFDHSetProtocolParameters(DWORD Lun, DWORD Protocol, UCHAR Flags,
				       UCHAR PTS1, UCHAR PTS2, UCHAR PTS3)
{
	(void)Flags; (void)PTS1; (void)PTS2; (void)PTS3;
	if (!get_reader(Lun))
		return IFD_COMMUNICATION_ERROR;
	/* 通信速度とプロトコルの交渉はリーダーの中で行われる（たぶん）。受け付けるだけ。 */
	if (Protocol == SCARD_PROTOCOL_T0 || Protocol == SCARD_PROTOCOL_T1)
		return IFD_SUCCESS;
	return IFD_PROTOCOL_NOT_SUPPORTED;
}

RESPONSECODE IFDHPowerICC(DWORD Lun, DWORD Action, PUCHAR Atr, PDWORD AtrLength)
{
	reader_t *rd = get_reader(Lun);
	uint8_t atr[64];
	size_t len = 0;
	int r;

	if (!rd)
		return IFD_COMMUNICATION_ERROR;

	pthread_mutex_lock(&rd->lock);
	switch (Action) {
	case IFD_POWER_DOWN:
		/* 公式ドライバーも電源を切る命令は送らない。持っている ATR だけ捨てる。 */
		rd->atr_len = 0;
		*AtrLength = 0;
		pthread_mutex_unlock(&rd->lock);
		return IFD_SUCCESS;
	case IFD_POWER_UP:
	case IFD_RESET:
		/* 電源が入っていない時のリセットは、電源投入として扱う */
		r = rw5100_power_on(&rd->rw,
				    (Action == IFD_RESET && rd->atr_len) ? RW5100_POWER_WARM : RW5100_POWER_COLD,
				    atr, &len);
		if (r != RW5100_OK || len > MAX_ATR_SIZE) {
			rd->atr_len = 0;
			*AtrLength = 0;
			pthread_mutex_unlock(&rd->lock);
			LOG("ATR を読めない: %s", rw5100_strerror(r));
			return r == RW5100_ERR_NODEV ? IFD_NO_SUCH_DEVICE : IFD_ERROR_POWER_ACTION;
		}
		memcpy(rd->atr, atr, len);
		rd->atr_len = len;
		memcpy(Atr, atr, len);
		*AtrLength = (DWORD)len;
		pthread_mutex_unlock(&rd->lock);
		return IFD_SUCCESS;
	default:
		pthread_mutex_unlock(&rd->lock);
		return IFD_NOT_SUPPORTED;
	}
}

RESPONSECODE IFDHTransmitToICC(DWORD Lun, SCARD_IO_HEADER SendPci, PUCHAR TxBuffer,
			       DWORD TxLength, PUCHAR RxBuffer, PDWORD RxLength,
			       PSCARD_IO_HEADER RecvPci)
{
	reader_t *rd = get_reader(Lun);
	rw5100_reply_t rep;
	int r;

	if (!rd) {
		*RxLength = 0;
		return IFD_COMMUNICATION_ERROR;
	}
	if (TxLength == 0 || TxLength > 255) {
		/* 枠の長さは 1 バイトなので、255 バイトまでしか送れない */
		*RxLength = 0;
		return IFD_NOT_SUPPORTED;
	}

	pthread_mutex_lock(&rd->lock);
	if (rd->atr_len == 0) {
		pthread_mutex_unlock(&rd->lock);
		*RxLength = 0;
		return IFD_ICC_NOT_PRESENT;
	}
	r = rw5100_apdu(&rd->rw, TxBuffer, TxLength, &rep);
	pthread_mutex_unlock(&rd->lock);

	if (r != RW5100_OK) {
		*RxLength = 0;
		LOG("APDU の送受信に失敗: %s", rw5100_strerror(r));
		return to_ifd(r);
	}
	if (rep.len > *RxLength) {
		*RxLength = 0;
		return IFD_ERROR_INSUFFICIENT_BUFFER;
	}
	memcpy(RxBuffer, rep.data, rep.len);
	*RxLength = (DWORD)rep.len;
	if (RecvPci) {
		RecvPci->Protocol = SendPci.Protocol;
		RecvPci->Length = sizeof(*RecvPci);
	}
	return IFD_SUCCESS;
}

RESPONSECODE IFDHControl(DWORD Lun, DWORD dwControlCode, PUCHAR TxBuffer, DWORD TxLength,
			 PUCHAR RxBuffer, DWORD RxLength, LPDWORD pdwBytesReturned)
{
	(void)Lun; (void)dwControlCode; (void)TxBuffer; (void)TxLength;
	(void)RxBuffer; (void)RxLength;
	if (pdwBytesReturned)
		*pdwBytesReturned = 0;
	return IFD_ERROR_NOT_SUPPORTED;
}

RESPONSECODE IFDHICCPresence(DWORD Lun)
{
	reader_t *rd = get_reader(Lun);
	uint8_t st = 0, state = 0;
	int r;

	if (!rd)
		return IFD_COMMUNICATION_ERROR;

	pthread_mutex_lock(&rd->lock);
	r = rw5100_status(&rd->rw, &st, &state);
	if (r == RW5100_OK && !(st & RW5100_STATUS_PRESENT))
		rd->atr_len = 0;
	pthread_mutex_unlock(&rd->lock);

	if (r != RW5100_OK)
		return to_ifd(r);
	return (st & RW5100_STATUS_PRESENT) ? IFD_ICC_PRESENT : IFD_ICC_NOT_PRESENT;
}
