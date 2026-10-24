# Audio implementation references and limits

The selected GAME73 renderer/transport and portable PS-ADPCM decoder are finite project code. The envelope behavior reference recorded in `src/game73_selected.h` is PPSSPP commit `1adf3c553d189539c8307ab30d6204ab3ebb1aac` (GPL2+). The project records an independently structured finite implementation; it does not distribute/link PPSSPP source or an emulator. This note preserves attribution, not proof of firmware equivalence.

FFmpeg7.1.1 is a separately supplied optional dependency and retains its own license/configuration obligations. No FFmpeg DLL/source is included in this repository. Existing `LICENSES/` notices remain unchanged.

The working selected path is SE_SYS group0/entry4 with a bounded waveform/envelope/native resampling/output policy. Device Start/drain observations do not prove audible/PSP equivalence. Other sequences, multivoice mixing, loops, wet effects and full SGX/ME/SAS are unfinished. The newer Options group0/event6 path currently stops before an unknown Port field after control-memory effects. An aligned SAS control-state representation is not a SAS DSP/render/backend implementation.

Original sound banks and samples remain user inputs and are not covered by the project's MIT license.
