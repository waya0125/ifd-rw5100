# ifd-rw5100

SHARP の USB IC カードリーダー RW-5100 を Linux の pcscd（PC/SC）で使うためのドライバーです。

RW-5100（USB ID `04dd:9259`）はメーカー独自の方式で通信するため、pcscd の標準のドライバー（libccid）では認識されません。このドライバーを入れると、pcscd から普通の PC/SC リーダーとして使えます。

カードの抜き挿しの検出、ATR の読み取り、APDU のやり取りまで動作します。T=0 のカードには対応していません。

## 必要なもの

- Linux と pcscd（pcsc-lite）
- libusb 1.0
- ビルドには C コンパイラー・make・pkg-config と、libusb・pcsc-lite の開発用ファイル

Debian・Ubuntu では、次のコマンドでそろいます。

```sh
sudo apt install build-essential pkg-config libusb-1.0-0-dev libpcsclite-dev pcscd pcsc-tools
```

## 導入

```sh
make
sudo make install
sudo udevadm control --reload
sudo systemctl restart pcscd
```

udev のルールは RW-5100 を次に挿したときから有効になるため、挿したままの場合は一度抜いて挿し直してください。

## 動作の確認

```sh
pcsc_scan
```

`SHARP RW-5100` という名前のリーダーが表示され、カードを挿したときに ATR が出力されれば、正しく動作しています。

## アンインストール

```sh
sudo make uninstall
sudo systemctl restart pcscd
```

## 主な制限

- T=0 のカードは使用できません。RW-5100 自体が対応していないとみられます。
- 送れる APDU は 255 バイトまでです。
- カードの電源は切れません（Windows の公式ドライバーも電源を切る命令を送りません）。

その他の制限と動作は [docs/limitations.md](docs/limitations.md) にまとめています。

## ドキュメント

- [docs/limitations.md](docs/limitations.md): 制限と動作の詳しい説明、確認したカードと環境
- [docs/development.md](docs/development.md): 試験の道具、デバッグの表示、`make install` でインストールされるもの
- [docs/protocol.md](docs/protocol.md): RW-5100 の通信方式

## ライセンス

LGPL-2.1-or-later です。全文は [LICENSE](LICENSE) にあります。

SHARP はシャープ株式会社の商標です。このドライバーはシャープ株式会社とは関係がなく、シャープ株式会社のソフトウェアを含みません。
