
/* 
On the fly convert MJPEG file to AVI format when uploaded via FTP.
Allows recordings to replay at correct frame rate on media players.
The file names must include the frame count to be converted, 
so older style files will still be uploaded as MJPEGs.

Optionally includes a PCM audio stream recorded from an analog microphone on pin 33.
Only records first 150 seconds per capture.
Use a microphone with AGC to optimise volume & clarity, eg MAX9814 
Use of microphone will slow down framerate and quality of recorded audio is low
More sophisticated filters could reduce the noise level
Audio is not replayed on streaming, only via uploaded AVI file

s60sc 2020
*/

#define USE_MICROPHONE false // to record from analog microphone attached to pin 33

/* AVI file format:
header:
 310 bytes
per jpeg:
 4 byte 00dc marker
 4 byte jpeg size
 jpeg frame content
0-3 bytes filler to align on DWORD boundary
per PCM (audio file)
 4 byte 01wb marker
 4 byte pcm size
 pcm content
 0-3 bytes filler to align on DWORD boundary
footer:
 4 byte idx1 marker
 4 byte index size
 per jpeg:
  4 byte 00dc marker
  4 byte 0000
  4 byte jpeg location
  4 byte jpeg size
 per pcm:
  4 byte 01wb marker
  4 byte 0000
  4 byte pcm location
  4 byte pcm size
*/

#include "Arduino.h"
#include "FS.h" 
#include "SD_MMC.h"
#include <regex>
#include "driver/adc.h"

// avi header data
static const uint8_t dcBuf[4] = {0x30, 0x30, 0x64, 0x63};   // 00dc
static const uint8_t wbBuf[4] = {0x30, 0x31, 0x77, 0x62};   // 01wb
static const uint8_t idx1Buf[4] = {0x69, 0x64, 0x78, 0x31}; // idx1
static const uint8_t zeroBuf[4] = {0x00, 0x00, 0x00, 0x00}; // 0000
static uint8_t* idxBuf;

#define AVI_HEADER_LEN 310 // AVI header length
static uint8_t aviHeader[AVI_HEADER_LEN] = { // AVI header template
  0x52, 0x49, 0x46, 0x46, 0x00, 0x00, 0x00, 0x00, 0x41, 0x56, 0x49, 0x20, 0x4C, 0x49, 0x53, 0x54,
  0x16, 0x01, 0x00, 0x00, 0x68, 0x64, 0x72, 0x6C, 0x61, 0x76, 0x69, 0x68, 0x38, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4C, 0x49, 0x53, 0x54, 0x6C, 0x00, 0x00, 0x00,
  0x73, 0x74, 0x72, 0x6C, 0x73, 0x74, 0x72, 0x68, 0x30, 0x00, 0x00, 0x00, 0x76, 0x69, 0x64, 0x73,
  0x4D, 0x4A, 0x50, 0x47, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x73, 0x74, 0x72, 0x66,
  0x28, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x01, 0x00, 0x18, 0x00, 0x4D, 0x4A, 0x50, 0x47, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x4C, 0x49, 0x53, 0x54, 0x56, 0x00, 0x00, 0x00, 
  0x73, 0x74, 0x72, 0x6C, 0x73, 0x74, 0x72, 0x68, 0x30, 0x00, 0x00, 0x00, 0x61, 0x75, 0x64, 0x73,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x01, 0x00, 0x00, 0x00, 0x11, 0x2B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x11, 0x2B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x73, 0x74, 0x72, 0x66,
  0x12, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x11, 0x2B, 0x00, 0x00, 0x11, 0x2B, 0x00, 0x00,
  0x01, 0x00, 0x08, 0x00, 0x00, 0x00, 
  0x4C, 0x49, 0x53, 0x54, 0x00, 0x00, 0x00, 0x00, 0x6D, 0x6F, 0x76, 0x69,
};

struct frameSizeStruct {
  uint8_t frameWidth[2];
  uint8_t frameHeight[2];
};
// indexed by frame type - needs to be consistent with sensor.h framesize_t enum
static const frameSizeStruct frameSizeData[] = {
  {{0x60, 0x00}, {0x60, 0x00}}, // 96X96
  {{0xA0, 0x00}, {0x78, 0x00}}, // qqvga 
  {{0xB0, 0x00}, {0x90, 0x00}}, // qcif 
  {{0xF0, 0x00}, {0xB0, 0x00}}, // hqvga 
  {{0xF0, 0x00}, {0xF0, 0x00}}, // 240X240
  {{0x40, 0x01}, {0xF0, 0x00}}, // qvga 
  {{0x90, 0x01}, {0x28, 0x01}}, // cif 
  {{0xE0, 0x01}, {0x40, 0x01}}, // hvga 
  {{0x80, 0x02}, {0xE0, 0x01}}, // vga 
  {{0x20, 0x03}, {0x58, 0x02}}, // svga 
  {{0x00, 0x04}, {0x00, 0x03}}, // xga 
  {{0x00, 0x05}, {0xD0, 0x02}}, // hd
  {{0x00, 0x05}, {0x00, 0x04}}, // sxga
  {{0x40, 0x06}, {0xB0, 0x04}}  // uxga 
};

extern const char* _STREAM_BOUNDARY; 
extern const char* _STREAM_PART;
static const size_t streamBoundaryLen = strlen(_STREAM_BOUNDARY);
static const size_t streamPartLen = strlen(_STREAM_PART)+6;
#define LENGTH_OFFSET 78 // from start of mjpeg boundary to Content-Length: value
#define REMAINDER_OFFSET 14 // from LENGTH_OFFSET to start of jpeg data
#define MJPEG_HDR (LENGTH_OFFSET + REMAINDER_OFFSET)
#define CHUNK_HDR 8 // bytes per jpeg hdr in AVI 
#define IDX_ENTRY 16 // bytes per index entry
#define MAX_FRAMES 20000

static char mjpegHdrStr[MJPEG_HDR];
static bool doAVI = false;
static bool doAVIheader = false;
static bool haveSoundFile = false;
static uint16_t frameCnt = 0;
static uint16_t framePtr = 0;
static uint16_t idxPtr = 0;
static uint32_t idxOffset;
static uint8_t frameType;
static uint8_t FPS;
static size_t fileSize;
static size_t audSize;
static size_t indexLen;
bool aviOn = true;  // set to false if do not want conversion to AVI  

// sound recording
#define SAMPLE_RATE 11025  // 11025Hz sample rate used - adequate for voice
#define AUDIO_RAM SAMPLE_RATE*150 //up to 150 secs in psram
#define RAMSIZE ((SAMPLE_RATE+1)/2)
static hw_timer_t* timer2 = NULL;
static uint8_t* psramBuf;
static uint32_t psramPtr = 0;
static uint8_t ramBuf[RAMSIZE]; // intermediate buffer for ISR as psram much slower
static volatile uint32_t ramPtr = 0;
static File wavFile;
static uint8_t bufferPointer;
static TaskHandle_t tranferBufHandle = NULL;

#define WAV_HEADER_LEN 44 // WAV header length
static uint8_t wavHeader[WAV_HEADER_LEN] = { // WAV header template
  0x52, 0x49, 0x46, 0x46, 0x00, 0x00, 0x00, 0x00, 0x57, 0x41, 0x56, 0x45, 0x66, 0x6D, 0x74, 0x20,
  0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x11, 0x2B, 0x00, 0x00, 0x11, 0x2B, 0x00, 0x00,
  0x01, 0x00, 0x08, 0x00, 0x64, 0x61, 0x74, 0x61, 0x00, 0x00, 0x00, 0x00,
};

int* extractMeta(const char* fname); 
void showProgress();  

size_t soundFile(File &fh) {
  // derive audio file name from video file but with extension .wav
  std::string wfile(fh.name());
  wfile = std::regex_replace(wfile, std::regex("mjpeg"), "wav");

  // check if wave file exists and get its size
  size_t fileSize = 0;
  wavFile = SD_MMC.open(wfile.data(), FILE_READ);
  if (wavFile) {
    fileSize = wavFile.size() - WAV_HEADER_LEN; 
    wavFile.seek(WAV_HEADER_LEN, SeekSet); // skip over header
  } else wavFile.close();
  haveSoundFile = (fileSize) ? true : false;
  return fileSize; 
}

bool isAVI(File &fh) {
  // extract file metadata and determine if mjpeg or avi upload
  int* meta = extractMeta(fh.name()); 
  frameCnt = (uint16_t)meta[3];
  if (!aviOn) frameCnt = 0; // frig to disable AVI conversion if required
  if (frameCnt > 0) { 
    // presence of frame count in file name indicates file suitable for conversion to AVI
    frameType = (uint8_t)meta[0];
    FPS = (uint8_t)meta[1];
    fileSize = fh.size();
    doAVI = true;
    doAVIheader = true;   
    audSize = soundFile(fh); // get audio file size if present
    Serial.print("Uploading as AVI");
    if (audSize) Serial.println(" with audio");
    else Serial.println("");
    return true;
  } else {
    doAVI = false;
    Serial.println("Uploading as MJPEG");
    return false;
  }
}

static inline void littleEndian(uint8_t* inBuff, uint32_t in) {
  // arrange bits in little endian order
  for (int i=0; i<4; i++) {
    inBuff[i] = in % 0x100;
    in = in >> 8;  
  }
}

static size_t buildAVIhdr(byte* &clientBuf) {
  // first call on file, update AVI header template with file specific details
  size_t moviSize = audSize + (fileSize - (streamBoundaryLen+streamPartLen)*frameCnt - streamBoundaryLen); 
  size_t aviSize = moviSize + AVI_HEADER_LEN + ((CHUNK_HDR+IDX_ENTRY) * (frameCnt+(haveSoundFile?1:0))); // AVI content size 
  // update aviHeader with relevant stats
  littleEndian(aviHeader+4, aviSize);
  littleEndian(aviHeader+0x20, (uint32_t)round(1000000.0f / FPS)); // usecs_per_frame 
  littleEndian(aviHeader+0x30, frameCnt);
  littleEndian(aviHeader+0x8C, frameCnt);
  littleEndian(aviHeader+0x84, FPS);
  littleEndian(aviHeader+0x12E, moviSize + ((frameCnt+(haveSoundFile?1:0)) * CHUNK_HDR) + 4); // data size 
  if (haveSoundFile) littleEndian(aviHeader+0x38, 2); // increase number of streams for audio
  littleEndian(aviHeader+0x100, audSize); // audio data size
  // apply video framesize to avi header
  memcpy(aviHeader+0x40, frameSizeData[frameType].frameWidth, 2);
  memcpy(aviHeader+0xA8, frameSizeData[frameType].frameWidth, 2);
  memcpy(aviHeader+0x44, frameSizeData[frameType].frameHeight, 2);
  memcpy(aviHeader+0xAC, frameSizeData[frameType].frameHeight, 2);
  
  memcpy(clientBuf, aviHeader, AVI_HEADER_LEN);
  doAVIheader = false;
  
  // prep buffer to store index data, gets appended to end of file
  idxBuf = (uint8_t*)ps_malloc((MAX_FRAMES+1)*IDX_ENTRY); // include some space for audio index
  memcpy(idxBuf, idx1Buf, 4); // index header
  littleEndian(idxBuf+4, (frameCnt+(haveSoundFile?1:0))*IDX_ENTRY); // size of index 
  idxOffset = 4;
  idxPtr = CHUNK_HDR;

  if (haveSoundFile) {
    // add sound file header if required
    memcpy(clientBuf+AVI_HEADER_LEN, wbBuf, 4); 
    littleEndian(clientBuf+AVI_HEADER_LEN+4, audSize);
    // add index
    memcpy(idxBuf+CHUNK_HDR, wbBuf, 4);
    memcpy(idxBuf+CHUNK_HDR+4, zeroBuf, 4);
    littleEndian(idxBuf+CHUNK_HDR+8, idxOffset); 
    littleEndian(idxBuf+CHUNK_HDR+12, audSize); 
    idxOffset += audSize + CHUNK_HDR;
    idxPtr += IDX_ENTRY;
  }
  indexLen = ((frameCnt+(haveSoundFile?1:0))*IDX_ENTRY)+CHUNK_HDR;
  return AVI_HEADER_LEN+(haveSoundFile?8:0);
}

static void buildIdx(size_t dataSize) {
  // build AVI video index into buffer - 16 bytes per frame
  memcpy(idxBuf+idxPtr, dcBuf, 4);
  memcpy(idxBuf+idxPtr+4, zeroBuf, 4);
  littleEndian(idxBuf+idxPtr+8, idxOffset); 
  littleEndian(idxBuf+idxPtr+12, dataSize); 
  idxOffset += dataSize + CHUNK_HDR;
  idxPtr += IDX_ENTRY; 
}

size_t readClientBuf(File &fh, byte* &clientBuf, size_t buffSize) {
  static int32_t readLen = 0; 
  static int jStart = 0; // start of current jpeg
  static int jEnd = 0; // end of current jpeg
  static int iPtr = 0; // pointer in index buffer
  static int hdrOffset = 0; // indicates if mjpeg header straddles buffers
  static bool theEnd = false;
  showProgress();
    
  if (theEnd) {
    // end of avi file processing, reset for next file
    theEnd = false;
    jStart = 0;
    jEnd = 0; 
    iPtr = 0;
    hdrOffset = 0;
    Serial.printf("\nProcessed %d of %d frames\n", framePtr, frameCnt);
    return 0; 
  }
  if (doAVI) {
    // AVI upload, make modifications
    if (doAVIheader) {
      framePtr = 0;
      return buildAVIhdr(clientBuf);
      
    } else {     
      if (haveSoundFile) {
        int readLen = wavFile.read(clientBuf, RAMSIZE); // already opened by soundFile()  
        // if data available return it, else move to next section on completion
        if (readLen) return readLen; 
        else {
          haveSoundFile = false; 
          wavFile.close();
        }
      }

      // process video file
      readLen = fh.available() ? fh.read(clientBuf, buffSize) : 0; // load 32k cluster from SD
      if (readLen == 0) {
        // reached end of file, append index data, loop until done
        size_t sendLen = buffSize;
        if (indexLen-iPtr > buffSize) {
          // index bigger than buffer
          memcpy(clientBuf, idxBuf+iPtr, buffSize);
          iPtr += buffSize;
        } else {
          // final part of index
          memcpy(clientBuf, idxBuf+iPtr, indexLen-iPtr);
          sendLen = indexLen-iPtr;
          free(idxBuf); 
          theEnd = true;
        }
        return sendLen;
        
      } else {
        // get next buffer to modify to remove mjpeg headers and add avi headers 
        while (true) { // break out of loop when conditions occur
          if (jEnd < readLen) {
            // move to mjpeg header
            if (hdrOffset > 0) {
              // need to shift up buffer and copy in saved partial mjpeg header 
              memmove(clientBuf+hdrOffset, clientBuf, buffSize); 
              memcpy(clientBuf, mjpegHdrStr, hdrOffset); 
              readLen += hdrOffset;
              hdrOffset = jEnd = 0;
            }
            
            if (MJPEG_HDR > (readLen-jEnd)) {
              // remaining buffer content less than mjpeg header block, so postpone to next buffer
              hdrOffset = (readLen-jEnd);
              if (hdrOffset > 0) {
                memcpy(mjpegHdrStr, clientBuf+jEnd, hdrOffset); // string containing partial mjpeg header
                readLen -= hdrOffset;
                break;     
              } // else ignore     
            }

            jStart = jEnd + LENGTH_OFFSET; // offset from end of previous jpeg    
            if (jStart > readLen) {
                jEnd = readLen - jStart; // set -ve as offset to next buffer
                readLen = jEnd;
                break;
            }
            // extract jpeg size
            memcpy(mjpegHdrStr, clientBuf+jStart, 10); // string containing jpeg size
            mjpegHdrStr[10] = 0; // terminator
            size_t jpegSize = atoi(mjpegHdrStr); 
            if (jpegSize == 0) {
              Serial.printf("\nERROR: AVI conversion failed on frame: %u\n", framePtr);
              jStart = 0;
              jEnd = 0; 
              iPtr = 0;
              hdrOffset = 0;
              readLen = 0;
              break;
            }
            jStart += REMAINDER_OFFSET;                                        
            // create AVI header for jpeg
            memcpy(clientBuf+jEnd, dcBuf, 4); 
            littleEndian(clientBuf+jEnd+4, jpegSize);
            buildIdx(jpegSize); // build index entry for this jpeg
            framePtr++;
            
            // shift jpeg data so starts after avi header
            readLen -= (MJPEG_HDR - CHUNK_HDR); // length of relevant data reduced  
            memmove(clientBuf+jEnd+CHUNK_HDR, clientBuf+jStart, readLen-jEnd);  
            // determine end of this jpeg
            jEnd += CHUNK_HDR + jpegSize;      
            if (jEnd > readLen) {
              jEnd -= readLen; // adjust for next buffer 
              break; 
              
            }
          } else {
            // for jpeg bigger than buffer
            jEnd -= readLen;
            break;
          }
        }   
      }
      // post loop processing, return modified data for ftp
      if (readLen < 0) return jStart-LENGTH_OFFSET;  // if reached end of file, send last part of final jpeg 
      else return readLen; 
    } 
  } else {
    // mjpeg upload, just return what received from SD card
    return fh.read(clientBuf, buffSize); 
  }
}

/************** sound recording *******************/

// record from ADC at sample rate and store in PSRAM 
// at end write to SD card as WAV file so can read by media players
// combined into AVI file as PCM channel on FTP upload

void IRAM_ATTR onSampleISR() { 
  // on timer interrupt, sample microphone (12 bits) and save 8 MSB in psram
  // pin 33 is only one available with ADC
  if (ramPtr++ >= RAMSIZE) ramPtr = 0;
  ramBuf[ramPtr] = (uint8_t)(adc1_get_raw(ADC1_CHANNEL_5) >> 4);   
}

void tranferBufTask(void* parameter) {
  while (true) {
    // periodically transfers half of ram buffer to psram
    static bool bottomDone = false;
    bool doTransfer = false;
    size_t ramOffset = 0;
    if (!bottomDone && ramPtr > RAMSIZE/2) {
      // transfer bottom half
      bottomDone = true;
      doTransfer = true;
    }
    if (bottomDone && ramPtr < RAMSIZE/2) {
      // transfer top half
      bottomDone = false;
      ramOffset = RAMSIZE/2;
      doTransfer = true;
    }
    if (doTransfer && psramPtr < AUDIO_RAM-(RAMSIZE/2)) {
      memcpy(psramBuf+psramPtr, ramBuf+ramOffset, RAMSIZE/2);
      psramPtr += RAMSIZE/2;
    }
    delay(1000*RAMSIZE/(3*SAMPLE_RATE));
  }
}

void startAudio() {
  // start a recording
  if (USE_MICROPHONE) {
    adc1_config_width(ADC_WIDTH_BIT_12); // configure 12 bit ADC
    //  adc1_config_channel_atten(ADC1_CHANNEL_5,  ADC_ATTEN_DB_6); 
    if (psramBuf) free(psramBuf);
    psramBuf = (uint8_t*)ps_malloc(AUDIO_RAM); // up to 150 secs audio per recording in psram
    psramPtr = WAV_HEADER_LEN; // allow space for header
    ramPtr = 0;
    // timer 2 interrupt at audio sample rate
    timer2 = timerBegin(2, 80000000/SAMPLE_RATE, true); // ticks per SAMPLE_RATE
    timerAttachInterrupt(timer2, &onSampleISR, true);
    timerAlarmWrite(timer2, 1, true); 
    timerAlarmEnable(timer2);
    if (tranferBufHandle == NULL) xTaskCreate(&tranferBufTask, "tranferBufTask", 4096, NULL, 2, &tranferBufHandle);
  } 
}

void noiseFilter() {
  // single pass moving average filter to reduce noise 
  const uint8_t bins = 8;
  uint8_t integratingBuffer[bins-1];
  for (uint32_t s=WAV_HEADER_LEN; s<psramPtr; s++) {
    // add sample to the cyclic integrating buffer 
    bufferPointer = (bufferPointer+1)%(bins-1);
    integratingBuffer[bufferPointer] = psramBuf[s];
    
    // sum the current content of the buffer to create one filtered sample
    uint16_t filteredSample = integratingBuffer[bufferPointer]; // double weight for current sample
    for (int k=0; k<bins-1; k++) filteredSample += integratingBuffer[k]; 
      
    // filteredSample is now a sum of <bins> samples range 0-255
    // so divide to fit into uint8_t
    psramBuf[s] = (uint8_t)(filteredSample/bins); // store filtered data
  }
}

void finishAudio(const char* mjpegName, bool isValid) {
  if (USE_MICROPHONE) {
    // finish a recording and save
    timerEnd(timer2);
    if (tranferBufHandle != NULL) vTaskDelete(tranferBufHandle);
    tranferBufHandle = NULL;
    noiseFilter();
    if (isValid) {
      // build wav file name
      std::string wfile(mjpegName);
      wfile = std::regex_replace(wfile, std::regex("mjpeg"), "wav");
      size_t _psramPtr = (psramPtr%2 == 0) ? psramPtr : --psramPtr; // size needs to be even number of bytes
  
      // update wav header
      littleEndian(wavHeader+4, _psramPtr-CHUNK_HDR); // wav file size
      littleEndian(wavHeader+WAV_HEADER_LEN-4, _psramPtr-WAV_HEADER_LEN); // wav data size
      memcpy(psramBuf, wavHeader, WAV_HEADER_LEN);
      // write psram to wav file on sd card 
      File wavFile = SD_MMC.open(wfile.data(), FILE_WRITE);
      uint32_t wTime = millis();
      int written = 0;
      while (_psramPtr) {
        int writeLen = _psramPtr > RAMSIZE ? RAMSIZE : _psramPtr;
        wavFile.write(psramBuf+written, writeLen);
        _psramPtr -= writeLen;
        written += writeLen;
      }
      wavFile.close();   
      wTime = millis() - wTime;
      Serial.printf("\nSaved %s to SD in %u ms for %ukB\n", wfile.data(), wTime, written/1024);
    } 
    if (psramBuf) free(psramBuf);
    psramBuf = NULL;
  }
}

bool useMicrophone() {
  return USE_MICROPHONE;
}
