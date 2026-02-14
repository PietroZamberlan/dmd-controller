// dmd_control_closedloop.cpp - Test program for DMD controller

#include <iostream>
#include <chrono>
#include <conio.h>      // For _kbhit() and _getch()
#include <Windows.h>    // For Sleep() and pipe communication
#include <cstdlib>      // For calloc(), free()
#include <string>       // For std::string

// ALP SDK includes
#include "alp.h"

using namespace std;

// Get the directory of the current executable (for relative path resolution)
std::string getExeDirectory() {
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string path(exePath);
    size_t lastSlash = path.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        return path.substr(0, lastSlash + 1);
    }
    return "";
}

const int MAX_RETRIES = 6;         // Maximum number of retry attempts for queuing requested image
const int RETRY_DELAY_MS = 40;     // Milliseconds to wait between retries

// Setup Named Pipe for Python communication
bool setupNamedPipe(HANDLE& hPipe) {
    hPipe = CreateNamedPipe(
        TEXT("\\\\.\\pipe\\DMDControlPipe"),
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_NOWAIT, // ATTENTION: MIGHT CAUSE ERROR IN PYTHON CLIENT
        // PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, // Use PIPE_WAIT to block until client connects
        1, 1024, 1024, 0, NULL);

    if (hPipe == INVALID_HANDLE_VALUE) {
        cout << "Failed to create named pipe: " << GetLastError() << endl;
        return false;
    } else {
        cout << "Named pipe created successfully." << endl;
    }

    // Loop until connection or user skips
    cout << "Waiting for Python client to connect..." << endl;
    cout << "Press 'y' to skip connection." << endl;
    bool clientConnected = false;
    while (!clientConnected) {
        // Check for connection
        BOOL result = ConnectNamedPipe(hPipe, NULL);
        DWORD error = GetLastError();
        
        if (result || error == ERROR_PIPE_CONNECTED) {
            cout << "Python client connected." << endl;
            clientConnected = true;
        }
        
        // Check for keyboard input
        if (_kbhit()) {
            char key = _getch();
            if (key == 'y' || key == 'Y') {
                cout << "Connection skipped by user. Continuing without client." << endl;
                return true; // Return success even without connection
            }
        }
        
        Sleep(100); // Small delay to prevent CPU hogging
    }
    
    return true;


    // cout << "Named pipe created. Waiting for Python client to connect..." << endl;
    // // This call is blocking and will wait here until the client connects.
    // bool connected = ConnectNamedPipe(hPipe, NULL);
    // if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
    //     cout << "Client connection failed: " << GetLastError() << endl;
    //     CloseHandle(hPipe);
    //     hPipe = INVALID_HANDLE_VALUE;
    //     return false;
    // }
    
    // cout << "Python client connected." << endl;
    // return true;
}

// Setup DMD device
bool setupDMD(ALP_ID& nDevId, long& nDmdType, long& nSizeX, long& nSizeY) {
    // Allocate the ALP high-speed device
    // Opens communication with the DMD device
    // nDevId becomes the handle to the device
    long ret_val = AlpDevAlloc(ALP_DEFAULT, ALP_DEFAULT, &nDevId);  
    if (ret_val != ALP_OK) {
        cout << "Failed to initialize DMD with error: " << ret_val << endl;
        return false;
    }
    cout << "DMD initialized successfully" << endl;
    
    // Inquire DMD type and dimensions
    if (ALP_OK != AlpDevInquire(nDevId, ALP_DEV_DMDTYPE, &nDmdType)) {
        cout << "Failed to inquire DMD type" << endl;
        AlpDevFree(nDevId);
        return false;
    }

    // Set dimensions based on DMD type`
    switch (nDmdType) {
        case ALP_DMDTYPE_XGA_055A:
        case ALP_DMDTYPE_XGA_055X:
        case ALP_DMDTYPE_XGA_07A:
            nSizeX = 1024; nSizeY = 768;
            cout << "DMD type: XGA (1024x768)" << endl;
            break;
        case ALP_DMDTYPE_1080P_095A:
            nSizeX = 1920; nSizeY = 1080;
            cout << "DMD type: 1080P (1920x1080)" << endl;
            break;
        case ALP_DMDTYPE_WUXGA_096A:
            nSizeX = 1920; nSizeY = 1200;
            cout << "DMD type: WUXGA (1920x1200)" << endl;
            break;
        default:
            cout << "Unsupported DMD type" << endl;
            AlpDevFree(nDevId);
            return false;
    }
    
    return true;
}

// Generate frame data: gray, black, and white frames
UCHAR* setUniformMemoryBlock( UCHAR* block , long nFramesToSet, long nSizeX, long nSizeY, int color) {
   
    // block : memory block of host to allocate
   
    block = (UCHAR*)calloc(nFramesToSet * nSizeX * nSizeY, sizeof(UCHAR)); // Allocate memory for nFramesSeq1 frames
    if (block == NULL) {
        // cout << "Failed to load frames from binary file" << endl;
        cout << "Failed to allocate memory for block" << endl;
        return NULL;
    }
    memset(block, color, nFramesToSet * nSizeX * nSizeY); // Initialize with gray value (127)
   
    return block;
}
// Generate a memory block with a uniform-colored rectangle centered within it.

UCHAR* setUniformCenteredMemoryBlock(UCHAR* block, long nFramesToSet, long nSizeX, long nSizeY, int frameWidth, 
                                    int frameHeight, int beg_w, int beg_h, int color) {

    // Allocate memory for the entire block for all frames
    long long totalSize = (long long)nFramesToSet * nSizeX * nSizeY;
    block = (UCHAR*)calloc((size_t)totalSize, sizeof(UCHAR));
    if (block == NULL) {
        cout << "Failed to allocate memory for centered uniform block" << endl;
        return NULL;
    }

    // Draw the centered rectangle for each frame in the block
    for (long frameNum = 0; frameNum < nFramesToSet; ++frameNum) {
        // Get a pointer to the start of the current frame in the block
        UCHAR* currentFrameStart = block + (frameNum * nSizeX * nSizeY);

        // Set the pixels for each row of the centered rectangle
        for (int y = 0; y < frameHeight; ++y) {
            // Calculate the starting position of the current row
            UCHAR* rowStart = currentFrameStart + (beg_h + y) * nSizeX + beg_w;
            // Fill the row with the specified color
            memset(rowStart, color, frameWidth);
        }
    }

    return block;
}

// Check available system memory
void checkSystemMemory() {
    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof(statex);
    GlobalMemoryStatusEx(&statex);
    
    cout << "=== System Memory Status ===" << endl;
    cout << "Total Physical RAM: " << statex.ullTotalPhys / (1024 * 1024) << " MB" << endl;
    cout << "Available Physical RAM: " << statex.ullAvailPhys / (1024 * 1024) << " MB" << endl;
    cout << "Memory Load: " << statex.dwMemoryLoad << "%" << endl;
    cout << "=============================" << endl;
}

// Debug function to print memory block contents
void debugPrintMemoryBlock(const char* blockName, UCHAR* memoryBlock, long nFramesMemory, long nSizeX, long nSizeY, 
                          int maxFramesToPrint = 5) {
    if (memoryBlock == NULL) {
        cout << "Cannot debug " << blockName << ": Memory block is NULL" << endl;
        return;
    }

    long frameSize = nSizeX * nSizeY;
    int framesToPrint = min(nFramesMemory, maxFramesToPrint);
    
    cout << "\n===== DEBUG: " << blockName << " Memory Content =====" << endl;
    cout << "Total frames: " << nFramesMemory << ", showing details for " << framesToPrint << " frames" << endl;
    
    for (int frameIdx = 0; frameIdx < framesToPrint; frameIdx++) {
        UCHAR* frameStart = memoryBlock + (frameIdx * frameSize);
        
        // Calculate statistics for this frame
        int minValue = 255;
        int maxValue = 0;
        double avgValue = 0;
        
        // Sample 1000 pixels for statistics (checking every pixel would be too slow)
        const int sampleSize = 1000;
        int sampleStep = frameSize / sampleSize;
        if (sampleStep < 1) sampleStep = 1;
        
        for (int i = 0; i < frameSize; i += sampleStep) {
            int value = frameStart[i];
            minValue = min(minValue, value);
            maxValue = max(maxValue, value);
            avgValue += value;
        }
        
        avgValue /= (frameSize / sampleStep);
        
        // Print frame info
        cout << "\n-- Frame " << frameIdx << " --" << endl;
        cout << "First bytes: ";
        for (int i = 0; i < 10 && i < frameSize; i++) {
            cout << (int)frameStart[i] << " ";
        }
        cout << "..." << endl;
        
        cout << "Last bytes: ";
        for (int i = frameSize - 10; i < frameSize; i++) {
            if (i >= 0) cout << (int)frameStart[i] << " ";
        }
        cout << endl;
        
        cout << "Stats: Min=" << minValue << ", Max=" << maxValue 
             << ", Avg=" << avgValue << endl;
        
        // Special case for debugging the gray frame issue
        if (frameIdx == 0 && minValue < 50 && maxValue < 50) {
            cout << "WARNING: Frame 0 appears to be BLACK, not gray!" << endl;
            cout << "Binary threshold is 127.5 - values below will be BLACK" << endl;
        }
        else if (frameIdx == 0 && minValue > 200 && maxValue > 200) {
            cout << "WARNING: Frame 0 appears to be WHITE, not gray!" << endl;
        }
        else if (frameIdx == 0) {
            cout << "Gray frame byte values look normal" << endl;
        }
    }
    
    cout << "=============================================" << endl;
}

// Read frame dimensions from initial frame file at startup
bool loadFrameDimensions(const char* framePath, int& frameWidth, int& frameHeight,
                         int& beg_w, int& beg_h, int nSizeX, int nSizeY) {
    FILE* file = NULL;

    errno_t err = fopen_s(&file, framePath, "rb");
    if (file == NULL) {
        cout << "Failed to open initial frame file: " << err << endl;
        cout << "Make sure " << framePath << " exists before starting." << endl;
        return false;
    }

    // Read header
    short header[4]; // [width, height, frameCount, bitDepth]
    size_t headerRead = fread(header, sizeof(short), 4, file);
    fclose(file);

    if (headerRead != 4) {
        cout << "Failed to read frame header" << endl;
        return false;
    }

    frameWidth = header[0];
    frameHeight = header[1];

    // Calculate centering offsets
    beg_w = (nSizeX - frameWidth) / 2;
    beg_h = (nSizeY - frameHeight) / 2;

    cout << "Frame dimensions loaded: " << frameWidth << "x" << frameHeight << endl;
    cout << "Centering offsets - beg_w: " << beg_w << ", beg_h: " << beg_h << endl;

    return true;
}

// Load a single frame from file and copy to block2
bool loadSingleFrameFromFile(const char* framePath, UCHAR* block2, long nSizeX, long nSizeY,
                             long nFramesToSet, int frameWidth, int frameHeight,
                             int beg_w, int beg_h) {
    if (block2 == NULL) {
        cout << "ERROR: block2 is NULL" << endl;
        return false;
    }

    // Retry logic for robust file reading
    const int MAX_FILE_RETRIES = 6;
    const int FILE_RETRY_DELAY_MS = 40;

    for (int attempt = 0; attempt < MAX_FILE_RETRIES; attempt++) {
        FILE* file = NULL;
        errno_t err = fopen_s(&file, framePath, "rb");

        if (file == NULL) {
            if (attempt < MAX_FILE_RETRIES - 1) {
                Sleep(FILE_RETRY_DELAY_MS);
                continue;
            }
            cout << "ERROR: Failed to open frame file after " << MAX_FILE_RETRIES << " attempts: " << err << endl;
            return false;
        }

        // Get file size for verification
        fseek(file, 0, SEEK_END);
        long fileSize = ftell(file);
        fseek(file, 0, SEEK_SET);

        // Expected size: header (8 bytes) + frame data
        long expectedFrameSize = frameWidth * frameHeight;
        long expectedFileSize = sizeof(short) * 4 + expectedFrameSize;

        if (fileSize != expectedFileSize) {
            fclose(file);
            if (attempt < MAX_FILE_RETRIES - 1) {
                cout << "File size mismatch (attempt " << (attempt + 1) << "), retrying..." << endl;
                Sleep(FILE_RETRY_DELAY_MS);
                continue;
            }
            cout << "ERROR: File size mismatch - expected " << expectedFileSize
                 << ", got " << fileSize << endl;
            return false;
        }

        // Read and validate header
        short header[4];
        size_t headerRead = fread(header, sizeof(short), 4, file);

        if (headerRead != 4) {
            fclose(file);
            if (attempt < MAX_FILE_RETRIES - 1) {
                Sleep(FILE_RETRY_DELAY_MS);
                continue;
            }
            cout << "ERROR: Failed to read header" << endl;
            return false;
        }

        // Validate dimensions match expected
        if (header[0] != frameWidth || header[1] != frameHeight) {
            fclose(file);
            cout << "ERROR: Frame dimensions changed! Expected " << frameWidth << "x" << frameHeight
                 << ", got " << header[0] << "x" << header[1] << endl;
            return false;
        }

        // Allocate temporary buffer for frame data
        UCHAR* tempBuffer = (UCHAR*)malloc(expectedFrameSize);
        if (tempBuffer == NULL) {
            fclose(file);
            cout << "ERROR: Failed to allocate temporary buffer" << endl;
            return false;
        }

        // Read frame data
        size_t bytesRead = fread(tempBuffer, 1, expectedFrameSize, file);
        fclose(file);

        if (bytesRead != (size_t)expectedFrameSize) {
            free(tempBuffer);
            if (attempt < MAX_FILE_RETRIES - 1) {
                cout << "Incomplete read (attempt " << (attempt + 1) << "), retrying..." << endl;
                Sleep(FILE_RETRY_DELAY_MS);
                continue;
            }
            cout << "ERROR: Failed to read complete frame - expected " << expectedFrameSize
                 << " bytes, got " << bytesRead << endl;
            return false;
        }

        // Success! Now copy to block2 with centering
        // Clear block2 first
        memset(block2, 0, nSizeX * nSizeY * nFramesToSet);

        // Copy frame data centered, replicated nFramesToSet times
        for (long frameOffset = 0; frameOffset < nFramesToSet; frameOffset++) {
            UCHAR* destFrame = block2 + (frameOffset * nSizeX * nSizeY);

            for (int y = 0; y < frameHeight; y++) {
                long destOffset = (beg_h + y) * nSizeX + beg_w;
                long sourceOffset = y * frameWidth;

                memcpy(destFrame + destOffset, tempBuffer + sourceOffset, frameWidth);
            }
        }

        free(tempBuffer);
        return true;
    }

    return false;
}


// Setup DMD sequences
bool setupSequences(ALP_ID nDevId, ALP_ID& nSeqId1, ALP_ID& nSeqId2, long nBit, 
                    long frameTime, long nFramesSeq1, long nFramesSeq2, long nOffset) {

    // Allocate sequences memory on DMD and sequence parameters
    
    // AlpSeqAlloc Creates storage space on the DMD for the frames
    // nBit -> expect nBit-bit data, needed to know how much space to allocate on DMD
    // nFrames ( number of frames to store in this sequence )
    // nSeqId1 and nSeqId2 are the handles for the sequences
    // NB: sequences are actual memory regions in the DMD,
    //     they both need to be filled with frames, independently.

    cout << "Allocating seq 1 with bitdepth " << nBit << " and " << nFramesSeq1 << " frames" << endl;
    if (ALP_OK != AlpSeqAlloc(nDevId, nBit, nFramesSeq1, &nSeqId1)) {
        cout << "Failed to allocate sequence 1" << endl;
        return false;
    }
    cout << "Sequence 1 allocated on DMD" << endl;

    cout << "Allocating seq 2 with bitdepth " << nBit << " and " << nFramesSeq2 << " frames" << endl;
    // Allocate sequence 2 for just one frame (dynamically loaded)
    if (ALP_OK != AlpSeqAlloc(nDevId, nBit, nFramesSeq2, &nSeqId2)) {
        cout << "Failed to allocate sequence 2" << endl;
        AlpSeqFree(nDevId, nSeqId1);
        return false;
    }
    cout << "Sequence 2 allocated on DMD" << endl;

    // Configure sequence for binary mode without dark phase
    // ALP_BIN_MODE setting does not tell the API "my image is 1-bit." ( so not only black or white images)
    // It tells the DMD hardware which PWM method to use to process the incoming pixel data. 
    // (Pulse Width Modulation) 
    // ALP_BITNUM=1 IS CORRECT means When you read from the allocated nBit=8 block1, 
    // interpret it as 1-bit data for display. <127 is black, >=127 is white

    // Configure binary mode for sequences
    if (ALP_OK != AlpSeqControl(nDevId, nSeqId1, ALP_BITNUM, 1) ||
        ALP_OK != AlpSeqControl(nDevId, nSeqId1, ALP_BIN_MODE, ALP_BIN_UNINTERRUPTED) ||
        ALP_OK != AlpSeqControl(nDevId, nSeqId2, ALP_BITNUM, 1) ||
        ALP_OK != AlpSeqControl(nDevId, nSeqId2, ALP_BIN_MODE, ALP_BIN_UNINTERRUPTED)) {
        cout << "Failed to set binary mode for sequences" << endl;
        return false;
    }
    cout << "Binary mode set for both sequences" << endl;
    
    // Allocating sequences a second time its NEEDED for the dmd to show a grayscale
    //  IMPORTANT
    if (ALP_OK != AlpSeqAlloc(nDevId, nBit, nFramesSeq1, &nSeqId1)) {
        cout << "Failed to RE-allocate sequence 1" << endl;
        return false;
    }
    cout << "Sequence 1 RE-allocated on DMD" << endl;

    // Allocate sequence 2 for just one frame (dynamically loaded)
    if (ALP_OK != AlpSeqAlloc(nDevId, nBit, nFramesSeq2, &nSeqId2)) {
        cout << "Failed to RE-allocate sequence 2" << endl;
        AlpSeqFree(nDevId, nSeqId1);
        return false;
    }
    cout << "Sequence 2 RE-allocated on DMD" << endl; 

    // Set timing parameters - AlpSeqTiming sets how long each FRAME is displayed
    if (ALP_OK != AlpSeqTiming(nDevId, nSeqId1, ALP_DEFAULT, frameTime, ALP_DEFAULT, ALP_DEFAULT, ALP_DEFAULT) ||
        ALP_OK != AlpSeqTiming(nDevId, nSeqId2, ALP_DEFAULT, frameTime, ALP_DEFAULT, ALP_DEFAULT, ALP_DEFAULT)) {
        cout << "Failed to set timing for sequences" << endl;
        return false;
    }
    cout << "Timing configured for sequences: " << frameTime << " microsec per frame" << endl;
    
    // Set queue mode
    if (ALP_OK != AlpProjControl(nDevId, ALP_PROJ_QUEUE_MODE, ALP_PROJ_SEQUENCE_QUEUE)) {
        cout << "Failed to set queue mode" << endl;
        return false;
    }
    cout << "Queue mode set" << endl;
    
    // We could also set sequence 1 to repeat continuously - NOT USED
        // if (ALP_OK != AlpSeqControl(nDevId, nSeqId1, ALP_SEQ_REPEAT, 10000)) { // putting ALP_DEFAULT here would mean ONCE only
        //     cout << "Failed to set repeat mode for sequence 1" << endl;
        //     goto cleanup;
        // }
        // cout << "Repeat mode set for sequence 1" << endl;

    return true;
}    

// Update function prototype
bool uploadBlocksToDMD(ALP_ID nDevId, ALP_ID nSeqId1, ALP_ID nSeqId2, UCHAR* block1, UCHAR* block2) {
    // Upload block1 to sequence 1 and associate it with seq1
    if (ALP_OK != AlpSeqPut(nDevId, nSeqId1, ALP_DEFAULT, ALP_DEFAULT, block1)) {
        cout << "Failed to send frames to sequence 1" << endl;
        return false;
    }
    cout << "Frames transferred to DMD memory for sequence 1" << endl;
    
    // Upload block2 to sequence 2
    if (ALP_OK != AlpSeqPut(nDevId, nSeqId2, ALP_DEFAULT, ALP_DEFAULT, block2)) {
        cout << "Failed to send frame to sequence 2" << endl;
        return false;
    }
    cout << "Frame transferred to DMD memory for sequence 2" << endl;
    
    return true;
}


// Process keyboard input
void processKeyboardInput(bool& running, ALP_ID nDevId, ALP_ID nSeqId2, long nOffset,
                        long& totalFramesDisplayed, long nFramesSeq2,
                         UCHAR* block2, long nSizeX, long nSizeY,
                         bool& justPlayedSeq2, const char* framePath,
                         int frameWidth, int frameHeight, int beg_w, int beg_h) {
    if (!_kbhit()) {
        return;
    }

    char key = _getch();
    if (key == 'q' || key == 'Q') {
        running = false;
        return;
    }

    if (key == 'b' || key == 'B') {
        cout << "\nTrigger 'b': Loading black frame..." << endl;

        // Create a black frame in block2 (fill ALL frames, not just first one)
        memset(block2, 0, nSizeX * nSizeY * nFramesSeq2);
        
        // Update sequence 2 with the black frame - with retry logic
        int retryCount = 0;
        bool success = false;
        while (!success && retryCount < MAX_RETRIES) {
            if (ALP_OK == AlpSeqPut(nDevId, nSeqId2, ALP_DEFAULT, ALP_DEFAULT, block2)) {
                success = true;
            } else {
                retryCount++;
                cout << "Failed to update seq2 memory with black frame (attempt " << retryCount << "/" << MAX_RETRIES << ")" << endl;
                Sleep(RETRY_DELAY_MS);
            }
        }

        if (!success) {
            cout << "Failed to update sequence 2 with black frame after " << MAX_RETRIES << " attempts" << endl;
            return;
        }
        
        // Queue sequence 2 for display - with retry logic
        retryCount = 0;
        success = false;
        while (!success && retryCount < MAX_RETRIES) {
            if (ALP_OK == AlpProjStart(nDevId, nSeqId2)) {
                success = true;
            } else {
                retryCount++;
                cout << "Failed to start sequence 2 (attempt " << retryCount << "/" << MAX_RETRIES << ")" << endl;
                Sleep(RETRY_DELAY_MS);
            }
        }

        if (!success) {
            cout << "Failed to start sequence 2 for black frame" << endl;
            return;
        }
        
        cout << "Black frame queued for display after frame " << totalFramesDisplayed << endl;
        totalFramesDisplayed += nFramesSeq2;
        justPlayedSeq2 = true;
    } 
    else if (key == 'w' || key == 'W') {
        cout << "\nTrigger 'w': Loading white frame..." << endl;

        // Create a white frame in block2 (fill ALL frames, not just first one)
        memset(block2, 255, nSizeX * nSizeY * nFramesSeq2);
        
        // Update sequence 2 with the white frame - with retry logic
        int retryCount = 0;
        bool success = false;
        while (!success && retryCount < MAX_RETRIES) {
            if (ALP_OK == AlpSeqPut(nDevId, nSeqId2, ALP_DEFAULT, ALP_DEFAULT, block2)) {
                success = true;
            } else {
                retryCount++;
                cout << "Failed to update seq2 memory with white frame (attempt " << retryCount << "/" << MAX_RETRIES << ")" << endl;
                Sleep(RETRY_DELAY_MS);
            }
        }

        if (!success) {
            cout << "Failed to update sequence 2 with white frame after " << MAX_RETRIES << " attempts" << endl;
            return;
        }
        
        // Queue sequence 2 for display - with retry logic
        retryCount = 0;
        success = false;
        while (!success && retryCount < MAX_RETRIES) {
            if (ALP_OK == AlpProjStart(nDevId, nSeqId2)) {
                success = true;
            } else {
                retryCount++;
                cout << "Failed to start sequence 2 (attempt " << retryCount << "/" << MAX_RETRIES << ")" << endl;
                Sleep(RETRY_DELAY_MS);
            }
        }

        if (!success) {
            cout << "Failed to start sequence 2 for white frame" << endl;
            return;
        }
        
        cout << "White frame queued for display after frame " << totalFramesDisplayed << endl;
        totalFramesDisplayed += nFramesSeq2;
        justPlayedSeq2 = true;
    }
    else if (key == 'r' || key == 'R') {
        cout << "\nTrigger 'R': Loading frame from file..." << endl;

        // Load frame from current_frame.bin directly into block2
        if (!loadSingleFrameFromFile(framePath, block2, nSizeX, nSizeY, nFramesSeq2,
                                     frameWidth, frameHeight, beg_w, beg_h)) {
            cout << "Failed to load frame from file" << endl;
            return;
        }

        cout << "Frame loaded into block2" << endl;

        // Update sequence 2 with the new frame - with retry logic
        int retryCount = 0;
        bool success = false;
        while (!success && retryCount < MAX_RETRIES) {
            if (ALP_OK == AlpSeqPut(nDevId, nSeqId2, ALP_DEFAULT, ALP_DEFAULT, block2)) {
                success = true;
            } else {
                retryCount++;
                cout << "Failed to update seq2 memory (attempt " << retryCount << "/" << MAX_RETRIES << ")" << endl;
                Sleep(RETRY_DELAY_MS);
            }
        }

        if (!success) {
            cout << "Failed to update sequence 2 after " << MAX_RETRIES << " attempts" << endl;
            return;
        }

        // Queue sequence 2 for display - with retry logic
        retryCount = 0;
        success = false;
        while (!success && retryCount < MAX_RETRIES) {
            if (ALP_OK == AlpProjStart(nDevId, nSeqId2)) {
                success = true;
            } else {
                retryCount++;
                cout << "Failed to start sequence 2 (attempt " << retryCount << "/" << MAX_RETRIES << ")" << endl;
                Sleep(RETRY_DELAY_MS);
            }
        }

        if (!success) {
            cout << "Failed to start sequence 2" << endl;
            return;
        }

        cout << "Frame queued for display after frame " << totalFramesDisplayed << endl;
        totalFramesDisplayed += nFramesSeq2;
        justPlayedSeq2 = true;
    }
}

// Process commands from Python pipe
void processPipeCommands(HANDLE hPipe, bool& running, ALP_ID nDevId, ALP_ID nSeqId1, ALP_ID nSeqId2, long nOffset,
                         long& totalFramesDisplayed, long nFramesSeq1, long nFramesSeq2,
                         char* buffer, char* response, UCHAR* block2, const char* framePath,
                         long nSizeX, long nSizeY, int frameWidth, int frameHeight,
                         bool& justPlayedSeq2, int beg_w, int beg_h) {
    if (hPipe == INVALID_HANDLE_VALUE) {
        return;
    }

    DWORD bytesRead = 0;
    DWORD bytesAvailable = 0;

    // Check if there's data to read
    if (PeekNamedPipe(hPipe, NULL, 0, NULL, &bytesAvailable, NULL) && bytesAvailable > 0) {
        if (ReadFile(hPipe, buffer, 127, &bytesRead, NULL) && bytesRead > 0) {
            buffer[bytesRead] = '\0';

            // Handle SHOW_FRAME command - loads frame from file
            if (strcmp(buffer, "SHOW_FRAME") == 0) {
                cout << "\nPython requested SHOW_FRAME..." << endl;

                // Load frame from file directly into block2
                if (!loadSingleFrameFromFile(framePath, block2, nSizeX, nSizeY, nFramesSeq2,
                                             frameWidth, frameHeight, beg_w, beg_h)) {
                    cout << "Failed to load frame from file" << endl;

                    // Send error response
                    sprintf_s(response, 64, "ERROR:FRAME_LOAD_FAILED");
                    DWORD bytesWritten = 0;
                    WriteFile(hPipe, response, strlen(response), &bytesWritten, NULL);
                    return;
                }

                // Update sequence 2 with the new frame - with retry logic
                int retryCount = 0;
                bool success = false;
                while (!success && retryCount < MAX_RETRIES) {
                    if (ALP_OK == AlpSeqPut(nDevId, nSeqId2, ALP_DEFAULT, ALP_DEFAULT, block2)) {
                        success = true;
                    } else {
                        retryCount++;
                        cout << "Failed to update seq2 memory (attempt " << retryCount << "/" << MAX_RETRIES << ")" << endl;
                        Sleep(RETRY_DELAY_MS);
                    }
                }

                if (!success) {
                    cout << "Failed to update seq2 memory after " << MAX_RETRIES << " attempts" << endl;

                    // Send error response
                    sprintf_s(response, 64, "ERROR:FRAME_UPLOAD_FAILED");
                    DWORD bytesWritten = 0;
                    WriteFile(hPipe, response, strlen(response), &bytesWritten, NULL);
                    return;
                }

                // Queue sequence 2 for display - with retry logic
                retryCount = 0;
                success = false;
                while (!success && retryCount < MAX_RETRIES) {
                    if (ALP_OK == AlpProjStart(nDevId, nSeqId2)) {
                        success = true;
                    } else {
                        retryCount++;
                        cout << "Failed to start sequence 2 (attempt " << retryCount << "/" << MAX_RETRIES << ")" << endl;
                        Sleep(RETRY_DELAY_MS);
                    }
                }

                if (!success) {
                    cout << "Failed to start sequence 2 after " << MAX_RETRIES << " attempts" << endl;

                    // Send error response
                    sprintf_s(response, 64, "ERROR:SEQUENCE_START_FAILED");
                    DWORD bytesWritten = 0;
                    WriteFile(hPipe, response, strlen(response), &bytesWritten, NULL);
                    return;
                }

                // Send confirmation with frame number
                sprintf_s(response, 64, "SHOW_FRAME_STARTED:%ld", totalFramesDisplayed);
                DWORD bytesWritten = 0;
                WriteFile(hPipe, response, strlen(response), &bytesWritten, NULL);

                cout << "Frame queued for display after frame " << totalFramesDisplayed << endl;
                totalFramesDisplayed += nFramesSeq2;
                justPlayedSeq2 = true;
            }
            // Handle existing commands
            else if (strcmp(buffer, "BLACK") == 0) {
                cout << "\nPython requested BLACK frame..." << endl;

                // Create a black frame in block2 (fill ALL frames, not just first one)
                memset(block2, 0, nSizeX * nSizeY * nFramesSeq2);
                
                // Update sequence 2 with the black frame
                if (ALP_OK != AlpSeqPut(nDevId, nSeqId2, ALP_DEFAULT, ALP_DEFAULT, block2)) {
                    cout << "Failed to update sequence 2 with black frame" << endl;
                    return;
                }
                
                // Queue sequence 2 for display
                if (ALP_OK != AlpProjStart(nDevId, nSeqId2)) {
                    cout << "Failed to start sequence 2 for black frame" << endl;
                    return;
                }
                
                // Send confirmation with frame number
                sprintf_s(response, 64, "BLACK_STARTED:%ld", totalFramesDisplayed);
                DWORD bytesWritten = 0;
                WriteFile(hPipe, response, strlen(response), &bytesWritten, NULL);
                
                totalFramesDisplayed += nFramesSeq2;
                justPlayedSeq2 = true;
            } 
            else if (strcmp(buffer, "WHITE") == 0) {
                cout << "\nPython requested WHITE frame..." << endl;

                // Create a white frame in block2 (fill ALL frames, not just first one)
                memset(block2, 255, nSizeX * nSizeY * nFramesSeq2);
                
                // Update sequence 2 with the white frame
                if (ALP_OK != AlpSeqPut(nDevId, nSeqId2, ALP_DEFAULT, ALP_DEFAULT, block2)) {
                    cout << "Failed to update sequence 2 with white frame" << endl;
                    return;
                }
                
                // Queue sequence 2 for display
                if (ALP_OK != AlpProjStart(nDevId, nSeqId2)) {
                    cout << "Failed to start sequence 2 for white frame" << endl;
                    return;
                }
                
                // Send confirmation with frame number
                sprintf_s(response, 64, "WHITE_STARTED:%ld", totalFramesDisplayed);
                DWORD bytesWritten = 0;
                WriteFile(hPipe, response, strlen(response), &bytesWritten, NULL);
                
                totalFramesDisplayed += nFramesSeq2;
                justPlayedSeq2 = true;
            } 
            else if (strcmp(buffer, "QUIT") == 0) {
                cout << "\nPython requested to QUIT..." << endl;
                running = false;
            } else {
                cout << "\nUnknown command from Python: " << buffer << endl;
                
                // Send error response
                sprintf_s(response, 64, "ERROR:UNKNOWN_COMMAND");
                DWORD bytesWritten = 0;
                WriteFile(hPipe, response, strlen(response), &bytesWritten, NULL);
            }
        }
    }
    
    // Handle disconnection
    if (GetLastError() == ERROR_BROKEN_PIPE || GetLastError() == ERROR_NO_DATA) {
        DisconnectNamedPipe(hPipe);
        ConnectNamedPipe(hPipe, NULL);
    }
}

// Update terminal output with dots showing progress
void updateTerminalOutput(long currentSequenceId, long& lastSequenceId, std::chrono::steady_clock::time_point& lastDotTime) {
    // Check if sequence changed
    if (currentSequenceId != lastSequenceId) {
        if (lastSequenceId != 0) {
            cout << endl;
        }
        
        cout << "Playing sequence " << currentSequenceId;
        lastSequenceId = currentSequenceId;
        lastDotTime = std::chrono::steady_clock::now();
    }
    
    // Display dots to show activity
    auto currentTime = std::chrono::steady_clock::now();
    if (currentSequenceId > 0 && (currentTime - lastDotTime >= std::chrono::milliseconds(500))) {
        cout << ".";
        cout.flush();
        lastDotTime = currentTime;
    }
}

// Ensure continuous display by queueing sequence 1 if needed
void ensureContinuousDisplay(ALP_ID nDevId, ALP_ID nSeqId1, tAlpProjProgress& QueueInfo,
                             long& totalFramesDisplayed, long nFramesSeq1, bool& running) {
    if (QueueInfo.nWaitingSequences <= 2) {
        if (ALP_OK != AlpProjStart(nDevId, nSeqId1)) {
            cout << "CRITICAL ERROR: Failed to queue gray in ensureContinuousDisplay. DMD queue may empty. Stopping program." << endl;
            running = false;
            return;
        }
        // cout << "Queuing seq 1 for continuos gray after frame " << totalFramesDisplayed << endl;
        totalFramesDisplayed += nFramesSeq1;
    }
}

// Clean up all resources
void cleanupResources(HANDLE hPipe, UCHAR* block1, UCHAR* block2, ALP_ID nDevId, ALP_ID nSeqId1, ALP_ID nSeqId2) {
    cout << "\n=== Starting DMD Cleanup ===" << endl;

    // CRITICAL: Stop all projection activity FIRST
    // This is the most important step for clean state between runs
    if (nDevId != ALP_INVALID_ID) {
        cout << "Halting all projection..." << endl;
        if (ALP_OK != AlpProjHalt(nDevId)) {
            cout << "WARNING: AlpProjHalt failed" << endl;
        } else {
            cout << "Projection halted successfully" << endl;
        }

        // Clear the sequence queue to remove all waiting sequences
        cout << "Clearing sequence queue..." << endl;
        if (ALP_OK != AlpProjControl(nDevId, ALP_PROJ_RESET_QUEUE, ALP_DEFAULT)) {
            cout << "WARNING: Failed to reset sequence queue" << endl;
        } else {
            cout << "Sequence queue cleared" << endl;
        }

        // CRITICAL: AlpProjHalt() is ASYNCHRONOUS - hardware needs time to actually stop
        //
        // WHY THIS WAIT LOOP IS NECESSARY:
        // --------------------------------
        // AlpProjHalt() returns immediately (ALP_OK), but the DMD hardware takes time to:
        // 1. Complete the current frame being displayed (~33ms at 30Hz)
        // 2. Stop the mirror deflection mechanism
        // 3. Release internal references to sequences currently in use
        //
        // WHAT HAPPENS WITHOUT THE WAIT:
        // - Sequence 1 (gray background) is actively displaying when halt is called
        // - Hardware still holds a reference to sequence 1
        // - AlpSeqFree(nSeqId1) fails with error because sequence is "in use"
        // - DMD memory is not properly released
        // - Next program run starts with residual state → MEMORY LEAKAGE
        //
        // SOLUTION:
        // Poll ALP_PROJ_STATE until it becomes ALP_PROJ_IDLE, indicating hardware
        // has fully stopped and released all sequence references. Typically takes 10-50ms.
        //
        cout << "Waiting for projection to fully stop..." << endl;
        long projState = -1;
        const int MAX_WAIT_MS = 5000;  // 2 second timeout
        const int POLL_INTERVAL_MS = 10;  // Check every 10ms
        int elapsed_ms = 0;

        while (elapsed_ms < MAX_WAIT_MS) {
            if (ALP_OK == AlpProjInquire(nDevId, ALP_PROJ_STATE, &projState)) {
                if (projState == ALP_PROJ_IDLE) {
                    cout << "Projection state: IDLE (confirmed stopped after " << elapsed_ms << "ms)" << endl;
                    break;
                }
            }

            Sleep(POLL_INTERVAL_MS);
            elapsed_ms += POLL_INTERVAL_MS;

            // Log progress every 500ms to show we're still waiting
            if (elapsed_ms % 500 == 0) {
                cout << "  Still waiting for IDLE state... (" << elapsed_ms << "ms)" << endl;
            }
        }

        // Check if we timed out
        if (elapsed_ms >= MAX_WAIT_MS) {
            cout << "CRITICAL WARNING: Projection did not reach IDLE state after " << MAX_WAIT_MS << "ms!" << endl;
            cout << "  Current state: " << projState << " (expected " << ALP_PROJ_IDLE << ")" << endl;
            cout << "  Proceeding with cleanup anyway - sequences may fail to free" << endl;
        }

        // Free sequence memory on DMD (now safe since projection stopped)
        if (nSeqId1 != ALP_INVALID_ID) {
            cout << "Freeing sequence 1..." << endl;
            if (ALP_OK != AlpSeqFree(nDevId, nSeqId1)) {
                cout << "WARNING: Failed to free sequence 1" << endl;
            } else {
                cout << "Sequence 1 freed" << endl;
            }
        }

        if (nSeqId2 != ALP_INVALID_ID) {
            cout << "Freeing sequence 2..." << endl;
            if (ALP_OK != AlpSeqFree(nDevId, nSeqId2)) {
                cout << "WARNING: Failed to free sequence 2" << endl;
            } else {
                cout << "Sequence 2 freed" << endl;
            }
        }

        // Halt device (redundant but ensures clean state)
        cout << "Halting device..." << endl;
        if (ALP_OK != AlpDevHalt(nDevId)) {
            cout << "WARNING: AlpDevHalt failed" << endl;
        } else {
            cout << "Device halted" << endl;
        }

        // Free device and close communication
        cout << "Freeing device..." << endl;
        if (ALP_OK != AlpDevFree(nDevId)) {
            cout << "WARNING: AlpDevFree failed" << endl;
        } else {
            cout << "Device freed successfully" << endl;
        }
    }

    // Free host memory
    if (block1 != NULL) {
        free(block1);
        cout << "Host memory block1 freed" << endl;
    }

    if (block2 != NULL) {
        free(block2);
        cout << "Host memory block2 freed" << endl;
    }

    // Close named pipe
    if (hPipe != INVALID_HANDLE_VALUE) {
        cout << "Closing named pipe connection..." << endl;
        FlushFileBuffers(hPipe);
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
        cout << "Named pipe closed" << endl;
    }

    cout << "=== DMD Cleanup Complete ===" << endl;
}


int main()
{

    // Named pipe for Python communication
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    bool pipeConnected = false;
    // Communication variables
    char buffer[128] = {0};   // Array to store the message from Python
    char response[64] = {0};  // Response to send back to Python

    // Device and sequence IDs (initialize to ALP_INVALID_ID for safe cleanup)
    ALP_ID nDevId = ALP_INVALID_ID, nSeqId1 = ALP_INVALID_ID, nSeqId2 = ALP_INVALID_ID;
    long nDmdType = 0, nSizeX = 0, nSizeY = 0;

    // TODO : Implement function that takes frame rate and display time to calculate nFramesSeq
        // Frame settings
    const long nBit = 8;
    int color;

    // Frame settings - User-friendly parameters
    const int frameRateHz = 30;                    // Frame rate in Hz
    const int grayDisplayTimeMs = 100;             // Gray frame display time in milliseconds  (we keep it lower so as soon as mindysplaytimesgray is done, the code is responsive to update the gray frame)
    const int imgDisplayTimeMs = 300;              // Image frame display time in milliseconds
    const int minDisplayTimesGray = 4;             // Number of times minimum to display the gray sequence 

    // Calculate frame period in milliseconds (as float for precision)
    const float frameTimeMs = 1000.0f / frameRateHz;

    // Ensure at least the requested time (round up)
    const long nFramesSeq1 = (long)ceil(grayDisplayTimeMs / frameTimeMs);
    const long nFramesSeq2 = (long)ceil(imgDisplayTimeMs / frameTimeMs);

    // Convert to microseconds for ALP API
    const long frameTime = 1000000 / frameRateHz;
    
    cout << "Gray actual duration: " << nFramesSeq1 * frameTimeMs << " ms / " << nFramesSeq1 << " frames" << endl;
    cout << "Image actual duration: " << nFramesSeq2 * frameTimeMs << " ms / " << nFramesSeq2 << " frames"  << endl;

    // Sequence frame allocation - must be multiple of 256 as required by ALP API
    const long nOffset = 256;

    // Status variables
    tAlpProjProgress QueueInfo;
    long totalFramesDisplayed = 0;// Total frames displayed, used for communicating at which frame (->therefore trigger) a sequence started

    // I am trying to use totalFramesDisplayed as a tracker 
    // for how many frames the dmd as played just before queuing another
    // sequence, so that I can know at which exact frame that sequence will start.


    long lastSequenceId = 0;
    auto lastDotTime = std::chrono::steady_clock::now();
    bool running = true;
    UCHAR *block1 = NULL; // Host memory block for frames ( Host = PC memory )
    UCHAR *block2 = NULL; // Host memory block for a single frame (block2)

    bool justPlayedSeq2 = false;

    srand((unsigned int)time(NULL));

    // Frame file path - resolved relative to executable directory
    // current_frame.bin is expected in the same directory as the .exe
    std::string exeDir = getExeDirectory();
    std::string framePathStr = exeDir + "current_frame.bin";
    const char* currentFramePath = framePathStr.c_str();

    cout << "DMD Control closedloop (Online Frame Generation) - Starting..." << endl;

    checkSystemMemory();

    // Setup named pipe for communication with Python
    setupNamedPipe(hPipe);

    // Setup DMD
    if (!setupDMD(nDevId, nDmdType, nSizeX, nSizeY)) {
        cout << "DMD setup failed" << endl;
        return 1;
    } else {
        cout << "DMD setup successful" << endl;
    }

    // Load frame dimensions from initial frame file
    int frameWidth, frameHeight, beg_w, beg_h;
    if (!loadFrameDimensions(currentFramePath, frameWidth, frameHeight, beg_w, beg_h, nSizeX, nSizeY)) {
        cout << "Failed to load frame dimensions. Make sure " << currentFramePath << " exists." << endl;
        cleanupResources(hPipe, block1, block2, nDevId, nSeqId1, nSeqId2);
        return 1;
    }

    // Variables recap
    // block1: Holds gray background frames in host memory (PC) - static, uploaded once
    // block2: Holds dynamically loaded frame from file - updated on each SHOW_FRAME command
    // frameTime: Frame rate set to 30 fps, each frame displays for 33.33ms
    // Sequence Architecture (Online Frame Generation):
    // - Sequence 1 (nSeqId1): Stores gray background frames (block1) - queued continuously for seamless display
    // - Sequence 2 (nSeqId2): Stores dynamically loaded frames (block2) - loaded from file and queued on demand
    // - Queue Mode: Uses ALP_PROJ_SEQUENCE_QUEUE to alternate between sequences
    // - The system maintains continuous display by automatically queuing sequence 1 after sequence 2 completes
    // - Frames are generated by external process and written to current_frame.bin
    // - Python sends SHOW_FRAME command after frame is ready

    // Generate frames
    color = 127;
    block1 = setUniformCenteredMemoryBlock( block1, nFramesSeq1, nSizeX, nSizeY, frameWidth, frameHeight, 
                                            beg_w, beg_h, color);
    // block1 = setUniformMemoryBlock( block1, nFramesSeq1, nSizeX, nSizeY,  color);

    color = 255;
    block2 = setUniformCenteredMemoryBlock( block2, nFramesSeq2, nSizeX, nSizeY, frameWidth, frameHeight, 
                                            beg_w, beg_h, color);
    // block2 = setUniformMemoryBlock( block2, nFramesSeq2, nSizeX, nSizeY,  color);

    cout << "Memory allocated for block 1 and block2 " << nFramesSeq1 << " frames and" << nFramesSeq2 << " frames" << endl;

    // Setup DMD sequences
    if (!setupSequences(nDevId, nSeqId1, nSeqId2, nBit,
                       frameTime, nFramesSeq1, nFramesSeq2, nOffset)) {
        cleanupResources(hPipe, block1, block2, nDevId, nSeqId1, nSeqId2);
        return 1;
    }
        
    // Upload frames to DMD
    if (!uploadBlocksToDMD(nDevId, nSeqId1, nSeqId2, block1, block2)) {
        cleanupResources(hPipe, block1, block2, nDevId, nSeqId1, nSeqId2);
        return 1;
    }

    
    // Free host memory as we don't need it anymore
    free(block1);
    block1 = NULL;

    // Queue initial gray sequences to create a buffer before main loop starts
    // This prevents immediate triggering of ensureContinuousDisplay() and gives wiggle room
    const int INITIAL_GRAY_SEQUENCES = 5;
    cout << "Queuing " << INITIAL_GRAY_SEQUENCES << " initial gray sequences..." << endl;

    int successfulQueues = 0;
    for (int i = 0; i < INITIAL_GRAY_SEQUENCES; i++) {
        if (ALP_OK != AlpProjStart(nDevId, nSeqId1)) {
            cout << "ERROR: Failed to queue initial gray sequence " << (i + 1) << "/" << INITIAL_GRAY_SEQUENCES << endl;
            cleanupResources(hPipe, block1, block2, nDevId, nSeqId1, nSeqId2);
            return 1;
        }

        totalFramesDisplayed += nFramesSeq1;
        successfulQueues++;
        cout << "  Sequence " << (i + 1) << "/" << INITIAL_GRAY_SEQUENCES
             << " queued (total frames: " << totalFramesDisplayed << ")" << endl;
    }

    cout << "Initial buffer: " << successfulQueues << " gray sequences queued successfully" << endl;

    // --- Main Loop ---
    while (running) {
        // Inquire about the current projection status
        AlpProjInquireEx(nDevId, ALP_PROJ_PROGRESS, &QueueInfo);
        long currentSequenceId = QueueInfo.SequenceId; // should I inquire this more often?

        // Update terminal output
        updateTerminalOutput(currentSequenceId, lastSequenceId, lastDotTime);

        // Process keyboard input
        processKeyboardInput(running, nDevId, nSeqId2, nOffset,
                    totalFramesDisplayed, nFramesSeq2, block2,
                    nSizeX, nSizeY, justPlayedSeq2, currentFramePath,
                    frameWidth, frameHeight, beg_w, beg_h);

        // Process pipe commands from Python
        processPipeCommands(hPipe, running, nDevId, nSeqId1, nSeqId2, nOffset,
                        totalFramesDisplayed, nFramesSeq1, nFramesSeq2, buffer, response, block2,
                        currentFramePath, nSizeX, nSizeY, frameWidth,
                        frameHeight, justPlayedSeq2, beg_w, beg_h);
            
                        
        // Check if we need to queue gray frames after sequence 2
        if (justPlayedSeq2) {
            // Queue minimum amount of gray frames to ensure display
            for (int i = 0; i < minDisplayTimesGray; i++) {
                // cout << "Queuing sequence 1 for gray frame repeat " << (i + 1) << "..." << endl;
                if (ALP_OK != AlpProjStart(nDevId, nSeqId1)) {
                    cout << "Failed to start sequence 1 for gray frame" << endl;
                    running = false;
                    break;
                }
                else{
                    cout << "Sequence 1 queued for gray frame repeat " << (i + 1) << "after frame " << totalFramesDisplayed << endl;
                    totalFramesDisplayed += nFramesSeq1;
                }
            }
            justPlayedSeq2 = false;
        }

        // Ensure continuous display. If queue <= 2 queue sequence 1
        ensureContinuousDisplay(nDevId, nSeqId1, QueueInfo, totalFramesDisplayed, nFramesSeq1, running);
    
        // Small sleep to prevent CPU hogging
        Sleep(10);
    }

    // Cleanup all resources
    cleanupResources(hPipe, block1, block2, nDevId, nSeqId1, nSeqId2);

    return 0;
}
