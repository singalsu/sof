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

### Environment Setup

#### Setup Piper Virtual Environment (for Dataset Generation)
```bash
python3 -m venv ~/venvs/piper
source ~/venvs/piper/bin/activate
pip install --upgrade pip
pip install piper-sample-generator
```

#### Setup `piper-sample-generator` and Model Checkpoint
```bash
git clone https://github.com/rhasspy/piper-sample-generator ~/git/piper-sample-generator
mkdir -p ~/git/piper-sample-generator/models
wget -O ~/git/piper-sample-generator/models/en_US-libritts_r-medium.pt \
      https://github.com/rhasspy/piper-sample-generator/releases/download/v2.0.0/en_US-libritts_r-medium.pt
```

#### Setup Model Training Virtual Environment (Python 3.10 via `uv`)
For systems with newer default Python versions (such as Ubuntu 24.04/26.04), install `uv` to manage a Python 3.10 environment required by TensorFlow 2.16:
```bash
curl -LsSf https://astral.sh/uv/install.sh | sh
uv venv ~/venvs/mww-train --python 3.10
source ~/venvs/mww-train/bin/activate
uv pip install --upgrade pip
uv pip install "tensorflow==2.16.1" "tf-keras==2.16.0"
```

### 1. Generate Synthetic Keyword Data

#### Option A: Multi-speaker dataset with `piper-sample-generator` (PyTorch `.pt` model)
```bash
./sof_mww_generate_keyword_dataset.sh \
    --keyword "hey jarvis" \
    --label hey_jarvis \
    --venv ~/venvs/piper \
    ~/wov/wavs
```
By default, the script automatically uses:
- `PIPER_MODEL="$HOME/git/piper-sample-generator/models/en_US-libritts_r-medium.pt"` (auto-detected)
- `MAX_SAMPLES=1000` (clips synthesized per speaking-rate variation)
- `MAX_SPEAKERS=200` (cap on LibriTTS-R speaker indices for diverse, high-quality voices)
- `GAIN_AUG=1` (Gaussian level jitter enabled: peak target -10 dBFS, sigma 5 dB)

These can be overridden via environment variables if desired (e.g., `MAX_SAMPLES=1500 MAX_SPEAKERS=300 ./sof_mww_generate_keyword_dataset.sh ...`).

#### Option B: Single-speaker dataset with `piper-tts` (ONNX `.onnx` model)
```bash
./sof_mww_generate_keyword_dataset_piper_tts.sh \
    --keyword "hey jarvis" \
    --label hey_jarvis \
    --voice ~/.local/share/piper-voices/en_US-lessac-medium.onnx \
    ~/wov/wavs
```

### 2. Run End-to-End Retraining Pipeline
With the `mww-train` virtual environment activated:
```bash
source ~/venvs/mww-train/bin/activate
./sof_mww_train_pipeline.sh \
    --keyword hey_jarvis \
    --name hey_jarvis \
    ~/wov/wavs ~/wov/feats ~/wov/model
```

---

### Training Multiple Keyword Variants into the Same Model

You can train a single wake-word model to trigger on multiple spoken variants (such as `"hey jarvis"` and `"hi jarvis"`) with **no increase in model size or DSP RAM**:

#### 1. Generate Datasets for Both Phrases
```bash
./sof_mww_generate_keyword_dataset.sh \
    --keyword "hey jarvis" --label hey_jarvis \
    --keyword "hi jarvis"  --label hi_jarvis \
    --venv ~/venvs/piper \
    ~/wov/wavs
```

#### 2. Train with Multiple `--keyword` Arguments
Pass both directory labels to the training pipeline and specify `--name hey_jarvis`:
```bash
source ~/venvs/mww-train/bin/activate
./sof_mww_train_pipeline.sh \
    --keyword hey_jarvis \
    --keyword hi_jarvis \
    --name hey_jarvis \
    ~/wov/wavs ~/wov/feats ~/wov/model
```

Both phrase directories are pooled as the positive target class ($y = 1$). The streaming verification report provides individual detection recall for each phrase, and the single exported `mww_model_data.cc` triggers when either phrase is spoken.

---

### Visualizing Streaming Diagnostics from mtrace

You can visualize real-time hop metrics, energy/noise floor, int8 feature quantization, and detection probability curves captured from device `mtrace` logs:

```bash
# Capture mtrace output from device:
sof-mtrace-reader > /tmp/mtrace.txt

# Plot streaming diagnostics:
./sof_mww_plot_mtrace.py /tmp/mtrace.txt -o /tmp/mww_diag.png
```
