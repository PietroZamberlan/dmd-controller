"""
DMD Onboarding Script

Automated end-to-end test of the DMD hardware using the DMDController class.
Does everything the example_python_client.py does, but fully automated:
launches the C++ process, connects, cycles through test patterns, tests
BLACK/WHITE commands, and shuts down cleanly.

Usage:
    1. Build the C++ exe first:  .\\build_generate.ps1
    2. Run:  python tools\\onboarding.py

No manual steps required -- the script handles everything including
creating the initial frame file and launching the C++ process.
"""

import os
import sys
import struct
import time

import numpy as np

# Import DMDController from the same directory
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dmd_controller import DMDController


# Frame dimensions (must match what the DMD expects)
FRAME_WIDTH = 864
FRAME_HEIGHT = 864


def generate_test_frame(pattern_type):
    """Generate an 864x864 test frame with a named pattern."""
    frame = np.zeros((FRAME_HEIGHT, FRAME_WIDTH), dtype=np.uint8)

    if pattern_type == 'checkerboard':
        block_size = 32
        for y in range(FRAME_HEIGHT):
            for x in range(FRAME_WIDTH):
                if ((x // block_size) + (y // block_size)) % 2 == 0:
                    frame[y, x] = 255

    elif pattern_type == 'horizontal':
        stripe_height = 50
        for y in range(FRAME_HEIGHT):
            if (y // stripe_height) % 2 == 0:
                frame[y, :] = 255

    elif pattern_type == 'vertical':
        stripe_width = 50
        for x in range(FRAME_WIDTH):
            if (x // stripe_width) % 2 == 0:
                frame[:, x] = 255

    elif pattern_type == 'random':
        frame = np.random.randint(0, 256, (FRAME_HEIGHT, FRAME_WIDTH), dtype=np.uint8)

    elif pattern_type == 'circle':
        center_y, center_x = FRAME_HEIGHT // 2, FRAME_WIDTH // 2
        radius = min(FRAME_HEIGHT, FRAME_WIDTH) // 4
        for y in range(FRAME_HEIGHT):
            for x in range(FRAME_WIDTH):
                if (x - center_x)**2 + (y - center_y)**2 < radius**2:
                    frame[y, x] = 255

    return frame


def write_frame_to_file(frame, file_path):
    """Write frame to binary file with 8-byte header."""
    header = struct.pack('hhhh', FRAME_WIDTH, FRAME_HEIGHT, 1, 8)
    with open(file_path, 'wb') as f:
        f.write(header)
        f.write(frame.tobytes())


def main():
    print("=" * 60)
    print("DMD Onboarding -- Automated Hardware Test")
    print("=" * 60)

    # Resolve paths relative to repo root
    repo_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    bin_dir = os.path.join(repo_dir, "bin", "x64")
    frame_path = os.path.join(bin_dir, "current_frame.bin")
    log_path = os.path.join(bin_dir, "dmd_onboarding.log")

    exe_path = os.path.join(bin_dir, "dmd_control_closedloop_generate.exe")
    if not os.path.exists(exe_path):
        print(f"ERROR: C++ exe not found at {exe_path}")
        print("Build it first: .\\build_generate.ps1")
        return 1

    # Step 1: Create initial frame (C++ needs this at startup to read dimensions)
    print("\n1. Creating initial frame file...")
    initial_frame = generate_test_frame('checkerboard')
    write_frame_to_file(initial_frame, frame_path)
    print(f"   Written to {frame_path} ({os.path.getsize(frame_path)} bytes)")

    # Step 2: Launch C++ and connect
    print("\n2. Launching DMD controller...")
    controller = DMDController(log_path=log_path)

    if not controller.connect(timeout=15.0):
        print("ERROR: Failed to connect to DMD controller.")
        print(f"Check the log at {log_path}")
        return 1

    try:
        # Step 3: Cycle through test patterns
        patterns = ['horizontal', 'vertical', 'checkerboard', 'circle', 'random']
        print(f"\n3. Testing {len(patterns)} frame patterns...")

        for i, pattern in enumerate(patterns):
            print(f"\n   --- Pattern {i+1}/{len(patterns)}: {pattern} ---")

            frame = generate_test_frame(pattern)
            write_frame_to_file(frame, controller.bin_output_path)
            time.sleep(0.05)  # ensure file is flushed

            frame_num = controller.show_frame()
            if frame_num is not None:
                print(f"   OK - displayed at frame {frame_num}")
            else:
                print(f"   FAILED to display {pattern} pattern")
                return 1

            time.sleep(2)

        # Step 4: Test BLACK and WHITE
        print("\n4. Testing BLACK command...")
        frame_num = controller.show_black()
        if frame_num is not None:
            print(f"   OK - black at frame {frame_num}")
        else:
            print("   FAILED")
            return 1
        time.sleep(1)

        print("\n5. Testing WHITE command...")
        frame_num = controller.show_white()
        if frame_num is not None:
            print(f"   OK - white at frame {frame_num}")
        else:
            print("   FAILED")
            return 1
        time.sleep(1)

        # Step 5: Final frame
        print("\n6. Showing final frame (circle)...")
        frame = generate_test_frame('circle')
        write_frame_to_file(frame, controller.bin_output_path)
        time.sleep(0.05)
        frame_num = controller.show_frame()
        if frame_num is not None:
            print(f"   OK - displayed at frame {frame_num}")

        print("\n" + "=" * 60)
        print("ALL TESTS PASSED")
        print("=" * 60)
        print(f"\nTotal frames displayed: {controller.last_frame_number}")
        print(f"C++ log saved to: {log_path}")

    except KeyboardInterrupt:
        print("\n\nInterrupted by user.")

    finally:
        # Step 6: Clean shutdown
        print("\n7. Shutting down DMD controller...")
        controller.close()
        print("Done.")

    return 0


if __name__ == '__main__':
    sys.exit(main())
