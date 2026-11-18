# StagePilot local compatibility changes

Based on FST 0.177.0, GPL-3.0-or-later, upstream https://git.iem.at/zmoelnig/FST.
The unmodified release archive is included as `fst_0.177.0.orig.tar.bz2`.
Source download: https://deb.debian.org/debian/pool/main/f/fst/fst_0.177.0.orig.tar.bz2
SHA-256: `580fdfb93789a3842c913ab6ec63b0539271758e1b05470a116ba36fdfd5a300`.

Local edits in `pluginterfaces/vst2.x/fst.h`:

- `MidiKeyName.keyName` has fixed storage for the documented `kVstMaxNameLen`, permitting stack initialization in JUCE on MSVC.
- `AEffect.flags` uses a 32-bit field. Previously unnamed padding fields are explicit. These changes preserve the Windows x64 offsets.
- `SpeakerArrangement.speakers` has eight entries. JUCE's `VstSpeakerConfigurationHolder::allocate` allocates `sizeof(VstSpeakerArrangement)` plus channels beyond eight; a flexible array here caused heap writes past the allocation for ordinary stereo plugins.
- `PinProperties` includes 48 reserved bytes. Native plugins may write the complete 128-byte record.
- Compile-time size checks guard the latter two allocations.

The Windows x64 build is tested against the separately installed Keyzone Classic and Vital plugins. These patches do not claim complete VST2 compatibility on other platforms.
