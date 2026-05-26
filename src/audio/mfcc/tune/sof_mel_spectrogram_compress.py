"""Live scrolling mel spectrogram viewer for SOF compress PCM capture.

Displays a real-time scrolling mel spectrogram and VAD strip from ALSA
compress PCM capture (crecord) with embedded DSP VAD flag. No Whisper
inference — this is a lightweight visualization tool.

Frame format: [magic(int32), frame_number(uint32), reserved(int32),
               energy(int32), noise_energy(int32), vad_flag(int32),
               mel[0..79](int32)]

Usage:
    python sof_mel_spectrogram_compress.py [--card 0] [--device 48]
    python sof_mel_spectrogram_compress.py --width 200  # wider spectrogram
"""

import argparse
import os
import queue
import struct
import subprocess
import threading
import numpy as np
import matplotlib
matplotlib.use('TkAgg')
import matplotlib.pyplot as plt

# SOF compress mel frame format constants (with DSP data header)
SOF_MAGIC_BYTES = struct.pack('<i', 0x6D666363)  # ASCII 'mfcc' as int32
SOF_NUM_HEADER = 6            # magic, frame_number, reserved, energy, noise_energy, vad_flag
SOF_Q_FORMAT = 23            # Q9.23 fixed-point
SOF_NUM_MEL = 80
SOF_FRAME_INTS = SOF_NUM_HEADER + SOF_NUM_MEL  # 86 int32 per frame
SOF_FRAME_BYTES = SOF_FRAME_INTS * 4  # 344 bytes per frame
SOF_HOP_SEC = 0.01           # 10 ms per STFT hop

SPECTROGRAM_WIDTH = 300       # default number of frames visible


def decode_mel_frame(raw_ints):
    """Convert 80 int32 Q9.23 values to float32 mel coefficients."""
    return raw_ints.astype(np.float32) / (2 ** SOF_Q_FORMAT)


def parse_frame(buf):
    """Parse one complete mel frame from a bytearray buffer.

    Frame layout: [magic(4B), frame_number(4B), reserved(4B), energy(4B),
                   noise_energy(4B), vad_flag(4B), mel[0..79](320B)] = 344 bytes

    Mutates buf in-place (deletes consumed bytes).
    Returns: (frame_number, vad_flag, mel_ints) or (None, None, None)
    """
    idx = buf.find(SOF_MAGIC_BYTES)
    if idx < 0:
        if len(buf) > 3:
            del buf[:-3]
        return None, None, None
    end = idx + SOF_FRAME_BYTES
    if end > len(buf):
        del buf[:idx]
        return None, None, None

    frame_number = struct.unpack_from('<I', buf, idx + 4)[0]
    vad_flag = struct.unpack_from('<i', buf, idx + 20)[0]

    mel_bytes = bytes(buf[idx + SOF_NUM_HEADER * 4:end])
    mel_ints = np.frombuffer(mel_bytes, dtype=np.int32)
    del buf[:end]
    return frame_number, vad_flag, mel_ints


def run_spectrogram(card, device, width):
    """Capture compress PCM mel frames and display scrolling spectrogram."""

    mel_buf = np.zeros((SOF_NUM_MEL, width), dtype=np.float32)
    vad_buf = np.zeros(width, dtype=np.float32)
    x = np.arange(width)

    fig, (ax_mel, ax_vad) = plt.subplots(
        2, 1, figsize=(12, 5),
        gridspec_kw={'height_ratios': [5, 1]},
        sharex=True
    )
    fig.tight_layout(pad=2.0)

    im_mel = ax_mel.imshow(
        mel_buf, aspect='auto', origin='lower',
        interpolation='nearest', cmap='turbo',
        vmin=-2.0, vmax=2.0
    )
    ax_mel.set_ylabel('Mel bin')
    ax_mel.set_title('SOF Mel Spectrogram (compress PCM) — DSP VAD')
    fig.colorbar(im_mel, ax=ax_mel, fraction=0.02, pad=0.02)

    line_vad, = ax_vad.plot(
        x, vad_buf, color='green', linewidth=1.5,
        drawstyle='steps-post')
    ax_vad.set_ylabel('VAD')
    ax_vad.set_xlabel('Frame')
    ax_vad.set_ylim(-0.1, 1.1)
    ax_vad.set_yticks([0, 1])
    ax_vad.set_yticklabels(['Silent', 'Speech'])

    plt.ion()
    plt.show(block=False)
    fig.canvas.draw()
    fig.canvas.flush_events()

    # Start crecord capture
    crecord_cmd = [
        'crecord', '-v',
        '-c', str(card),
        '-d', str(device),
        '-I', 'BESPOKE',
        '-R', '16000',
        '-C', '2',
        '-F', 'S32_LE',
    ]
    cmd = ['stdbuf', '-o0'] + crecord_cmd

    print(f"Starting compress capture: {' '.join(crecord_cmd)}")
    print(f"Spectrogram width: {width} frames ({width * SOF_HOP_SEC:.1f}s)")
    print()

    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            bufsize=0)

    frame_q = queue.Queue()

    def reader_thread():
        buf = bytearray()
        raw_fd = proc.stdout.fileno()
        try:
            while True:
                data = os.read(raw_fd, SOF_FRAME_BYTES * 4)
                if not data:
                    break
                buf.extend(data)
                while True:
                    frame_number, vad_flag, frame_ints = parse_frame(buf)
                    if frame_ints is None:
                        break
                    frame_q.put((frame_number, vad_flag, frame_ints))
        except (OSError, ValueError):
            pass
        frame_q.put(None)

    reader = threading.Thread(target=reader_thread, daemon=True)
    reader.start()

    recv_frames = 0
    prev_speech = None

    try:
        while True:
            try:
                item = frame_q.get(timeout=0.05)
            except queue.Empty:
                fig.canvas.flush_events()
                continue

            if item is None:
                stderr_out = proc.stderr.read().decode(errors='replace')
                rc = proc.wait()
                print(f"\ncrecord exited with code {rc}")
                if stderr_out:
                    print(f"stderr: {stderr_out}")
                break

            frame_number, vad_flag, frame_ints = item
            recv_frames += 1
            mel = decode_mel_frame(frame_ints)
            speech = vad_flag != 0

            if speech != prev_speech:
                t = frame_number * SOF_HOP_SEC
                tag = "SPEECH" if speech else "SILENCE"
                print(f"  [{t:7.2f}s] {tag} (hop {frame_number})", flush=True)
            prev_speech = speech

            # Scroll left and append new frame
            mel_buf[:, :-1] = mel_buf[:, 1:]
            mel_buf[:, -1] = mel
            vad_buf[:-1] = vad_buf[1:]
            vad_buf[-1] = 1.0 if speech else 0.0

            # Batch update: refresh plot every few frames to reduce overhead
            if recv_frames % 3 == 0 or not speech:
                im_mel.set_data(mel_buf)
                line_vad.set_ydata(vad_buf)
                fig.canvas.draw_idle()
                fig.canvas.flush_events()

    except (KeyboardInterrupt, BrokenPipeError):
        pass
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
        print(f"\nCapture stopped. Received {recv_frames} frames.")
        try:
            plt.close(fig)
        except Exception:
            pass


def main():
    parser = argparse.ArgumentParser(
        description="Live scrolling mel spectrogram from SOF compress PCM capture")
    parser.add_argument('--card', '-c', type=int, default=0,
                        help='ALSA card number (default: 0)')
    parser.add_argument('--device', '-d', type=int, default=48,
                        help='ALSA compress device number (default: 48)')
    parser.add_argument('--width', '-w', type=int, default=SPECTROGRAM_WIDTH,
                        help=f'Spectrogram width in frames (default: {SPECTROGRAM_WIDTH})')
    args = parser.parse_args()

    print("=== SOF Mel Spectrogram Viewer (Compress PCM) ===\n")
    run_spectrogram(args.card, args.device, args.width)


if __name__ == '__main__':
    main()
