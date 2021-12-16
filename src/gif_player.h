#pragma once

#include <AnimatedGIF.h>
#include <Arduino.h>
#include <SD_MMC.h>
#include <TFT_eSPI.h>

#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 135
// #define USE_DMA 1
#define BUFFER_SIZE 256            // Optimum is >= GIF width or integral division of width

class GifPlayer {
    private:
        static AnimatedGIF gif;
        static TFT_eSPI* tft;

        static File FSGifFile; // temp gif file holder

#ifdef USE_DMA
        static uint16_t usTemp[2][BUFFER_SIZE]; // Global to support DMA use
#else
        static uint16_t usTemp[1][BUFFER_SIZE];    // Global to support DMA use
#endif
        static bool dmaBuf;

        static int frame_delay;
        static int max_line;

        static void * GIFOpenFile(const char *fname, int32_t *pSize);
        static void GIFCloseFile(void *pHandle);
        static int32_t GIFReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen);
        static int32_t GIFSeekFile(GIFFILE *pFile, int32_t iPosition);
        static void GIFDraw(GIFDRAW *pDraw);

    public:
        static void begin(TFT_eSPI* tft);

        static bool start(const char* path);
        static bool play_frame(int* frame_delay);
        static void stop();

        static void set_max_line(int l);

};
