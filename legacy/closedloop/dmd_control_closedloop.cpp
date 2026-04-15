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

    // Set dimensions based on DMD type
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

// Load bin file into host memory
UCHAR* loadBinFileToHostMemory(const char* binFilePath, int& frameCount, int& frameWidth, int& frameHeight, 
                                int& beg_w, int& beg_h, int nSizeX, int nSizeY ) {
    FILE* file = NULL;
    UCHAR* hostFrameBuffer = NULL;
    
    errno_t err = fopen_s(&file, binFilePath, "rb");
    if (file == NULL) {
        cout << "Failed to open bin file: " << err << endl;
        return NULL;
    }
    
    // Read header
    short header[4]; // [width, height, frameCount, bitDepth]
    fread(header, sizeof(short), 4, file);
    
    frameWidth = header[0];
    frameHeight = header[1];
    frameCount = header[2];

    // Based on the dimension of the frames in the bin file, get vars to center on dmd

    beg_w = (nSizeX - frameWidth) / 2;
    beg_h = (nSizeY - frameHeight) / 2;

    //  debug info:

    cout << "frameWidth: " << frameWidth << ", frameHeight: " << frameHeight << endl;
    cout << "beg_w: " << beg_w << ", beg_h: " << beg_h << endl;
    cout << "xSize: " << nSizeX << ", ySize: " << nSizeY << endl;
    
    cout << "Bin file has " << frameCount << " frames (" << frameWidth << "x" << frameHeight << ") Bitdepth" << header[3] << endl;
    
    // Calculate total size needed - use long long to prevent overflow
    long long totalSize = (long long)frameCount * (long long)frameWidth * (long long)frameHeight;
    cout << "Calculated memory needed: " << totalSize / (1024 * 1024) << " MB" << endl;
    
    // Check if size is reasonable (less than 4GB for safety)
    if (totalSize > 4LL * 1024 * 1024 * 1024) {
        cout << "File too large: " << totalSize / (1024 * 1024 * 1024) << " GB" << endl;
        fclose(file);
        return NULL;
    }
    
    hostFrameBuffer = (UCHAR*)malloc((size_t)totalSize);
    
    if (hostFrameBuffer == NULL) {
        cout << "Failed to allocate host memory for bin file (" << totalSize / (1024 * 1024) << " MB)" << endl;
        fclose(file);
        return NULL;
    }
    
    cout << "Successfully allocated " << totalSize / (1024 * 1024) << " MB of host memory" << endl;
    
    // Skip header and read all frame data at once
    fseek(file, sizeof(short) * 4, SEEK_SET);
    size_t bytesRead = fread(hostFrameBuffer, 1, (size_t)totalSize, file);
    
    if (bytesRead != (size_t)totalSize) {
        cout << "Warning: Only read " << bytesRead << " of " << totalSize << " bytes" << endl;
    }
    
    fclose(file);
    cout << "Successfully loaded bin file to host memory" << endl;
    return hostFrameBuffer;
}

// Select frame from host memory and copy to block2
bool setBinFrameOnBlock(UCHAR* hostFrameBuffer, int frameIndex, int frameWidth, int frameHeight, 
                   UCHAR* block2, long nSizeX, long nSizeY, long nFramesToSet, int beg_w, int beg_h, int maxFrameIndex) {
    
    // nFramesToSet should be nFramesSeq2 if we are not using FLUT to repeat a frame
        // Basic error checking
    if (hostFrameBuffer == NULL || block2 == NULL) {
        cout << "ERROR: NULL buffer passed to setBinFrameOnBlock" << endl;
        return false;
    }

    if (frameIndex < 0  || frameIndex > maxFrameIndex) {
        cout << "ERROR: Invalid frame index " << frameIndex << endl;
        return false;
    }

    cout << " frameIndex: " << frameIndex << ", maxFrameIndex: " << maxFrameIndex << endl;

    // Clear destination first - need to clear the entire block2 memory
    memset(block2, 0, nSizeX * nSizeY * nFramesToSet);

    for (int frameOffset = 0; frameOffset < nFramesToSet; frameOffset++) {
        // Calculate destination frame position in block2
        UCHAR* destFrame = block2 + (frameOffset * nSizeX * nSizeY);
        
        // Copy the frame line by line
        for (int y = 0; y < frameHeight; y++) {

            // Calculate source position
            long long sourceOffset = (long long)frameIndex * frameWidth * frameHeight + y * frameWidth;

            // Calculate destination position
            long destOffset = (beg_h + y) * nSizeX + beg_w;

            // Make sure we're not going out of bounds
            if (destOffset + frameWidth > nSizeX * nSizeY) {
                cout << "ERROR: Destination position out of bounds" << endl;
                return false;
            }
            
            memcpy(
                destFrame + destOffset,
                hostFrameBuffer + sourceOffset,
                frameWidth
            );
                 
        }
    }
    return true;
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
    //     the FLUT is one single loockup table ( script )  
    //     where one line refers to a frame in block1
    //     sequences tell the DMD which lines to read from the FLUT

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
    
    // Configure FLUT mode
    // Using ALP_FLUT_18BIT is necessary instead of ALP_FLUT_9BIT cause otherwise
    // the flut would only have 2^9=512 entries. We would only be able to reference
    // 512 unique frames.

    // if (ALP_OK != AlpSeqControl(nDevId, nSeqId1, ALP_FLUT_MODE, ALP_FLUT_18BIT) ||
    //     ALP_OK != AlpSeqControl(nDevId, nSeqId2, ALP_FLUT_MODE, ALP_FLUT_18BIT)) {
    //     cout << "Failed to set FLUT mode for sequences" << endl;
    //     return false;
    // }
    // cout << "FLUT mode set for both sequences" << endl;

    // Set FLUT entries - Tell sequences how many frames to read from the FLUT for each sequence
    // "For seq1, use nFramesSeq1 of the 9-bit slots in the FLUT for this sequence"
    // Since the flut is set to 18-bit, each sequence frame takes up 2 9-bits slots of memory
    // so we need to double the number of frames to read
    // The ALP_FLUT_ENTRIES9 parameter needs to be doubled here
    // if (ALP_OK != AlpSeqControl(nDevId, nSeqId1, ALP_FLUT_ENTRIES9, nFramesSeq1*2)) {
    //     cout << "Failed to set FLUT entries for sequence 1" << endl;
    //     return false;
    // }
    // cout << "FLUT entries set for sequence 1: " << nFramesSeq1 << " frames" << endl;
    
    // if (ALP_OK != AlpSeqControl(nDevId, nSeqId2, ALP_FLUT_ENTRIES9, nFramesSeq2*2)) {
    //     cout << "Failed to set FLUT entries for sequence 2" << endl;
    //     return false;
    // }
    // cout << "FLUT entries set for sequence 2: " << nFramesSeq2 << " frames" << endl;

    // Set FLUT offset for sequence 2 to point to the second half
    // If the flut is set to 18-bit, 2 slots are used for each frame.
    // The ALP_FLUT_OFFSET9 parameter then needs to be doubled here
    // if (ALP_OK != AlpSeqControl(nDevId, nSeqId2, ALP_FLUT_OFFSET9, nOffset*2)) {
    //     cout << "Failed to set FLUT offset for sequence 2" << endl;
    //     return false;
    // }
    // cout << "FLUT offset set for sequence 2: " << nOffset << endl;

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


// Setup FLUT for both sequences
bool setupFLUT(ALP_ID nDevId, tFlutWrite& LutArray, long nOffset) {
    // Setup FLUT for sequence 1 (gray frame)
    // nSize   : How many entries/frames to write in a SINGLE WRITE OPERATION. 
    //           This is not the total number of frames in the FLUT which has been set 
    //           in AlpSeqControl(nDevId, nSeqId1, ALP_FLUT_ENTRIES9, nOffset * 2) above
    // This is what allows to update one sequence FLUT while the other is playing
    // nOffset : where to start writing in the FLUT ( LutArray )
    // The FLUT memory on the DMD is already a fixed size. The LutArray.nSize parameter doesn't "allocate" new memory;
    
    // This first write operation is relative to the first sequence
    // it writes exactly nOffset frames in the FLUT which happen to be exactly 
    // how many frames I am also setting for the first sequence to read with this 
    // write operation using LutArray.nsize

    LutArray.nSize = nOffset; // This tells the DMD "I'm about to send nSize frame numbers to the DMD internal FLUT", its not the lenght of any sequence
    LutArray.nOffset = 0;
    
    for (long i = 0; i < nOffset; i++) {
        LutArray.FrameNumbers[i] = 0;  // Frame 0 is gray
    }
    
    // Write the FLUT to the DMD - 1st sequence
    // We are setting 18bit mode, to allow for more than 2ˆ9=512 frames to be referenced by the FLUT (needed)
    // This means the in the DMD we are prepating to store 18bit numbers, each taking 2 9-bit slots in the FLUT
    // This is why every 9BIT setting is doubled. Check /alp.h line 202
    if (ALP_OK != AlpProjControlEx(nDevId, ALP_FLUT_WRITE_18BIT, &LutArray)) {
        cout << "Failed to write FLUT for sequence 1" << endl;
        return false;
    }
    cout << "FLUT table for sequence 1 filled and copied to DMD" << endl;
    
    // Setup FLUT for sequence 2 (initially black frame)
    LutArray.nSize = nOffset;
    LutArray.nOffset = nOffset;
    
    for (long i = 0; i < nOffset; i++) {
        LutArray.FrameNumbers[i] = 1;  // Frame 1 is black
    }
    
    if (ALP_OK != AlpProjControlEx(nDevId, ALP_FLUT_WRITE_18BIT, &LutArray)) {
        cout << "Failed to write FLUT for sequence 2" << endl;
        return false;
    }
    cout << "FLUT table for sequence 2 filled and copied to DMD" << endl;
    
    return true;
}

// Process keyboard input
void processKeyboardInput(bool& running, ALP_ID nDevId, ALP_ID nSeqId2, long nOffset, 
                        long& totalFramesDisplayed, long nFramesSeq2,
                         UCHAR* block2, UCHAR* hostFrameBuffer, long nSizeX, long nSizeY, 
                         int maxFrameIndex, int frameWidth, int frameHeight, bool& justPlayedSeq2, 
                         int beg_w, int beg_h) {
    if (!_kbhit()) {
        return;
    }
    
    char key = _getch();
    if (key == 'q' || key == 'Q') {
        running = false;
        return;
    }

    if (key == 'r' || key == 'R') {
        // Generate random frame index between 0 and maxFrameIndex
        // int randomFrameIndex = rand() % (maxFrameIndex + 1);
        // int randomFrameIndex = 2500;
        // int randomFrameIndex = 0;


        int randomFrameIndex = 3190;

        cout << "\nTrigger 'r': Loading random frame " << randomFrameIndex << "..." << endl;
        
        // Select the random frame from host memory and copy to block2

        
        if ( !setBinFrameOnBlock(hostFrameBuffer, randomFrameIndex, frameWidth, frameHeight, block2, 
                             nSizeX, nSizeY, nFramesSeq2, beg_w, beg_h, maxFrameIndex)) {
            cout << "Failed to set random frame on block2" << endl;
            return;
        }

        cout << "Random frame " << randomFrameIndex << " loaded into block2" << endl;
        // Update sequence 2 with the new frame - with retry logic
        int retryCount = 0;
        bool success = false;
        while (!success && retryCount < MAX_RETRIES) {
            if (ALP_OK == AlpSeqPut(nDevId, nSeqId2, ALP_DEFAULT, ALP_DEFAULT, block2)) {
                success = true;
            } else {
                retryCount++;
                cout << "Failed to update seq2 memory with random frame (attempt " << retryCount << "/" << MAX_RETRIES << ")" << endl;
                Sleep(RETRY_DELAY_MS);
            }
        }

        if (!success) {
            cout << "Failed to update sequence 2 with random frame after " << MAX_RETRIES << " attempts" << endl;
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
            cout << "Failed to start sequence 2 for random frame" << endl;
            return;
        }
        
        cout << "Random frame " << randomFrameIndex << " queued for display after frame " << totalFramesDisplayed << endl;
        totalFramesDisplayed += nFramesSeq2;
        justPlayedSeq2 = true;
    } 
    else if (key == 'b' || key == 'B') {
        cout << "\nTrigger 'b': Loading black frame..." << endl;
        
        // Create a black frame in block2
        memset(block2, 0, nSizeX * nSizeY);
        
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
        
        // Create a white frame in block2
        memset(block2, 255, nSizeX * nSizeY);
        
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
}

// Process commands from Python pipe
void processPipeCommands(HANDLE hPipe, bool& running, ALP_ID nDevId, ALP_ID nSeqId1, ALP_ID nSeqId2, long nOffset,
                         long& totalFramesDisplayed, long nFramesSeq1, long nFramesSeq2,
                         char* buffer, char* response, UCHAR* block2, UCHAR* hostFrameBuffer, int minDisplayTimesGray,
                         long nSizeX, long nSizeY, int maxFrameIndex, int frameWidth, int frameHeight,
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
            
            // Check for new IMG_XXXX command
            if (strncmp(buffer, "IMG_", 4) == 0 && strlen(buffer) >= 8) {
                // Extract the frame index (assumes format "IMG_XXXX" where XXXX is a number)
                int requestedFrameIndex = atoi(buffer + 4);
                
                // Validate frame index
                if (requestedFrameIndex < 0 || requestedFrameIndex > maxFrameIndex) {
                    cout << "\nInvalid frame index: " << requestedFrameIndex 
                         << " (max: " << maxFrameIndex << ")" << endl;
                    
                    // Send error response
                    sprintf_s(response, 64, "ERROR:INVALID_FRAME_INDEX");
                    DWORD bytesWritten = 0;
                    WriteFile(hPipe, response, strlen(response), &bytesWritten, NULL);
                    return;
                }
                
                cout << "\nPython requested image frame " << requestedFrameIndex << "..." << endl;

                // requestedFrameIndex = 0; // Show gray fr testing purposes

                // Select the requested frame from host memory and copy to block2
                if (!setBinFrameOnBlock(hostFrameBuffer, requestedFrameIndex, frameWidth, frameHeight, 
                               block2, nSizeX, nSizeY, nFramesSeq2, beg_w, beg_h, maxFrameIndex)) {
                    cout << "Failed to set requested frame on block2" << endl;
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
                        cout << "Failed to update seq2 memory with requested frame (attempt " << retryCount << "/" << MAX_RETRIES << ")" << endl;
                        Sleep(RETRY_DELAY_MS);
                    }
                }

                if (!success) {
                    cout << "Failed to update seq2 memory with requested frame after " << MAX_RETRIES << " attempts" << endl;
                    
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
                sprintf_s(response, 64, "IMG_STARTED:%ld:%d", totalFramesDisplayed, requestedFrameIndex);
                DWORD bytesWritten = 0;
                WriteFile(hPipe, response, strlen(response), &bytesWritten, NULL);
                
                cout << "Frame " << requestedFrameIndex << " queued for display after frame " << totalFramesDisplayed << endl;
                totalFramesDisplayed += nFramesSeq2;
                justPlayedSeq2 = true;

                // Queue sequence 1 for a few repeats to ensure a minimum gray display after each non gray image
                // for (int i = 0; i < minDisplayTimesGray; i++) {
                //     cout << "Queuing sequence 1 for gray frame repeat " << (i + 1) << "..." << endl;
                //     if (ALP_OK != AlpProjStart(nDevId, nSeqId1)) {
                //         cout << "Failed to start sequence 1 for gray frame" << endl;
                //         return;
                //     }
                //     totalFramesDisplayed += nFramesSeq1;
                // }

            }
            // Handle existing commands
            else if (strcmp(buffer, "BLACK") == 0) {
                cout << "\nPython requested BLACK frame..." << endl;
                
                // Create a black frame in block2
                memset(block2, 0, nSizeX * nSizeY);
                
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
                
                // Create a white frame in block2
                memset(block2, 255, nSizeX * nSizeY);
                
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
                             long& totalFramesDisplayed, long nFramesSeq1) {
    if (QueueInfo.nWaitingSequences <= 1) {
        AlpProjStart(nDevId, nSeqId1);
        // cout << "Queuing seq 1 for continuos gray after frame " << totalFramesDisplayed << endl;
        totalFramesDisplayed += nFramesSeq1;
    }
}

// Clean up all resources
void cleanupResources(HANDLE hPipe, UCHAR* block1, UCHAR* block2, ALP_ID nDevId, ALP_ID nSeqId1, ALP_ID nSeqId2) {
    // Close named pipe
    if (hPipe != INVALID_HANDLE_VALUE) {
        cout << "Closing named pipe connection..." << endl;
        FlushFileBuffers(hPipe);
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
    }

    // Free block memory if still allocated
    if (block1 != NULL) {
        free(block1);
    }
    
    if (block2 != NULL) {
        free(block2);
    }
    
    // Free DMD resources
    AlpDevHalt(nDevId);
    AlpSeqFree(nDevId, nSeqId1);
    AlpSeqFree(nDevId, nSeqId2);
    AlpDevFree(nDevId);
}


int main()
{

    // Named pipe for Python communication
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    bool pipeConnected = false;
    // Communication variables
    char buffer[128] = {0};   // Array to store the message from Python
    char response[64] = {0};  // Response to send back to Python

    // Device and sequence IDs
    ALP_ID nDevId, nSeqId1, nSeqId2;
    long nDmdType = 0, nSizeX = 0, nSizeY = 0;

    // TODO : Implement function that takes frame rate and display time to calculate nFramesSeq
        // Frame settings
    const long nBit = 8;
    long nFramesBin; 
    // long nFrames = 1000; // Number of frames to display, this is the number of frames in the bin file

    int color;
    int maxFrameIndex;

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

    // FLUT settings - must be multiple of 256 as required by ALP API
    const long nOffset = 256;          

    // Status variables
    tFlutWrite LutArray;  // hold FLUT entries
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

    // Bin file variables - resolved relative to executable directory
    // Place your .bin sequence file in the same directory as the .exe
    std::string exeDir = getExeDirectory();
    std::string binFilePathStr = exeDir + "bin_file.bin";
    const char* binFilePath = binFilePathStr.c_str();

    
    cout << "DMD Control closedloop - Starting..." << endl;
    
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
    
    // bin file variables
    int frameCount, frameWidth, frameHeight, beg_w, beg_h;
    // Put bin in the ram and get its frame dimensions, and values to center on the DMD
    UCHAR* hostFrameBuffer = loadBinFileToHostMemory(binFilePath, frameCount, frameWidth, frameHeight, 
                                                    beg_w, beg_h, nSizeX, nSizeY );
    if (hostFrameBuffer == NULL) {
        cout << "Failed to load bin file to host memory" << endl;
        return 1;
    } else {
        // Based on the dimension of the frames in the bin file, get vars to center on dmd
        maxFrameIndex = frameCount - 1;
    
        cout << " maxFrameIndex: " << maxFrameIndex << endl;
        cout << "Bin file loaded to host memory, frame count: " << frameCount 
             << ", frame width: " << frameWidth << ", frame height: " << frameHeight 
             << ", beg_w: " << beg_w << ", beg_h: " << beg_h << endl;
    }
    
    // Variables recap
    // block1: chunk of memory in the host (PC) that holds all frames
    // frameTime: This is the speed of your projector. You set it to run at 30 frames per second, so each frame is shown for exactly tot_ms. This speed is fixed.
    // The FLUT (Frame Look-Up Table): This is your script or storyboard. It's a long list of instructions. For example: "Show Frame #0. Then show Frame #0 again. Then show Frame #0 again..."
    // A Sequence (nSeqId1): This is a specific playback instruction. For example: "Play the first 256 instructions from the script." from the memory block I am handing you ( block1 in both cases)    
    // The length of this instruction set is the "sequence length."
    // nRepeatCount (ALP_SEQ_REPEAT): This is a meta-instruction. "Once you finish playing the 256 instructions for Sequence 1, repeat that entire block of 256 instructions nRepeatCount times."

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
        free(block1);
        free(block2);
        AlpDevFree(nDevId);
        return 1;
    }
        
    // Upload frames to DMD 
    if (!uploadBlocksToDMD(nDevId, nSeqId1, nSeqId2, block1, block2)) {
        free(block1);
        free(block2);
        AlpSeqFree(nDevId, nSeqId1);
        AlpSeqFree(nDevId, nSeqId2);
        AlpDevFree(nDevId);
        return 1;
    }

    // Setup FLUT
    // if (!setupFLUT(nDevId, LutArray, nOffset)) {
    //     free(block1);
    //     AlpSeqFree(nDevId, nSeqId1);
    //     AlpSeqFree(nDevId, nSeqId2);
    //     AlpDevFree(nDevId);
    //     return 1;
    // }
    
    // Free host memory as we don't need it anymore
    free(block1);
    block1 = NULL;

    // Start sequence 1 initially
    if (ALP_OK != AlpProjStart(nDevId, nSeqId1)) {
        std::cout << "Failed to start initial gray display" << std::endl;
    }
    else {
        totalFramesDisplayed += nFramesSeq1;
        cout << "Initial gray sequence started, frame count after this: " << totalFramesDisplayed << endl;
    }

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
                    hostFrameBuffer, nSizeX, nSizeY, maxFrameIndex, 
                    frameWidth, frameHeight, justPlayedSeq2, beg_w, beg_h);
        
        // Process pipe commands from Python
        processPipeCommands(hPipe, running, nDevId, nSeqId1, nSeqId2, nOffset,
                        totalFramesDisplayed, nFramesSeq1, nFramesSeq2, buffer, response, block2,
                        hostFrameBuffer, minDisplayTimesGray, nSizeX, nSizeY, maxFrameIndex, 
                        frameWidth, frameHeight, justPlayedSeq2, beg_w, beg_h);
            
                        
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

        // Ensure continuous display. If queue <= 1 queue sequence 1
        ensureContinuousDisplay(nDevId, nSeqId1, QueueInfo, totalFramesDisplayed, nFramesSeq1);
    
        // Small sleep to prevent CPU hogging
        Sleep(10);
    }

    // Cleanup all resources
    if (hostFrameBuffer != NULL) {
        free(hostFrameBuffer);
    }
    cleanupResources(hPipe, block1, block2, nDevId, nSeqId1, nSeqId2);
    
    return 0;
}
