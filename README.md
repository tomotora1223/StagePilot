# StagePilot 0.21

StagePilot は Windows 向けのライブ演奏用 VST2・VST3 ホストです。MIDI キーボードをつなぎ、音源の重ね合わせ・スプリット・移調を設定して、単体音源と組み合わせた音色を同じ一覧から切り替えられます。ASIO と WASAPI に対応しています。

[GitHub Releases から入手](https://github.com/tomotora1223/StagePilot/releases/latest)

Windows は ZIP を書き込みできるフォルダーに展開し、StagePilot.exe または「音ありで開く.cmd」を実行します。Android は Releases の APK を入れ、PC の「スマホ連携」に出る IP・ポート・接続キーを入力します。

## 音源の検索

「音源を追加」からインストール済みの音源を開きます。「検索先…」でフォルダーを追加・削除できます。標準と追加した検索先をまとめて調べます。検索中は見つかっている音源を選べます。「もう一度検索」は保存済みの結果も再確認します。停止したときは確認済みの結果を保持します。

## Android リモコン

「編集」で好きな枠を開き、名前・色・操作を設定します。「次の操作を追加」で最大8操作を上から順番に割り当てられます。例えば音色を切り替え、全体移調を +2、全体音量を 60%、効果音を再生、という操作を一度に送れます。全体音量の増減や、指定音源の現在の音量も設定できます。

「ピッチベンドのレバー」を選ぶと、その枠が横方向に動かせるレバーになります。指を離すと中央へ戻り、通信が途切れた場合は PC 側で約0.6秒後に復帰します。ベンド幅は各音源側の設定に従います。新しい操作には Windows 版・Android 版の両方に0.21以降が必要です。

## Windows の自動更新

起動時に GitHub Releases の最新安定版を確認し、更新があれば ZIP を取得します。SHA-256 を照合してから、アプリを閉じた後に実行ファイルを置き換えます。次回の起動から新しい版になります。巻き戻す場合はアプリを閉じ、フォルダーに残る StagePilot.previous.exe を使ってください。自動更新を止めるにはアプリと同じフォルダーに no-auto-update という空のファイルを置きます。Android の更新は Releases の APK を端末でインストールします。

## ビルド

Windows x64、Visual Studio 2022 の C++ と Windows SDK、CMake 3.22 以降が必要です。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

JUCE を事前に取得済みの場合は構成時に -DJUCE_SOURCE_DIR=... を渡せます。Android は Android SDK 35 を設定し、Android ディレクトリで gradlew.bat assembleDebug を実行します。配布用APKの署名鍵はリポジトリに含まれません。

本体は AGPL-3.0-or-later です。依存ライブラリやフォントの出典は THIRD_PARTY_NOTICES.md を参照してください。
