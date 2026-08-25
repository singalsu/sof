# microWakeWord (MWW) Training & Tuning Toolchain

This directory contains offline scripts to train, quantize, verify, and export streaming keyword-spotting models for the SOF `microwakeword` (MWW) module using real SOF MFCC features extracted via `sof-testbench4`.

---

## Toolchain Overview

1. **Synthetic Data Synthesis:**
   - [sof_mww_generate_keyword_dataset_piper_tts.sh](sof_mww_generate_keyword_dataset_piper_tts.sh): Synthesizes positive keyword WAVs with single-speaker Piper TTS voices, pitch/tempo perturbation (via `sox`), Gaussian gain jitter, and Room Impulse Response (RIR) reverberation.
   - [sof_mww_generate_keyword_dataset.sh](sof_mww_generate_keyword_dataset.sh): Multi-speaker generation with `piper-sample-generator` (LibriTTS-R).
   - [sof_mww_prepare_silence_unknown.sh](sof_mww_prepare_silence_unknown.sh): Prepares negative dataset classes (`silence` and `unknown`) by slicing background noise and non-target words from Google Speech Commands v2.

2. **DSP Feature Extraction:**
   - [sof_mfcc_extract_features.sh](sof_mfcc_extract_features.sh): Emits 184-byte hop records ($24\text{-byte } \texttt{struct mfcc\_data\_header} + 40 \times \text{int32\_t}$ Q9.23 mel values) by running the SOF host testbench with 10 ms mel-40 topology.

3. **Model Training & Export:**
   - [sof_mww_dataset.py](sof_mww_dataset.py): Parses testbench `.raw` feature files, applies soft mel-log AGC ($0\text{ dB}$ target, $0.5\text{ dB/s}$ release recovery), and builds training slices.
   - [sof_mww_train.py](sof_mww_train.py): Trains a streaming Keras model, quantizes to `int8` with TFLite Micro resource variables, packages behind SOF IPC4 ABI headers, and emits:
     - `mww_model_data.{cc,h}`: Drop-in static C array.
     - `<name>.conf`: ALSA Topology v2 configuration blob for `Object.Base.data`.
     - `<name>.txt`: Runtime `sof-ctl` binary control file.

4. **Off-Device Verification & Automation:**
   - [sof_mww_verify.py](sof_mww_verify.py): Feeds 3-hop streaming slices through the quantized TFLite model to verify detection rates and false alarm rates.
   - [sof_mww_train_pipeline.sh](sof_mww_train_pipeline.sh): End-to-end automation runner.

---

## Quickstart

### 1. Generate Synthetic Keyword Data

#### Option A: Multi-speaker dataset with `piper-sample-generator` (PyTorch `.pt` model)
```bash
./sof_mww_generate_keyword_dataset.sh \
    --keyword "hey jarvis" \
    --label hey_jarvis \
    --model ~/git/piper-sample-generator/models/en_US-libritts_r-medium.pt \
    ~/wov/wavs
```

#### Option B: Single-speaker dataset with `piper-tts` (ONNX `.onnx` model)
```bash
./sof_mww_generate_keyword_dataset_piper_tts.sh \
    --keyword "hey jarvis" \
    --label hey_jarvis \
    --voice ~/.local/share/piper-voices/en_US-lessac-medium.onnx \
    ~/wov/wavs
```

### 2. Run End-to-End Retraining Pipeline
```bash
./sof_mww_train_pipeline.sh \
    --keyword hey_jarvis \
    --name hey_jarvis \
    ~/wov/wavs ~/wov/feats ~/wov/model
```
