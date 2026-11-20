# 開発のための情報

## ファイルの構成

| ファイル | 内容 |
|---------|------|
| `src/rw5100.c`・`src/rw5100.h` | RW-5100 との通信（枠の組み立て、チェックサム、通し番号）。pcscd には依存しない |
| `src/ifdhandler.c` | pcscd から呼ばれる IFD handler（IFD Handler API 3.0） |
| `src/Info.plist.in` | pcscd に読ませる bundle の設定の雛形 |
| `tools/rw5100-probe.c` | 試験の道具 |
| `udev/92-ifd-rw5100.rules` | pcscd が RW-5100 を開けるようにする udev のルール |

## make install でインストールされるもの

| 対象 | 場所 |
|---------|------|
| ドライバー | `/usr/lib/pcsc/drivers/ifd-rw5100.bundle` |
| udev のルール | `/usr/lib/udev/rules.d/92-ifd-rw5100.rules` |

ドライバーの場所は `pkg-config --variable=usbdropdir libpcsclite` で決まります。`make install` の変数 `USBDROPDIR`・`UDEVDIR`・`DESTDIR` で変更できます。

## pcscd の制約

- pcscd はユーザー `pcscd` で動作します。udev のルールは、RW-5100 のグループを `pcscd` にして、pcscd が開けるようにしています（libccid のルールと同じ考え方）。
- systemd の設定（`ProtectHome=yes`）により、pcscd からはホームディレクトリの中が見えません。開発中のビルドをホームの中に置いたまま、リンクで pcscd に読み込ませることはできません。
- pcscd はドライバーの一覧を起動時にのみ読み込むため、ドライバーを入れ替えたら pcscd を再起動してください。
- pcscd が読み込んでいる `.so` を上書きすると、pcscd が異常終了します。置き換えるときは、別名でコピーしてから名前を変更してください。

## 試験の道具 rw5100-probe

`make` で `build/rw5100-probe` が生成されます。pcscd を通さずに RW-5100 と直接通信し、版・カードの状態・ATR を表示します。カードには APDU を送りません。

pcscd が RW-5100 を使用している間は開けないため、pcscd を停止してから使用してください。udev のルールで RW-5100 のグループが `pcscd` になっている場合は、そのグループの権限か root が必要です。

```sh
./build/rw5100-probe -v        # 送受信した枠も表示する
./build/rw5100-probe -w 30     # 30 秒間、カードの抜き挿しを見る
./build/rw5100-probe -r        # ATR を読んだあと、ウォームリセットして読み直す
```

## デバッグの表示

pcscd に環境変数 `IFD_RW5100_DEBUG=1` を渡すと、送受信した枠を stderr に出力します（`pcscd -f -d` で確認できます）。APDU の中身も出力されるため、ログの扱いに注意してください。

リーダーが枠を受け付けなかった時と通し番号が合わない時は、この設定がなくても、枠の先頭の 3 バイトのみを stderr に出力します。
