# Android版のビルド

Javaで作ったネイティブアプリです。Android SDK 35、Java 17以降、Android Gradle Plugin 8.11.1、Gradle 8.13を使用します。外部のアプリ用ライブラリーはありません。Android 8 / API 26以降向け、targetSdk 35。Android 16のエミュレーターで実行確認しています。

日本語フォントは `app/src/main/res/font` のZen Kaku Gothic New Regular / Boldです。配布条件はPC版と共通の `../Licenses/ZenKakuGothicNew-OFL.txt` を参照してください。文字サイズ150%の追加検証では、instrumentに `-e layoutOnly true -e scenario portrait150`（またはlandscape150 / narrow150）を指定します。端末の文字サイズはテスト前に設定し、終了後に戻します。

WindowsのPowerShellで、このAndroidディレクトリーから実行：

```powershell
$env:JAVA_HOME='C:\Program Files\Android\Android Studio\jbr'
$env:ANDROID_HOME="$env:LOCALAPPDATA\Android\Sdk"
.\gradlew.bat assembleRelease lintRelease --max-workers=1 --no-watch-fs
```

APKは `app/build/outputs/apk/release/app-release.apk`。標準のAndroidデバッグ署名を使い、本体のデバッグは無効です。別PCで同じapplicationIdの更新版を配布する場合は、以前のAPKと同じ署名を使ってください。秘密鍵は同梱しません。

## 無音の通信・操作テスト

PCのStagePilotに以下の引数を付けると、音声デバイスを作らないオフラインの接続先を起動します。通常の演奏アプリは起動しません。待ち受けはループバックのみです。

```text
StagePilot.exe --remote-fixture C:\Temp\stagepilot-remote-test 19780 stagepilot-test
```

エミュレーターは音声を無効にして起動し、デバイスのシリアルを明示します。

```powershell
.\gradlew.bat assembleDebug assembleDebugAndroidTest --max-workers=1 --no-watch-fs
adb -s emulator-5554 install --no-streaming -r app/build/outputs/apk/debug/app-debug.apk
adb -s emulator-5554 install --no-streaming -r app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk
adb -s emulator-5554 shell am instrument -w -e host 10.0.2.2 -e port 19780 jp.stagepilot.remote.test/jp.stagepilot.remote.RemoteInstrumentation
```

テストはこのリモコンアプリの設定を初期化するため、普段使う実機ではなく専用エミュレーターで行ってください。PC接続先は15分で自動終了し、指定フォルダーに `stop.flag` を作っても終了します。HTTP/TCP/OSCのPC単体通信テストは `StagePilot.exe --remote-test 絶対パスのreport.json`。

## リモコン0.17

指定した移調値へ切り替えるボタンを追加しました。設定方法は `../スマホ移調ボタンの使い方.md`。既存のボタンは値が未保存なら0として読み込むため、旧版のリセット操作も保持します。

## リモコン0.18

＋1／−1の増減、ページごとの1〜32ボタンと自動／1〜4列を追加しました。増減にはPC版0.18が必要です。従来の24個は新しい96枠の元のページ・位置へ引き継ぎます。ボタン数を減らしても割り当ては保持します。設定は `../スマホ連携の使い方.md` を参照してください。
