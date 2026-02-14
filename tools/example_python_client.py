"""
Example Python client for DMD Control with Online Frame Generation

This script demonstrates how to use the new SHOW_FRAME command with dynamically generated frames.

Usage:
1. Start the C++ DMD control program (dmd_control_closedloop_generate.exe)
2. Run this script
3. The script generates frames and sends SHOW_FRAME commands
"""

import win32pipe
import win32file
import numpy as np
import struct
import time

# Configuration
PIPE_NAME = r'\\.\pipe\DMDControlPipe'
import os
_TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
_REPO_DIR = os.path.dirname(_TOOLS_DIR)
FRAME_FILE_PATH = os.path.join(_REPO_DIR, 'bin', 'x64', 'current_frame.bin')

# Frame dimensions (must match DMD configuration)
FRAME_WIDTH = 864
FRAME_HEIGHT = 864


def generate_test_frame(pattern_type='checkerboard'):
    """
    Generate a test frame with different patterns.

    Args:
        pattern_type: 'checkerboard', 'horizontal', 'vertical', 'random', 'circle'

    Returns:
        numpy array of shape (FRAME_HEIGHT, FRAME_WIDTH) with uint8 values
    """
    frame = np.zeros((FRAME_HEIGHT, FRAME_WIDTH), dtype=np.uint8)

    if pattern_type == 'checkerboard':
        # Create checkerboard pattern
        block_size = 32
        for y in range(FRAME_HEIGHT):
            for x in range(FRAME_WIDTH):
                if ((x // block_size) + (y // block_size)) % 2 == 0:
                    frame[y, x] = 255

    elif pattern_type == 'horizontal':
        # Horizontal stripes
        stripe_height = 50
        for y in range(FRAME_HEIGHT):
            if (y // stripe_height) % 2 == 0:
                frame[y, :] = 255

    elif pattern_type == 'vertical':
        # Vertical stripes
        stripe_width = 50
        for x in range(FRAME_WIDTH):
            if (x // stripe_width) % 2 == 0:
                frame[:, x] = 255

    elif pattern_type == 'random':
        # Random noise
        frame = np.random.randint(0, 256, (FRAME_HEIGHT, FRAME_WIDTH), dtype=np.uint8)

    elif pattern_type == 'circle':
        # Circle in the center
        center_y, center_x = FRAME_HEIGHT // 2, FRAME_WIDTH // 2
        radius = min(FRAME_HEIGHT, FRAME_WIDTH) // 4
        for y in range(FRAME_HEIGHT):
            for x in range(FRAME_WIDTH):
                if (x - center_x)**2 + (y - center_y)**2 < radius**2:
                    frame[y, x] = 255

    return frame


def write_frame_to_file(frame, file_path):
    """
    Write frame to binary file with header.

    Binary format:
    - Header (8 bytes): [frameWidth (short), frameHeight (short), frameCount (short), bitDepth (short)]
    - Frame data: width * height bytes
    """
    # Create header
    header = struct.pack('hhhh', FRAME_WIDTH, FRAME_HEIGHT, 1, 8)  # 1 frame, 8-bit depth

    # Write to file
    with open(file_path, 'wb') as f:
        f.write(header)
        f.write(frame.tobytes())

    print(f"Frame written to {file_path}")


def connect_to_dmd():
    """Connect to the DMD control pipe."""
    print(f"Connecting to {PIPE_NAME}...")

    try:
        handle = win32file.CreateFile(
            PIPE_NAME,
            win32file.GENERIC_READ | win32file.GENERIC_WRITE,
            0,
            None,
            win32file.OPEN_EXISTING,
            0,
            None
        )
        print("Connected successfully!")
        return handle
    except Exception as e:
        print(f"Failed to connect: {e}")
        print("Make sure the C++ DMD control program is running.")
        return None


def send_command(handle, command):
    """Send command to DMD and read response."""
    try:
        # Send command
        win32file.WriteFile(handle, command.encode())
        print(f"Sent command: {command}")

        # Read response
        result, response = win32file.ReadFile(handle, 64)
        response_str = response.decode().strip('\x00')
        print(f"Received response: {response_str}")
        return response_str
    except Exception as e:
        print(f"Communication error: {e}")
        return None


def main():
    """Main demonstration routine."""
    print("=" * 60)
    print("DMD Control - Online Frame Generation Demo")
    print("=" * 60)

    # Generate initial frame for dimension discovery
    print("\n1. Generating initial frame for dimension discovery...")
    initial_frame = generate_test_frame('checkerboard')
    write_frame_to_file(initial_frame, FRAME_FILE_PATH)

    print("\nInitial frame created. You can now start the C++ program.")
    print("Press Enter when the C++ program is waiting for connection...")
    input()

    # Connect to DMD
    handle = connect_to_dmd()
    if handle is None:
        return

    try:
        # Test different patterns
        patterns = ['horizontal', 'vertical', 'checkerboard', 'circle', 'random']

        print("\n2. Testing different frame patterns...")
        for i, pattern in enumerate(patterns):
            print(f"\n--- Pattern {i+1}/{len(patterns)}: {pattern} ---")

            # Generate and write frame
            frame = generate_test_frame(pattern)
            write_frame_to_file(frame, FRAME_FILE_PATH)

            # Small delay to ensure file is written
            time.sleep(0.05)

            # Send SHOW_FRAME command
            response = send_command(handle, 'SHOW_FRAME')

            if response and response.startswith('SHOW_FRAME_STARTED'):
                print(f"Success! Frame displayed at frame count: {response.split(':')[1]}")
            else:
                print(f"Error: {response}")

            # Wait before next pattern
            print("Waiting 2 seconds before next pattern...")
            time.sleep(2)

        # Test BLACK and WHITE commands
        print("\n3. Testing BLACK command...")
        send_command(handle, 'BLACK')
        time.sleep(1)

        print("\n4. Testing WHITE command...")
        send_command(handle, 'WHITE')
        time.sleep(1)

        print("\n5. Showing final frame...")
        frame = generate_test_frame('circle')
        write_frame_to_file(frame, FRAME_FILE_PATH)
        time.sleep(0.05)
        send_command(handle, 'SHOW_FRAME')

        print("\nDemo completed successfully!")
        print("Press Enter to quit (sends QUIT command to C++)...")
        input()

        # Send quit command
        send_command(handle, 'QUIT')

    except KeyboardInterrupt:
        print("\n\nInterrupted by user. Sending QUIT command...")
        send_command(handle, 'QUIT')
    finally:
        win32file.CloseHandle(handle)
        print("Connection closed.")


if __name__ == '__main__':
    main()
