"""
Frame Header Inspector

Reads and displays header information from current_frame.bin.
Uses the same binary format as the C++ code.

Header Format:
- 4 signed shorts (2 bytes each) = 8 bytes total
- [frameWidth, frameHeight, frameCount, bitDepth]
"""

import struct
import os

import os
_TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
_REPO_DIR = os.path.dirname(_TOOLS_DIR)
FRAME_FILE = os.path.join(_REPO_DIR, 'bin', 'x64', 'current_frame.bin')

def read_frame_header(file_path):
    """
    Read the 8-byte header from frame file.

    Returns dict with header info, or None if error.
    """
    if not os.path.exists(file_path):
        print(f"ERROR: File not found: {file_path}")
        return None

    with open(file_path, 'rb') as f:
        # Read 8-byte header: 4 shorts (2 bytes each)
        header_bytes = f.read(8)

        if len(header_bytes) < 8:
            print(f"ERROR: Header too short (got {len(header_bytes)} bytes)")
            return None

        # Unpack as 4 signed shorts (native byte order, same as C++)
        # Format 'hhhh' = 4 shorts, matches C++ "short header[4]"
        header = struct.unpack('hhhh', header_bytes)

        frame_width = header[0]
        frame_height = header[1]
        frame_count = header[2]
        bit_depth = header[3]

        return {
            'width': frame_width,
            'height': frame_height,
            'count': frame_count,
            'bit_depth': bit_depth
        }

def main():
    print("=" * 60)
    print("Frame Header Inspector")
    print("=" * 60)
    print(f"\nReading: {FRAME_FILE}")

    info = read_frame_header(FRAME_FILE)

    if info:
        print(f"\n{'Header Information':^60}")
        print("-" * 60)
        print(f"  Frame Width:  {info['width']:>6} pixels")
        print(f"  Frame Height: {info['height']:>6} pixels")
        print(f"  Frame Count:  {info['count']:>6}")
        print(f"  Bit Depth:    {info['bit_depth']:>6}")
        print("-" * 60)

        # Calculate expected sizes
        expected_data_size = info['width'] * info['height']
        expected_file_size = 8 + expected_data_size  # 8-byte header + pixel data

        actual_file_size = os.path.getsize(FRAME_FILE)

        print(f"\n{'File Size Verification':^60}")
        print("-" * 60)
        print(f"  Header size:        8 bytes")
        print(f"  Expected data size: {expected_data_size:,} bytes ({info['width']}×{info['height']} pixels)")
        print(f"  Expected total:     {expected_file_size:,} bytes")
        print(f"  Actual file size:   {actual_file_size:,} bytes")
        print("-" * 60)

        if actual_file_size == expected_file_size:
            print("  ✓ File size matches expected dimensions")
        else:
            difference = actual_file_size - expected_file_size
            print(f"  ✗ WARNING: File size mismatch!")
            print(f"     Difference: {difference:+,} bytes")

        print()
        print(f"{'Comparison':^60}")
        print("-" * 60)
        print(f"  Current dimensions: {info['width']}×{info['height']}")
        print(f"  Expected (if 864×864): 864×864")
        print("-" * 60)

        if info['width'] == 864 and info['height'] == 864:
            print("  ✓ Frame is 864×864 as expected")
        else:
            print(f"  ✗ Frame is NOT 864×864")
            print(f"     To change to 864×864, modify create_initial_frame.py")
    else:
        print("\nFailed to read frame header")

    print("=" * 60)

if __name__ == '__main__':
    main()
