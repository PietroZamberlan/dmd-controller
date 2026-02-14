// utils.cpp: Defines utils for the console application.
//

#include "stdafx.h"


long CreateFrames(UCHAR *block, UCHAR *blockBCB, UCHAR *frameCB, long nSizeX, long nSizeY, long CheckSizeX, long CheckSizeY, long NbCheckX, long NbCheckY, long LengthRepeat, long FrameNb, long nFrames) {

	/* --------- Draw n frames ----------- */

	// Initialisation

	long OffsetX, OffsetY, BufferPos, ix, iy, ifx, ify, BitNb, ByteNb, isWhite, nCurrent;

	// OffsetX = 720 + 720 / 2 - 1 - CheckSizeX * NbCheckX / 2;
	// OffsetY = 240 + 720 / 2 - 1 - CheckSizeY * NbCheckY / 2;
	OffsetX = nSizeX / 2 - CheckSizeX * NbCheckX / 2;
	OffsetY = nSizeY / 2 - CheckSizeY * NbCheckY / 2;

	nCurrent = 0;

	// ...
	while (++nCurrent <= nFrames) { /// Frame Nb is 1 in first iteration)

									//FillMemory(frameCB, nSizeX*nSizeY, 0); // quick
		FillMemory(frameCB, nSizeX*nSizeY, 0); // H.A: changing kevins code for the inverted display case

											   //Find buffer position. First check if we are in a repeat. 

		if (long(floor(double(FrameNb / LengthRepeat))) % 2 == 0) {//We are not in a repeat
			BufferPos = long(floor(double(FrameNb / LengthRepeat)) / 2)*LengthRepeat + (FrameNb	% LengthRepeat);
			//printf("%d \n",BufferPos);
		}
		else {//We are in a repeat
			BufferPos = FrameNb % LengthRepeat;
		}

		BufferPos = BufferPos * NbCheckX*NbCheckY;

		for (iy = 1; iy <= NbCheckY; iy++) {
			for (ix = 1; ix <= NbCheckX; ix++) {
				ByteNb = long(floor(double((BufferPos + (ix - 1 + NbCheckX * (iy - 1))) / 8)));
				BitNb = (BufferPos + (ix - 1 + NbCheckX * (iy - 1))) % 8;
				isWhite = (blockBCB[ByteNb] & (1 << BitNb)) >> BitNb;
				//printf("%d %d %d \n",BufferPos,ByteNb,BitNb);


				if (isWhite == 1) {//need to fill with 0  //changing kevins code for the inverted display case 
					for (ifx = 1; ifx <= CheckSizeX; ifx++) {
						for (ify = 1; ify <= CheckSizeY; ify++) {
							//frameCB[nSizeX*(OffsetY + (iy - 1)*CheckSizeY + ify - 1) + (OffsetX + (ix - 1)*CheckSizeX + ifx - 1)] = 255; // original kevin G. code
							frameCB[nSizeX*(OffsetY + (iy - 1)*CheckSizeY + ify - 1) + (OffsetX + (ix - 1)*CheckSizeX + ifx - 1)] = 255; //changing kevins code for the inverted display case
																																		 //to be checked if nSizeX or nSizeY. Also, there must be a faster way... 
						}
					}

				}
			}
		}

		memcpy(block + (nCurrent - 1)*nSizeX*nSizeY, frameCB, nSizeX*nSizeY);

		FrameNb++;


	}

	//printf("nCurrent = %d",nCurrent);

	/* --------- END Draw n frames ----------- */

	return FrameNb;

}
