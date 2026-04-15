# Copied from ClosedLoopProject/standalone_generate/dmd_controller.py
# Source commit: 5cc4d5f (ClosedLoopProject repo, standalone_generate branch)
# Copy date: 2026-04-15
#
# This is a local copy so the dmd-controller repo can be used standalone
# without needing the full ClosedLoopProject repo. If the original changes,
# update this copy manually.

"""
DMD Controller -- interface to the C++ DMD projector process.

Launches dmd_control_closedloop_generate.exe, communicates via a Windows
named pipe (SHOW_FRAME, BLACK, WHITE, QUIT commands), and manages process
lifecycle.

See docs/DMD_PROTOCOL.md for the full protocol specification.
"""

import subprocess
import time
import os
import random

import win32file
import pywintypes
import msvcrt


class DMDController:
    def __init__(self, testmode=False, log_path=None, threadict=None):
        """
        Initialize DMD Controller (generate mode).

        Args:
            testmode: Skip actual DMD connection (for testing)
            log_path: Path for C++ process stdout/stderr log file.
            threadict: Shared thread state dict (test mode only -- used to read
                       test_frame_counter for synchronized frame numbers).
        """
        self.pipe = None
        self.connected = False
        self.last_frame_number = 0
        self.process = None
        self._dmd_log_file = None
        self._dmd_log_path = log_path
        self.testmode = testmode
        self._threadict = threadict
        self.bin_output_path = None  # Set during connect(), derived from exe path

        if self.testmode:
            print("\n" + "="*50)
            print("INFO: DMD Controller starting in TEST MODE.")
            print("="*50 + "\n")
            self.connected = True
        else:
            print("DMD Controller initialized.")

    def connect(self, timeout=10.0):
        """Launch and Connect to the DMD control program"""

        if self.testmode:
            print("DMD connection skipped (Test Mode).")
            return True

        dmd_control_path = r"C:\Users\S8\Repositories\dmd-controller\bin\x64\dmd_control_closedloop_generate.exe"

        # current_frame.bin must be in the same directory as the exe
        # (the C++ process reads it from its own exe directory)
        self.bin_output_path = os.path.join(os.path.dirname(dmd_control_path), "current_frame.bin")

        print(f"Launching {dmd_control_path}...")
        print(f"DMD C++ stdout/stderr -> {self._dmd_log_path}")
        print(f"Bin output path: {self.bin_output_path}")

        # Launch DMD process with stdout/stderr captured to log file.
        # The named pipe (DMDControlPipe) is independent of stdio.
        self._dmd_log_file = open(self._dmd_log_path, "w")
        self.process = subprocess.Popen(
            [dmd_control_path],
            stdout=self._dmd_log_file,
            stderr=subprocess.STDOUT,
            cwd=os.path.dirname(dmd_control_path),
            # Own process group: prevents Ctrl+C from killing the C++ process
            # before Python can send QUIT over the named pipe for clean shutdown.
            creationflags=subprocess.CREATE_NEW_PROCESS_GROUP,
        )

        # Give it time to initialize
        time.sleep(0.5)

        start_time = time.time()
        while time.time() - start_time < timeout:
            # Check if the C++ process died (crash / missing DLL / etc.)
            if self.process.poll() is not None:
                self._dmd_log_file.flush()
                print(f"ERROR: DMD process exited immediately with code {self.process.returncode}")
                print(f"--- Last lines of {self._dmd_log_path} ---")
                try:
                    with open(self._dmd_log_path, "r") as f:
                        lines = f.readlines()
                        for line in lines[-20:]:
                            print(f"  {line.rstrip()}")
                except Exception:
                    print("  (could not read log file)")
                print("---")
                return False

            try:
                self.pipe = win32file.CreateFile(
                    r'\\.\pipe\DMDControlPipe',
                    win32file.GENERIC_READ | win32file.GENERIC_WRITE,
                    0, None, win32file.OPEN_EXISTING, 0, None)
                self.connected = True
                print("Connected to DMD controller")
                return True
            except pywintypes.error as e:
                print(f"Waiting for DMD controller... ({e.strerror})")
                time.sleep(0.5)

        print("Failed to connect to DMD controller (timeout)")
        return False

    def _send_command_and_get_response(self, command):
        """Send command and get frame number response"""
        if not self.connected:
            print("Not connected to DMD")
            return None

        try:
            # Send command
            win32file.WriteFile(self.pipe, command.encode())

            # Wait for response
            for _ in range(20):  # Try for 2 seconds
                result, data = win32file.ReadFile(self.pipe, 1024)
                if result == 0 and data:
                    return data.decode().strip()
                time.sleep(0.05)

            return None
        except pywintypes.error as e:
            print(f"Error communicating with DMD: {e.strerror}")
            self.connected = False
            return None

    def show_black(self):
        """Show black frame sequence and return start frame"""
        response = self._send_command_and_get_response("BLACK")
        if response and response.startswith("BLACK_STARTED:"):
            frame_num = int(response.split(":")[1])
            self.last_frame_number = frame_num
            return frame_num
        return None

    def show_white(self):
        """Show white frame sequence and return start frame"""
        response = self._send_command_and_get_response("WHITE")
        if response and response.startswith("WHITE_STARTED:"):
            frame_num = int(response.split(":")[1])
            self.last_frame_number = frame_num
            return frame_num
        return None

    def show_frame(self):
        """
        Show the current frame from file.

        Sends SHOW_FRAME command to the C++ process, which reads
        current_frame.bin and displays it on the DMD.

        Returns:
            int: Starting frame number, or None if failed
        """

        if self.testmode:
            return self.show_frame_test()

        print("Sending command: SHOW_FRAME")
        response = self._send_command_and_get_response("SHOW_FRAME")

        if response and response.startswith("SHOW_FRAME_STARTED:"):
            frame_num = int(response.split(":")[1])
            self.last_frame_number = frame_num
            print(f"Frame started at {frame_num}")
            return frame_num

        return None

    def show_frame_test(self):
        """Simulates showing a frame and returns a synchronized start frame.

        In real hardware, the C++ DMD controller's frame counter runs
        continuously at 30Hz in sync with MEA trigger dips.  When SHOW_FRAME
        is sent, the response contains the *absolute* frame number at which
        the image appeared -- reflecting actual elapsed time.

        In test mode, we replicate this by reading the ORT test loop's
        trigger counter (test_frame_counter in threadict), which tracks how
        many trigger dips have been generated so far.  This ensures the
        returned start_frame matches Linux's global_frame_counter, so the
        computed spike window lands near the current sample position.
        """
        print("SIMULATING command: SHOW_FRAME")

        time.sleep(0.05)

        # Read the current trigger count from ORT test mode.
        new_frame_number = self._threadict['test_frame_counter']
        self.last_frame_number = new_frame_number

        print(f"SIMULATED response: SHOW_FRAME_STARTED:{new_frame_number}")
        return new_frame_number

    def close(self):
        """Close connection and clean up DMD process + log file."""
        if self.testmode:
            print("DMD Controller (Test Mode) closed.")
            return

        # 1. Send QUIT over the named pipe (keep pipe open so C++ can read it)
        if self.pipe:
            try:
                win32file.WriteFile(self.pipe, "QUIT".encode())
            except Exception as e:
                print(f"Error sending QUIT: {e}")

        # 2. Wait for process to exit (DMD hardware cleanup needs time).
        if self.process:
            try:
                self.process.wait(timeout=10.0)
                print(f"DMD process exited with code {self.process.returncode}")
            except subprocess.TimeoutExpired:
                print("DMD process did not exit after QUIT (10s), terminating...")
                self.process.terminate()
                try:
                    self.process.wait(timeout=3.0)
                except subprocess.TimeoutExpired:
                    self.process.kill()
            self.process = None

        # 3. Close the named pipe after process has exited
        if self.pipe:
            try:
                win32file.CloseHandle(self.pipe)
            except Exception:
                pass
            self.pipe = None
        self.connected = False

        # 4. Close the log file
        if self._dmd_log_file:
            self._dmd_log_file.close()
            self._dmd_log_file = None
            print(f"DMD log saved to: {self._dmd_log_path}")

    def check_keyboard_input(self):
        """Check for keyboard input and trigger DMD commands"""
        if msvcrt.kbhit():  # Check if a key was pressed
            key = msvcrt.getch().decode('utf-8').lower()

            if key == 'w':
                print("Key 'w' pressed - showing white frame...")
                white_frame = self.show_white()
                if white_frame:
                    print(f"White frame started at frame {white_frame}")
                return 'white'

            elif key == 'b':
                print("Key 'b' pressed - showing black frame...")
                black_frame = self.show_black()
                if black_frame:
                    print(f"Black frame started at frame {black_frame}")
                return 'black'

            elif key == 'q':
                print("Key 'q' pressed - quitting...")
                return 'quit'

        return None
