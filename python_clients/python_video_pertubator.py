import win32pipe
import win32file
import numpy as np
import struct
import time
import cv2
import os
import random
import json
from datetime import datetime

# --- Configuration ---
PIPE_NAME = r"\\.\pipe\DMDControlPipe"
_TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
_REPO_DIR = os.path.dirname(_TOOLS_DIR)
FRAME_FILE_PATH = os.path.join(_REPO_DIR, "bin", "x64", "current_frame.bin")
LOG_DIR = os.path.join(_REPO_DIR, "logs")

if not os.path.exists(LOG_DIR):
    os.makedirs(LOG_DIR)

# DMD Native Dimensions
FRAME_WIDTH = 864
FRAME_HEIGHT = 864

# --- Experiment Parameters ---
nb_perts = 2000
n_frame_per_pert = 8
pert_val = 0.15
new_shape = (216, 216)
n_grey_frames = 16


def load_source_videos():
    video_source_folder = os.path.join(_REPO_DIR, "source_videos")
    gm_raw = np.load(os.path.join(video_source_folder, "global_motion.npy"))
    bar_raw = np.load(os.path.join(video_source_folder, "bar_video.npy"))
    pert_raw = np.load(os.path.join(video_source_folder, "pert_video.npy"))

    def downsample(vid):
        return np.array(
            [
                cv2.resize(
                    f.astype(np.float32), new_shape, interpolation=cv2.INTER_AREA
                )
                for f in vid
            ]
        )

    print("Pre-processing: Downsampling sources...")
    return [downsample(gm_raw), downsample(bar_raw)], downsample(pert_raw)


def prepare_frame_for_dmd(frame_216):
    frame_uint8 = (np.clip(frame_216, 0, 1) * 255).astype(np.uint8)
    canvas = np.full((FRAME_HEIGHT, FRAME_WIDTH), 127, dtype=np.uint8)
    y_off = (FRAME_HEIGHT - new_shape[1]) // 2
    x_off = (FRAME_WIDTH - new_shape[0]) // 2
    canvas[y_off : y_off + new_shape[1], x_off : x_off + new_shape[0]] = frame_uint8
    return canvas


def write_to_bin(canvas, file_path):
    header = struct.pack("hhhh", FRAME_WIDTH, FRAME_HEIGHT, 1, 8)
    with open(file_path, "wb") as f:
        f.write(header)
        f.write(canvas.tobytes())


def connect_to_dmd():
    try:
        return win32file.CreateFile(
            PIPE_NAME,
            win32file.GENERIC_READ | win32file.GENERIC_WRITE,
            0,
            None,
            win32file.OPEN_EXISTING,
            0,
            None,
        )
    except:
        return None


def send_command(handle, command):
    win32file.WriteFile(handle, command.encode())
    _, response = win32file.ReadFile(handle, 64)
    return response.decode().strip("\x00")


def main():
    # 1. LOAD SOURCES
    ds_video_list, ds_pert_video = load_source_videos()

    # 2. PRE-PROCESS METADATA
    print("Preparing experimental sequence...")
    trial_queue = []
    for v_type, base_video in enumerate(ds_video_list):
        v_len = base_video.shape[0]
        for rep in range(nb_perts):
            pert_start = rep * n_frame_per_pert
            pert_end = pert_start + v_len

            if pert_end <= len(ds_pert_video):
                trial_queue.append(
                    {
                        "v_type": v_type,
                        "rep_idx": rep,
                        "pert_slice_indices": [
                            pert_start,
                            pert_end,
                        ],  # JSON likes lists, not tuples
                        "meta_id": 100000 + (v_type * 10000) + rep,
                    }
                )

    random.shuffle(trial_queue)

    # --- SAVE TRIAL QUEUE LOG ---
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    log_filename = f"stimulus_metadata_{timestamp}.json"
    log_path = os.path.join(LOG_DIR, log_filename)

    with open(log_path, "w") as f:
        json.dump(trial_queue, f, indent=4)
    print(f"Trial metadata saved to: {log_path}")

    # 3. CONNECT AND RUN
    handle = connect_to_dmd()
    if not handle:
        print("DMD not found. Ensure C++ app is running.")
        return

    try:
        grey_frame_216 = np.ones(new_shape, dtype=np.float32) * 0.5
        grey_canvas = prepare_frame_for_dmd(grey_frame_216)

        for i, trial in enumerate(trial_queue):
            print(f"[{i+1}/{len(trial_queue)}] Running Trial {trial['meta_id']}")

            # PHASE A: Grey Screen (0.4s)
            write_to_bin(grey_canvas, FRAME_FILE_PATH)
            for _ in range(n_grey_frames):
                send_command(handle, "SHOW_FRAME")

            # PHASE B: On-the-fly Video Generation
            base_vid = ds_video_list[trial["v_type"]]
            p_start, p_end = trial["pert_slice_indices"]
            # We pre-calculate the trial pert once per trial to save CPU inside the frame loop
            pert_slice = (ds_pert_video[p_start:p_end] - 0.5) * (2 * pert_val)

            for t in range(len(base_vid)):
                current_frame_216 = base_vid[t] + pert_slice[t]
                dmd_canvas = prepare_frame_for_dmd(current_frame_216)
                write_to_bin(dmd_canvas, FRAME_FILE_PATH)
                send_command(handle, "SHOW_FRAME")

        print("Sequence complete.")
        send_command(handle, "QUIT")

    except KeyboardInterrupt:
        send_command(handle, "QUIT")
    finally:
        if handle:
            win32file.CloseHandle(handle)


if __name__ == "__main__":
    main()
