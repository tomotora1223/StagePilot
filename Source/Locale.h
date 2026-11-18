#pragma once
#include <JuceHeader.h>
#include "Typography.h"
inline void installJapanese() {
    juce::LocalisedStrings::setCurrentMappings(new juce::LocalisedStrings(juce::String::fromUTF8(R"translations(
language: Japanese
countries: jp
"none" = "なし"
"Audio device type:" = "出力方式:"
"Output:" = "出力機器:"
"Device:" = "使用する機器:"
"Input:" = "入力機器:"
"Sample rate:" = "サンプリング周波数:"
"Audio buffer size:" = "バッファーサイズ:"
"Active output channels:" = "使う出力チャンネル:"
"Active input channels:" = "使う入力チャンネル:"
"Active MIDI inputs:" = "使うMIDI入力にチェック:"
"MIDI Output:" = "MIDI出力:"
"No MIDI inputs available" = "MIDI機器が見つかりません"
"(no audio output channels found)" = "出力チャンネルが見つかりません"
"(no audio input channels found)" = "入力チャンネルが見つかりません"
"Control Panel" = "機器の設定画面"
"Opens the device's own control panel" = "オーディオ機器の専用設定画面を開きます"
"Reset Device" = "機器を再接続"
"Resets the audio interface - sometimes needed after changing a device's properties in its custom control panel" = "設定変更後に音が出ないとき、機器を再接続します"
"Test" = "テスト音を鳴らす"
"Plays a test tone" = "出力先を確認するためのテスト音を鳴らします"
"Show advanced settings..." = "詳しい設定を表示"
"Error when trying to open audio device!" = "出力機器に接続できませんでした"
"Bluetooth MIDI" = "Bluetooth MIDI"
"Scan for bluetooth MIDI devices" = "BluetoothのMIDI機器を探します"
"OK" = "閉じる"
"Cancel" = "キャンセル"
"Open" = "開く"
"Save" = "保存"
)translations"),false));
}
