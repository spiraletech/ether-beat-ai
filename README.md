# ETHERBEAT

Windows-native private generative music workstation for the EtherTech ecosystem.

## Thesis

ETHERBEAT is not intended to be a public prompt-to-song vending machine. It is a local-first producer instrument built around private, permissioned musical DNA, controllable generation, variation, resampling, and tight integration with EtherPlayer and HAKUI.

The source/runtime boundary is deliberate: the application shell can evolve publicly while the Pleiadian corpus, vocals, stems, taste history, LoRAs, checkpoints, and future proprietary model assets remain private and local.

## V0.1 foundation

- C++20 core
- Portable CLI harness
- Native Windows `ETHERBEAT.exe` shell
- Swappable `IModelBackend` inference contract
- Typed generation request + artifact lineage
- Seed, duration, BPM, key, mutation, mode, and reference-audio fields
- 48 kHz stereo WAV artifact pipeline
- `.etherbeat.json` generation metadata sidecars
- Strict `.gitignore` boundary for private corpus/model material

The current `MockWaveBackend` intentionally generates silence. Its purpose is to prove the complete request -> backend -> WAV -> metadata path before a heavyweight local model runtime is attached.

## Architecture

```text
                    ETHERBEAT.exe
                         |
                    GenerationRequest
                         |
                     ModelRouter
                         |
                  IModelBackend
                    /         \
       MockWaveBackend       future local AI backend
              |                     |
       WAV + lineage          generated music/stems
              \_____________________/
                         |
               EtherPlayer / HAKUI IPC
```

## Planned model path

1. Keep the native application independent from any single model vendor/runtime.
2. Add a local inference bridge behind `IModelBackend`.
3. Add permissioned Pleiadian training/caption data outside this repository.
4. Add private adapter / LoRA selection and versioning.
5. Add variation, extend, audio-to-audio, stem, and synesthesia controls.
6. Add EtherPlayer audition/library handoff and HAKUI IPC.

## Build

```bash
cmake -S . -B build
cmake --build build --config Release
```

On Windows, CMake builds the native `ETHERBEAT` target in addition to the portable `etherbeat` CLI harness.

CLI prototype:

```bash
./build/etherbeat "haunted alien workshop" 10
```

## Repository safety boundary

**Never commit:**

- private corpus audio
- producer stems or vocals
- datasets / training manifests containing private material
- checkpoints / LoRAs / model weights
- private prompt or taste databases
- API keys or secrets
- private generations

The repository `.gitignore` blocks the expected local directories and common model-weight formats. Keep source licensing and dataset/model licensing separate.
