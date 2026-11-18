# Source and third-party notices

StagePilot application source in `Source/` and its build configuration are provided under the GNU Affero General Public License, version 3 or later. See `LICENSE.txt`. The build pins the exact upstream JUCE commit used to build the executable. No third-party instrument binaries, presets, or samples are included. The demo uses the application's own generated sine-wave instrument.

JUCE 8.0.17, commit `91ad83ae34a81e0833b1a2b0866f54846370ae53`, is used under its AGPLv3 option. Its full source, including bundled dependency notices, is available from the upstream link below. JUCE's license overview is in `Licenses/JUCE.md`. The bundled ASIO headers are used under their GPLv3 option; see `Licenses/ASIO.txt` and `Licenses/GPL-3.0.txt`. VST3 SDK is under MIT; see `Licenses/VST3.txt`.

Graphics and codec dependency notices are included in `Licenses/` for HarfBuzz, SheenBidi, JPEG, PNG, Zlib, FLAC, and Ogg Vorbis. The dependency source archive retains all original copyright and license notices.

Upstream source: [JUCE 8.0.17](https://github.com/juce-framework/JUCE/tree/8.0.17).

To rebuild with a local JUCE checkout at the specified revision, pass its absolute path as `-DJUCE_SOURCE_DIR=...` to CMake. CMake expects that directory to contain JUCE's `CMakeLists.txt`.

The VST2 compatibility headers in `ThirdParty/FST/` come from Free Studio Technology (FST) 0.177.0, released under GPLv3-or-later. The original source archive and license are retained there. StagePilot corrects fixed buffer storage for JUCE/MSVC, including speaker arrangements and pin properties. See `ThirdParty/FST/STAGEPILOT_PATCHES.md` for provenance, checksums, and all local changes. No Steinberg SDK binaries are included.

Vital, PianoOne, and Keyzone Classic are names of separately installed third-party plugins used in local compatibility testing. They are not part of this package and no affiliation is implied.

The Android companion source is in Android/. APKs are available under GitHub Releases. Signing keys are not included.

Zen Kaku Gothic New Regular and Bold are embedded in the Windows executable and Android APK. Copyright 2022 The Zen Project Authors. These unmodified fonts are distributed under the SIL Open Font License 1.1; see `Licenses/ZenKakuGothicNew-OFL.txt`. Font files are included in `Assets/Fonts/` and Android resources for reproducible builds. Source: [Google Fonts](https://github.com/google/fonts/tree/main/ofl/zenkakugothicnew), [Zen Kaku Gothic upstream](https://github.com/googlefonts/zen-kakugothic). No OS-wide font installation is required.


## Windows Media Foundation

カメラと動画の処理はWindows標準のMedia Foundation APIを使用します。OSの動画DLLやFFmpegは同梱しません。FFmpeg/ffprobeは開発環境での検証だけに使用しています。
