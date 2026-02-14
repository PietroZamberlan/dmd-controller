"""
Create Initial Frame File for DMD Control

This script creates the initial current_frame.bin file required by the C++ program at startup.
The C++ program reads this file to determine frame dimensions and centering offsets.

Run this script BEFORE starting the C++ DMD control program.
"""

import struct
import numpy as np
import os

# Configuration
_TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
_REPO_DIR = os.path.dirname(_TOOLS_DIR)
OUTPUT_PATH = os.path.join(_REPO_DIR, 'bin', 'x64', 'current_frame.bin')
FRAME_WIDTH = 864
FRAME_HEIGHT = 864


def create_test_pattern(pattern_type='gray'):
    """
    Create a simple test pattern.

    Args:
        pattern_type: 'gray', 'white', 'checkerboard'

    Returns:
        numpy array of shape (FRAME_HEIGHT, FRAME_WIDTH) with uint8 values
    """
    frame = np.zeros((FRAME_HEIGHT, FRAME_WIDTH), dtype=np.uint8)

    if pattern_type == 'gray':
        # Gray frame (use 128+ for visibility in binary mode)
        frame[:] = 128

    elif pattern_type == 'white':
        # White frame
        frame[:] = 255

    elif pattern_type == 'checkerboard':
        # Checkerboard pattern
        block_size = 32
        for y in range(FRAME_HEIGHT):
            for x in range(FRAME_WIDTH):
                if ((x // block_size) + (y // block_size)) % 2 == 0:
                    frame[y, x] = 255
                else:
                    frame[y, x] = 0

    return frame


def write_frame_file(frame, output_path):
    """
    Write frame to binary file with proper header.

    Binary format:
    - Header (8 bytes): [frameWidth, frameHeight, frameCount, bitDepth] (all shorts)
    - Frame data: width * height bytes
    """
    # Validate frame shape
    if frame.shape != (FRAME_HEIGHT, FRAME_WIDTH):
        raise ValueError(f"Frame must be {FRAME_HEIGHT}x{FRAME_WIDTH}, got {frame.shape}")

    # Validate frame dtype
    if frame.dtype != np.uint8:
        raise ValueError(f"Frame must be uint8, got {frame.dtype}")

    # Create header
    # Format: frameWidth, frameHeight, frameCount=1, bitDepth=8
    header = struct.pack('hhhh', FRAME_WIDTH, FRAME_HEIGHT, 1, 8)

    # Create output directory if it doesn't exist
    output_dir = os.path.dirname(output_path)
    if output_dir and not os.path.exists(output_dir):
        os.makedirs(output_dir)
        print(f"Created directory: {output_dir}")

    # Write to file
    with open(output_path, 'wb') as f:
        f.write(header)
        f.write(frame.tobytes())

    file_size = os.path.getsize(output_path)
    expected_size = 8 + FRAME_WIDTH * FRAME_HEIGHT

    print(f"Frame file created: {output_path}")
    print(f"File size: {file_size} bytes (expected: {expected_size} bytes)")

    if file_size != expected_size:
        print("WARNING: File size mismatch!")
        return False

    return True


def main():
    print("=" * 60)
    print("Create Initial Frame File for DMD Control")
    print("=" * 60)
    print()

    # Check if file already exists
    if os.path.exists(OUTPUT_PATH):
        print(f"Warning: File already exists at {OUTPUT_PATH}")
        response = input("Overwrite? (y/n): ").strip().lower()
        if response != 'y':
            print("Cancelled.")
            return

    print(f"Frame dimensions: {FRAME_WIDTH} x {FRAME_HEIGHT}")
    print(f"Output path: {OUTPUT_PATH}")
    print()

    # Let user choose pattern
    print("Select initial frame pattern:")
    print("1. Gray (uniform gray, safe default)")
    print("2. White (uniform white)")
    print("3. Checkerboard (test pattern)")
    print()

    choice = input("Enter choice (1-3) [default: 1]: ").strip()

    if choice == '2':
        pattern_type = 'white'
    elif choice == '3':
        pattern_type = 'checkerboard'
    else:
        pattern_type = 'gray'

    print(f"\nGenerating {pattern_type} frame...")

    # Create frame
    frame = create_test_pattern(pattern_type)

    # Write to file
    success = write_frame_file(frame, OUTPUT_PATH)

    if success:
        print("\n" + "=" * 60)
        print("SUCCESS!")
        print("=" * 60)
        print()
        print("Initial frame file created successfully.")
        print("You can now start the C++ DMD control program:")
        print()
        print(f"  cd {os.path.join(_REPO_DIR, 'bin', 'x64')}")
        print("  dmd_control_closedloop_generate.exe")
        print()
    else:
        print("\n" + "=" * 60)
        print("ERROR!")
        print("=" * 60)
        print()
        print("Failed to create initial frame file.")
        print("Please check the error messages above.")


if __name__ == '__main__':
    try:
        main()
    except Exception as e:
        print(f"\nError: {e}")
        import traceback
        traceback.print_exc()
