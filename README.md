# ifd-rw5100

SHARP の USB IC カードリーダー RW-5100 を Linux の pcscd（PC/SC）で使うためのドライバーです。

**開発中です。まだ動きません。**

RW-5100（USB ID `04dd:9259`）は、USB の CCID クラスではなく、メーカー独自の方式で通信します。そのため、pcscd の標準のドライバー（libccid）では認識されません。このドライバーは、libusb で RW-5100 と直接通信し、pcscd からは普通の PC/SC リーダーとして見えるようにします。

## 対応する機器

| 機器 | USB ID | 状態 |
|------|--------|------|
| SHARP RW-5100 | `04dd:9259` | 開発中 |

## 必要なもの

- Linux と pcscd（pcsc-lite）
- libusb 1.0
- ビルドするには、C コンパイラー・make・pkg-config、libusb と pcsc-lite の開発用ファイル

Debian・Ubuntu では、次のコマンドでそろいます。

```sh
sudo apt install build-essential pkg-config libusb-1.0-0-dev libpcsclite-dev pcscd pcsc-tools
```

## ビルドと導入

TODO: ビルドの手順ができたら書く。

## 通信の方式

[docs/protocol.md](docs/protocol.md) に、分かっている範囲をまとめています。

## ライセンス

LGPL-2.1-or-later です。全文は [LICENSE](LICENSE) にあります。

SHARP はシャープ株式会社の商標です。このドライバーはシャープ株式会社とは関係がなく、シャープ株式会社のソフトウェアを含みません。
