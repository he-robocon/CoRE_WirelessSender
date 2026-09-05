# CoreS3 USBコントローラーLAN送信

M5Stack CoreS3、USB Module V1.2、LAN Module13.2を使い、HORI
ホリパッド TURBO for Nintendo Switch 2（NSX-065）の入力を別のCoreS3へ
UDPで送信します。

- `sender/`: USBコントローラーを読み取り、`192.168.10.20:5000`へ送信
- `receiver/`: 受信した8バイトのHID生データを画面とシリアルへ表示
- `usb_probe/`: USBだけを接続して確認する診断用スケッチ

## DIP・ジャンパー設定

両方のCoreS3でSPIは`SCLK=GPIO36`、`MOSI=GPIO37`、`MISO=GPIO35`を
使用します。

### 送信側

| モジュール | 信号 | 設定 | CoreS3 GPIO |
| --- | --- | --- | ---: |
| USB V1.2 | SS | CH2（シルクG5） | 1 |
| USB V1.2 | INT | CH2（シルクG34） | 14 |
| LAN Module13.2 | CS | M5-Bus 23 | 13 |
| LAN Module13.2 | INT | GPIO10側 | 10 |
| LAN Module13.2 | RST | M5-Bus 24 | 0 |

USBのINTがGPIO14を使用するため、送信側LANのINTをGPIO14へ接続しないで
ください。LANの割り込みはプログラムでは使用しませんが、同じ信号線へ二つの
モジュールを接続するとUSB通信へ影響します。

### 受信側

| モジュール | 信号 | 設定 | CoreS3 GPIO |
| --- | --- | --- | ---: |
| LAN Module13.2 | CS | M5-Bus 20 | 1 |
| LAN Module13.2 | INT | GPIO10側 | 10 |
| LAN Module13.2 | RST | M5-Bus 24 | 0 |

コントローラー本体のモード切替は`Nintendo Switch 2`側にします。

## ネットワーク設定

| 機器 | IPv4アドレス | UDPポート | MACアドレス |
| --- | --- | ---: | --- |
| 送信側 | `192.168.10.10/24` | 5001 | `02:43:4f:52:45:10` |
| 受信側 | `192.168.10.20/24` | 5000 | `02:43:4f:52:45:20` |

送信データにはシーケンス番号、送信時刻、HIDレポート長、8バイトのHID
レポートが入ります。受信側は各パケットへACKを返し、送信側に往復時間を
表示します。

確認したNSX-065の中立値は次のとおりです。

```text
00 00 0F 80 80 80 80 00
```

## Arduino CLI

M5Stack ESP32ボードパッケージ、M5Unified、Ethernet 2.0.2を使用します。
CoreS3対応の修正を含むUSB Host Shield Library 2.0は`libraries/`内にあります。

```sh
arduino-cli lib install M5Unified@0.2.21
arduino-cli lib install Ethernet@2.0.2
```

送信側：

```sh
arduino-cli compile \
  --fqbn m5stack:esp32:m5stack_cores3 \
  --libraries ./libraries \
  sender
```

受信側：

```sh
arduino-cli compile \
  --fqbn m5stack:esp32:m5stack_cores3 \
  --libraries ./libraries \
  receiver
```

正常動作時、送信側には`STREAMING`、受信側には`RECEIVING`と表示されます。
シリアル通信速度は両方とも115200 bpsです。
