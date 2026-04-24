# AudioFiles Import Notes

This directory is imported from `../AudioFiles` so all audio-related references are kept inside this repo.

- `starterkit/`: Terasic/Intel-style WM8731 codec reference modules.
- `wolfson.v`, `wolfson.do`: external reference files kept for bring-up comparison.

Current active audio path in this project is the custom `fighter_audio_wm8731` component
(`hw/fighter_audio.sv`) instantiated through Platform Designer (`soc_system.qsys`).

These imported files are currently reference assets and are not required for the existing
build output unless you explicitly swap the audio IP implementation.