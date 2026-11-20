# ifd-rw5100

SHARP の USB IC カードリーダー RW-5100 を Linux の pcscd（PC/SC）で使うためのドライバーです。

**開発中です。** pcscd でリーダーとして認識され、カードの抜き挿し、ATR の読み取り、APDU のやり取りまで動きます。試したのは 1 種類のカード（T=1）だけです。

RW-5100（USB ID `04dd:9259`）は、USB の CCID クラスではなく、メーカー独自の方式で通信します。そのため、pcscd の標準のドライバー（libccid）では認識されません。このドライバーは、libusb で RW-5100 と直接通信し、pcscd からは普通の PC/SC リーダーとして見えるようにします。

## 対応する機器

| 機器 | USB ID | 状態 |
|------|--------|------|
| SHARP RW-5100 | `04dd:9259` | 認識・カードの抜き挿し・ATR・APDU を確認（T=1 のカード 1 種類） |

## 必要なもの

- Linux と pcscd（pcsc-lite）
- libusb 1.0
- ビルドするには、C コンパイラー・make・pkg-config、libusb と pcsc-lite の開発用ファイル

Debian・Ubuntu では、次のコマンドでそろいます。

```sh
sudo apt install build-essential pkg-config libusb-1.0-0-dev libpcsclite-dev pcscd pcsc-tools
```

## ビルドと導入

```sh
make
sudo make install
sudo udevadm control --reload
sudo systemctl restart pcscd
```

`make install` は次の 2 つを置きます。

- ドライバー: `/usr/lib/pcsc/drivers/ifd-rw5100.bundle`（場所は `pkg-config --variable=usbdropdir libpcsclite` で決まります）
- udev のルール: `/usr/lib/udev/rules.d/92-ifd-rw5100.rules`（pcscd が RW-5100 を開けるように、グループを `pcscd` にします）

udev のルールは、次に RW-5100 を挿したときから効きます。挿したままなら、一度抜いて挿し直してください。

外すときは `sudo make uninstall` を使います。

## 使い方

導入すると、pcscd からは `SHARP RW-5100` という名前のリーダーとして見えます。pcsc-tools の `pcsc_scan` で確かめられます。

```sh
pcsc_scan
```

`make` では、試験の道具 `build/rw5100-probe` もできます。pcscd を通さずに RW-5100 と直接通信して、版・カードの状態・ATR を表示します。pcscd が RW-5100 を使っている間は開けないので、pcscd を止めてから使ってください。

```sh
./build/rw5100-probe -v        # 送受信した枠も表示する
./build/rw5100-probe -w 30     # 30 秒間、カードの抜き挿しを見る
```

## 分かっている制限

- 送れる APDU は 255 バイトまでです（枠の長さが 1 バイトのため）。
- pcscd が電源を切ろうとしても、カードには電源が入ったままです。Windows の公式ドライバーも電源を切る命令を送らないので、同じ動きにしています。
- pcscd が起動した直後の 1 秒ほどは、カードが入っていても「カードなし」と見えることがあります。
- T=0 のカードは試していません。
- pcscd のデバッグ表示で送受信した枠を見るには、pcscd に環境変数 `IFD_RW5100_DEBUG=1` を渡します。

## 通信の方式

[docs/protocol.md](docs/protocol.md) に、分かっている範囲をまとめています。

## ライセンス

LGPL-2.1-or-later です。全文は [LICENSE](LICENSE) にあります。

SHARP はシャープ株式会社の商標です。このドライバーはシャープ株式会社とは関係がなく、シャープ株式会社のソフトウェアを含みません。
