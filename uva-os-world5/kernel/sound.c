#define K2_DEBUG_WARN 1

// sound device based on pwm, will output to 3.5mm jack 
// largely ported from Circle
//  regarding HDMI sound output, cf README-fxl

// ref: https://github.com/babbleberry/rpi4-osdev/blob/master/part9-sound/kernel.c
//      this is for rpi4, but close enough 
//      https://github.com/rsta2/circle/blob/master/lib/sound/pwmsoundbasedevice.cpp
// app code: Circle 34-sounddevices.cpp

#include "utils.h"
#include "plat.h"
#include "spinlock.h"
#include "file.h"
#include "fcntl.h"
#include "circle-glue.h"
#include "spinlock.h"
#include "gpio.h"

// circle dmacommon.h
enum TDREQ
{
	DREQSourceNone	 = 0,
#if RASPPI >= 4
	DREQSourcePWM1	 = 1,
#endif
	DREQSourcePCMTX	 = 2,
	DREQSourcePCMRX	 = 3,
	DREQSourcePWM	 = 5,
	DREQSourceSPITX	 = 6,
	DREQSourceSPIRX	 = 7,
	DREQSourceEMMC	 = 11,
	DREQSourceUARTTX = 12,
	DREQSourceUARTRX = 14
};

//
// PWM device selection
//
#if RASPPI <= 3
	#define CLOCK_RATE	250000000
	#define PWM_BASE	ARM_PWM_BASE
	#define DREQ_SOURCE	DREQSourcePWM
#else
	#define CLOCK_RATE	125000000
	#define PWM_BASE	ARM_PWM1_BASE
	#define DREQ_SOURCE	DREQSourcePWM1
#endif

//
// PWM register offsets
//
#define PWM_CTL			(PWM_BASE + 0x00)
#define PWM_STA			(PWM_BASE + 0x04)
#define PWM_DMAC		(PWM_BASE + 0x08)
// NOTEs on: PWM_RNG1/2. 
// "define the range for the corresponding channel. In PWM mode
// evenly distributed pulses are sent within a period of length defined by 
// this register"           cf SoC manual "RNG1 Register"
#define PWM_RNG1		(PWM_BASE + 0x10) 
#define PWM_DAT1		(PWM_BASE + 0x14)
#define PWM_FIF1		(PWM_BASE + 0x18)
#define PWM_RNG2		(PWM_BASE + 0x20)
#define PWM_DAT2		(PWM_BASE + 0x24)

//
// PWM control register
//
#define ARM_PWM_CTL_PWEN1	(1 << 0)
#define ARM_PWM_CTL_MODE1	(1 << 1)
#define ARM_PWM_CTL_RPTL1	(1 << 2)
#define ARM_PWM_CTL_SBIT1	(1 << 3)
#define ARM_PWM_CTL_POLA1	(1 << 4)
#define ARM_PWM_CTL_USEF1	(1 << 5)        // use fifo
#define ARM_PWM_CTL_CLRF1	(1 << 6)       
#define ARM_PWM_CTL_MSEN1	(1 << 7)
#define ARM_PWM_CTL_PWEN2	(1 << 8)
#define ARM_PWM_CTL_MODE2	(1 << 9)
#define ARM_PWM_CTL_RPTL2	(1 << 10)
#define ARM_PWM_CTL_SBIT2	(1 << 11)
#define ARM_PWM_CTL_POLA2	(1 << 12)
#define ARM_PWM_CTL_USEF2	(1 << 13)       // use fifo
#define ARM_PWM_CTL_MSEN2	(1 << 14)

//
// PWM status register
//
#define ARM_PWM_STA_FULL1	(1 << 0)
#define ARM_PWM_STA_EMPT1	(1 << 1)
#define ARM_PWM_STA_WERR1	(1 << 2)
#define ARM_PWM_STA_RERR1	(1 << 3)
#define ARM_PWM_STA_GAPO1	(1 << 4)
#define ARM_PWM_STA_GAPO2	(1 << 5)
#define ARM_PWM_STA_GAPO3	(1 << 6)
#define ARM_PWM_STA_GAPO4	(1 << 7)
#define ARM_PWM_STA_BERR	(1 << 8)
#define ARM_PWM_STA_STA1	(1 << 9)
#define ARM_PWM_STA_STA2	(1 << 10)
#define ARM_PWM_STA_STA3	(1 << 11)
#define ARM_PWM_STA_STA4	(1 << 12)

//
// PWM DMA configuration register
//
#define ARM_PWM_DMAC_DREQ__SHIFT	0
#define ARM_PWM_DMAC_PANIC__SHIFT	8
#define ARM_PWM_DMAC_ENAB		(1 << 31)

//
// DMA controller
//
// manual: "DMA Control And Status register"
#define ARM_DMACHAN_CS(chan)		(ARM_DMA_BASE + ((chan) * 0x100) + 0x00) 
	#define CS_RESET			(1 << 31)
	#define CS_ABORT			(1 << 30)
	#define CS_WAIT_FOR_OUTSTANDING_WRITES	(1 << 28)
	#define CS_PANIC_PRIORITY_SHIFT		20
		#define DEFAULT_PANIC_PRIORITY		15
	#define CS_PRIORITY_SHIFT		16
		#define DEFAULT_PRIORITY		1
	#define CS_ERROR			(1 << 8)
    #define CS_WAITING_FOR_OUTSTANDING_WRITES			(1 << 6)    // fxl
    #define CS_DREQ_STOPS_DMA			(1 << 5)    // fxl
    #define CS_PAUSED			(1 << 4)    // fxl
	#define CS_INT				(1 << 2)
	#define CS_END				(1 << 1)
	#define CS_ACTIVE			(1 << 0)
#define ARM_DMACHAN_CONBLK_AD(chan)	(ARM_DMA_BASE + ((chan) * 0x100) + 0x04)
#define ARM_DMACHAN_TI(chan)		(ARM_DMA_BASE + ((chan) * 0x100) + 0x08)    // "Transfer info"
	#define TI_PERMAP_SHIFT			16
	#define TI_BURST_LENGTH_SHIFT		12
		#define DEFAULT_BURST_LENGTH		0
	#define TI_SRC_IGNORE			(1 << 11)
	#define TI_SRC_DREQ			(1 << 10)
	#define TI_SRC_WIDTH			(1 << 9)
	#define TI_SRC_INC			(1 << 8)
	#define TI_DEST_DREQ			(1 << 6)
	#define TI_DEST_WIDTH			(1 << 5)
	#define TI_DEST_INC			(1 << 4)
	#define TI_WAIT_RESP			(1 << 3)
	#define TI_TDMODE			(1 << 1)
	#define TI_INTEN			(1 << 0)
#define ARM_DMACHAN_SOURCE_AD(chan)	(ARM_DMA_BASE + ((chan) * 0x100) + 0x0C)
#define ARM_DMACHAN_DEST_AD(chan)	(ARM_DMA_BASE + ((chan) * 0x100) + 0x10)
#define ARM_DMACHAN_TXFR_LEN(chan)	(ARM_DMA_BASE + ((chan) * 0x100) + 0x14)
	#define TXFR_LEN_XLENGTH_SHIFT		0
	#define TXFR_LEN_YLENGTH_SHIFT		16
	#define TXFR_LEN_MAX			0x3FFFFFFF
	#define TXFR_LEN_MAX_LITE		0xFFFF          // fxl: 64K
#define ARM_DMACHAN_STRIDE(chan)	(ARM_DMA_BASE + ((chan) * 0x100) + 0x18)
	#define STRIDE_SRC_SHIFT		0
	#define STRIDE_DEST_SHIFT		16
#define ARM_DMACHAN_NEXTCONBK(chan)	(ARM_DMA_BASE + ((chan) * 0x100) + 0x1C)
#define ARM_DMACHAN_DEBUG(chan)		(ARM_DMA_BASE + ((chan) * 0x100) + 0x20)
	#define DEBUG_LITE			(1 << 28)
#define ARM_DMA_INT_STATUS		(ARM_DMA_BASE + 0xFE0)
#define ARM_DMA_ENABLE			(ARM_DMA_BASE + 0xFF0)

// circle dmachannel.h
struct TDMAControlBlock {
    u32 nTransferInformation;
    u32 nSourceAddress; // bus addr
    u32 nDestinationAddress;
    u32 nTransferLength;
    u32 n2DModeStride;
    u32 nNextControlBlockAddress;
    u32 nReserved[2];
} PACKED; //  shared with dma engine

typedef enum TPWMSoundState {
    PWMSoundIdle,
    PWMSoundRunning,
    PWMSoundCancelled,
    PWMSoundTerminating,
    PWMSoundError,
    PWMSoundUnknown
} TPWMSoundState;

typedef enum TSoundFormat			/// All supported formats are interleaved little endian
{
	SoundFormatUnsigned8,		/// Not supported as hardware format
	SoundFormatSigned16,
	SoundFormatSigned24,
	SoundFormatUnsigned32,		/// Not supported as write format  (fxl: why
	SoundFormatUnknown
} TSoundFormat;

#define SOUND_HW_CHANNELS	2
#define SOUND_MAX_SAMPLE_SIZE	(sizeof (u32))
#define SOUND_MAX_FRAME_SIZE	(SOUND_HW_CHANNELS * SOUND_MAX_SAMPLE_SIZE)

// typedef void TSoundNeedDataCallback (void *pParam);

typedef void TInterruptHandler (void *pParam); 
extern TInterruptHandler *dma_irq; // irq.c
extern void *dma_irq_param;  // irq.c 

/// assert. needed by assert()
void __assert_fail(const char * assertion, const char * file, 
  unsigned int line, const char * function) {  
  printf("assertion failed: %s at %s:%d\n", assertion, file, (int)line); 
  asm volatile("msr	daifset, #0b0010 "); // disable irq
  while (1); 
}

// hw specific info 
struct sound_dev {
    // gpio, pwm 
	unsigned m_nRange; // cf sound.c PWM_RNG1 
    struct gpioclock_dev m_Clock; 

    // dma xfer     (from CPWMSoundBaseDevice)
	unsigned m_nChunkSize;
	volatile TPWMSoundState m_State;
	unsigned m_nDMAChannel;     // dma chan id
	u32 *m_pDMABuffer[2];    // data for dma. each one chunk size. ker va. to be allocated
	u8 *m_pControlBlockBuffer[2];   // metadata for dma. to be allocated.
	struct TDMAControlBlock *m_pControlBlock[2]; // points to bufs above
	unsigned m_nNextBuffer;			// 0 or 1

    // (CPWMSoundDevice)
    // below is only to support the preloaded "playback" function (mostly for in-kernel testing)
    // TODO: factor out?
	unsigned  m_nRangeBits;         // derived from m_nRange. bits in use per sample
	boolean	  m_bChannelsSwapped;   // is L/R channel swapped? TODO refactor
	u8	 *m_pSoundData;     // sound samples passed in externally
	unsigned  m_nSamples;
	unsigned  m_nChannels;
	unsigned  m_nBitsPerSample;
    // -------  

    struct spinlock m_SpinLock;     // for hw resources
}; 

// driver resources (hw indep info): buffer mgmt, queuing, format, etc
// (Circle CSoundBaseDevice)
struct sound_drv {
    int valid;       // 1 if valid
	TSoundFormat m_HWFormat;
	unsigned m_nSampleRate;
	boolean m_bSwapChannels;

	unsigned m_nHWSampleSize;       // frames that goes to hw 
	unsigned m_nHWFrameSize;
	unsigned m_nQueueSize;
	unsigned m_nNeedDataThreshold;      // user writers sleep on this

	int m_nRangeMin;        // bookkeeping only?
	int m_nRangeMax;        // bookkeeping only?
	u8 m_NullFrame[SOUND_MAX_FRAME_SIZE];		// a null frame for padding

	TSoundFormat m_WriteFormat;     // frames as from user
	unsigned m_nWriteChannels;      
	unsigned m_nWriteSampleSize;
	unsigned m_nWriteFrameSize;

    // below for continuous play
	u8 *m_pQueue;			// Ring buffer, to be allocated
	unsigned m_nInPtr;
	unsigned m_nOutPtr;

	// TSoundNeedDataCallback *m_pCallback;
	// void *m_pCallbackParam;

	struct spinlock m_SpinLock;    // lock for protecting queue (syscall/irq concurrency)

    struct sound_dev dev; 
}; 

// 30-bit DMA-able memory 
// per Circle "high memory region (memory >= 3 GB is not safe to be DMA-able and is not used)"
// rpi3 (1GB of mem) should be fine
static void* malloc_dma30(unsigned size) { 
    void *p = malloc(size);
    V("malloc_dma30 returns %lx", (unsigned long)p); 
    return p; 
} 

// CPWMSoundBaseDevice::SetupDMAControlBlock
static void setup_dma_control_block(struct sound_dev *dev, unsigned nID) {
	assert (nID <= 1);

	dev->m_pDMABuffer[nID] = malloc_dma30(dev->m_nChunkSize * sizeof(u32));
	assert (dev->m_pDMABuffer[nID] != 0);

	dev->m_pControlBlockBuffer[nID] = malloc_dma30(sizeof (struct TDMAControlBlock) + 31);
	assert (dev->m_pControlBlockBuffer[nID] != 0);
	dev->m_pControlBlock[nID] = 
        (struct TDMAControlBlock *) (((uintptr) dev->m_pControlBlockBuffer[nID] + 31) & ~31);

	dev->m_pControlBlock[nID]->nTransferInformation     =   (DREQ_SOURCE << TI_PERMAP_SHIFT)
						         | (DEFAULT_BURST_LENGTH << TI_BURST_LENGTH_SHIFT)
						         | TI_SRC_WIDTH
						         | TI_SRC_INC
						         | TI_DEST_DREQ
						         | TI_WAIT_RESP
						         | TI_INTEN;
	dev->m_pControlBlock[nID]->nSourceAddress           = BUS_ADDRESS (VA2PA(dev->m_pDMABuffer[nID]));
	dev->m_pControlBlock[nID]->nDestinationAddress      = (PWM_FIF1 & 0xFFFFFF) + GPU_IO_BASE;
	dev->m_pControlBlock[nID]->n2DModeStride            = 0;
	dev->m_pControlBlock[nID]->nReserved[0]	       = 0;
	dev->m_pControlBlock[nID]->nReserved[1]	       = 0;
}

// CPWMSoundBaseDevice::RunPWM
static void RunPWM (struct sound_dev *dev) {    
    int n = gpioclock_start_rate(&dev->m_Clock, CLOCK_RATE); BUG_ON(n==0); 
    ms_delay(2); 

	assert ((1 << 8) <= dev->m_nRange && dev->m_nRange < (1 << 16));
	write32 (PWM_RNG1, dev->m_nRange);
	write32 (PWM_RNG2, dev->m_nRange);

	put32va (PWM_CTL,   ARM_PWM_CTL_PWEN1 | ARM_PWM_CTL_USEF1
			  | ARM_PWM_CTL_PWEN2 | ARM_PWM_CTL_USEF2
			  | ARM_PWM_CTL_CLRF1);
	ms_delay(2);
}

// CPWMSoundBaseDevice::StopPWM
static void StopPWM (struct sound_dev *dev)
{
	PeripheralEntry ();

	write32 (PWM_DMAC, 0);
	write32 (PWM_CTL, 0);			// disable PWM channel 0 and 1
	ms_delay (2);

	gpioclock_stop(&dev->m_Clock);
	ms_delay (2);

	PeripheralExit ();
}

// queue mgmt ... from pBuffer, copy nCount bytes to the queue (drv ring buf)
// 1. caller must hold spinlock
// 2. caller must ensure nCount won't make queue full/empty 
// CSoundBaseDevice::Enqueue
static void Enqueue (struct sound_drv *drv, const void *pBuffer, unsigned nCount)
{
	const u8 *p = (const u8 *) (pBuffer);
	assert (p != 0);
	assert (drv->m_pQueue != 0);

	assert (nCount > 0);
	while (nCount-- > 0) {   // TODO: optimize for memcpy w/ fewer checks
		drv->m_pQueue[drv->m_nInPtr] = *p++;
		if (++drv->m_nInPtr == drv->m_nQueueSize)
			drv->m_nInPtr = 0;      
	}
}

// 1. caller must hold spinlock
// 2. caller must ensure nCount won't make queue full/empty 
static void EnqueueFromUser (struct sound_drv *drv, uint64 src, unsigned nCount)
{
	assert (drv->m_pQueue != 0);
	assert (nCount > 0);

    unsigned len = MIN(nCount, drv->m_nQueueSize - drv->m_nInPtr); 
    if (either_copyin(drv->m_pQueue + drv->m_nInPtr, 1, src, len) == -1)
        BUG(); 
    drv->m_nInPtr = (drv->m_nInPtr+len) % drv->m_nQueueSize; nCount -= len; 
    assert(nCount >= 0);
    if (!nCount) return; 
    assert(!drv->m_nInPtr);
    if (either_copyin(drv->m_pQueue + drv->m_nInPtr, 1, src, len) == -1)
        BUG(); 
    drv->m_nInPtr += len; 	
}

// CSoundBaseDevice::Dequeue
static void Dequeue (struct sound_drv *drv, void *pBuffer, unsigned nCount) {
	u8 *p = (u8 *) (pBuffer);
	assert (p != 0);
	assert (drv->m_pQueue != 0);

	assert (nCount > 0);
	while (nCount-- > 0) { // TODO: optimize for memcpy w/ fewer checks
		*p++ = drv->m_pQueue[drv->m_nOutPtr];

		if (++drv->m_nOutPtr == drv->m_nQueueSize)
			drv->m_nOutPtr = 0;
	}
}

// alloc streaming buf by playback time
// CSoundBaseDevice::AllocateQueue
static boolean AllocateQueue (struct sound_drv *drv, unsigned nSizeMsecs)	
{
	assert (drv->m_pQueue == 0);
	assert (1 <= nSizeMsecs && nSizeMsecs <= 1000);

	// 1 byte remains free
	drv->m_nQueueSize = 
        (drv->m_nHWFrameSize*drv->m_nSampleRate*nSizeMsecs + 999) / 1000 + 1;

	drv->m_pQueue = malloc(drv->m_nQueueSize);
	if (drv->m_pQueue == 0)
		return FALSE;

	drv->m_nNeedDataThreshold = drv->m_nQueueSize / 2;
	return TRUE;
}

// caller must hold spinlock
// returns # of bytes that can be dequeued
static unsigned GetQueueBytesAvail (struct sound_drv *drv) {
	assert (drv->m_nQueueSize > 1);
	assert (drv->m_nInPtr < drv->m_nQueueSize);
	assert (drv->m_nOutPtr < drv->m_nQueueSize);

	if (drv->m_nInPtr < drv->m_nOutPtr)
		return drv->m_nQueueSize+drv->m_nInPtr-drv->m_nOutPtr;
	return drv->m_nInPtr-drv->m_nOutPtr;
}

// caller must hold spinlock
// return # of bytes that can be enqueued
//  i.e when queue is empty, inptr==outptr, bytesfree = queuesize - 1
// CSoundBaseDevice::GetQueueBytesFree
static unsigned GetQueueBytesFree (struct sound_drv *drv) {
	assert (drv->m_nQueueSize > 1);
	assert (drv->m_nInPtr < drv->m_nQueueSize);
	assert (drv->m_nOutPtr < drv->m_nQueueSize);

	if (drv->m_nOutPtr <= drv->m_nInPtr)
		return drv->m_nQueueSize + drv->m_nOutPtr - drv->m_nInPtr - 1;

	return drv->m_nOutPtr - drv->m_nInPtr - 1;
}

#if 0 // not used?
// CSoundBaseDevice::GetQueueFramesAvail
static unsigned GetQueueFramesAvail (struct sound_drv *drv) {
	assert (drv->m_nQueueSize > 0);

    acquire(&drv->m_SpinLock);
	unsigned nQueueBytesAvail = GetQueueBytesAvail(drv);
    release(&drv->m_SpinLock);

	return nQueueBytesAvail / drv->m_nHWFrameSize;
}
#endif
// dequeue a frame chunk from drv's ring buffer to device driver....
//  wakeup users if remaining chunks below certain threshold
//  return # of bytes (always == nChunkSize?? 
// if sound samples less than a chunk, pad with null frames
// CSoundBaseDevice::GetChunkInternal
static unsigned GetChunkFromQueue (struct sound_drv *drv, 
    void *pBuffer, unsigned nChunkSize) {
	u8 *pBuffer8 = (u8 *)(pBuffer);
	assert (pBuffer8 != 0);

	assert (nChunkSize > 0);
	assert (nChunkSize % SOUND_HW_CHANNELS == 0);
	unsigned nChunkSizeBytes = nChunkSize * drv->m_nHWSampleSize;

	acquire(&drv->m_SpinLock);

	unsigned nQueueBytesAvail = GetQueueBytesAvail (drv);
	unsigned nBytes = nQueueBytesAvail; // total bytes to return
	if (nBytes > nChunkSizeBytes)
		nBytes = nChunkSizeBytes;

	if (nBytes > 0) {
		Dequeue (drv, pBuffer8, nBytes);
		pBuffer8 += nBytes;
		nQueueBytesAvail -= nBytes;
	}

	// release(&drv->m_SpinLock);

	while (nBytes < nChunkSizeBytes) {  // chunk stil has room, pad w/ null frames
		memcpy (pBuffer8, drv->m_NullFrame, drv->m_nHWFrameSize);
		pBuffer8 += drv->m_nHWFrameSize;
		nBytes += drv->m_nHWFrameSize;
	}

    if (nQueueBytesAvail < drv->m_nNeedDataThreshold)
        wakeup(&drv->m_nNeedDataThreshold); 

    release(&drv->m_SpinLock); // wakeup with lock held

	return nChunkSize;
}

// a simple version of data load -- just load chunk from m_pSoundData (preloaded)
// until exhausted
// does data conversion. one shot playback.
//      convert u8 or s16 to u32 (hw native) 
// cf CPWMSoundDevice::GetChunk
static unsigned GetChunkPreloaded(struct sound_dev *dev, u32 *pBuffer, unsigned nChunkSize) {
    assert(pBuffer != 0);
    assert(nChunkSize > 0);
    assert((nChunkSize & 1) == 0);

    unsigned nResult = 0;

    if (dev->m_nSamples == 0) {
        return nResult;
    }

    assert(dev->m_pSoundData != 0);
    assert(dev->m_nChannels == 1 || dev->m_nChannels == 2);
    assert(dev->m_nBitsPerSample == 8 || dev->m_nBitsPerSample == 16);

    for (unsigned nSample = 0; nSample < nChunkSize / 2;) // 2 channels on output
    {
        unsigned nValue = *dev->m_pSoundData++;
        if (dev->m_nBitsPerSample > 8) {
            nValue |= (unsigned)*dev->m_pSoundData++ << 8;
            nValue = (nValue + 0x8000) & 0xFFFF; // signed -> unsigned (16 bit)
        }

        if (dev->m_nBitsPerSample >= dev->m_nRangeBits) {
            nValue >>= dev->m_nBitsPerSample - dev->m_nRangeBits;
        } else {
            nValue <<= dev->m_nRangeBits - dev->m_nBitsPerSample;
        }

        unsigned nValue2 = nValue;
        if (dev->m_nChannels == 2) {
            nValue2 = *dev->m_pSoundData++;
            if (dev->m_nBitsPerSample > 8) {
                nValue2 |= (unsigned)*dev->m_pSoundData++ << 8;
                nValue2 = (nValue2 + 0x8000) & 0xFFFF; // signed -> unsigned (16 bit)
            }

            if (dev->m_nBitsPerSample >= dev->m_nRangeBits) {
                nValue2 >>= dev->m_nBitsPerSample - dev->m_nRangeBits;
            } else {
                nValue2 <<= dev->m_nRangeBits - dev->m_nBitsPerSample;
            }
        }

        if (!dev->m_bChannelsSwapped) {
            pBuffer[nSample++] = nValue;
            pBuffer[nSample++] = nValue2;
        } else {
            pBuffer[nSample++] = nValue2;
            pBuffer[nSample++] = nValue;
        }

        nResult += 2;

        if (--dev->m_nSamples == 0) {
            break;
        }
    }

    return nResult;
}

// prep data for next xfer: load to dev's dma buffer, flush cache, flip buffer ptr
//      called from irq 
// caller must hold dev m_SpinLock
// CPWMSoundBaseDevice::GetNextChunk
static boolean GetNextChunk(struct sound_dev *dev) {
    unsigned int next = dev->m_nNextBuffer; 

    assert(dev->m_pDMABuffer[next] != 0);
    unsigned nChunkSize; 
    if (dev->m_pSoundData) // we're in one-shot playback
        nChunkSize = GetChunkPreloaded(dev, dev->m_pDMABuffer[next], dev->m_nChunkSize); 
    else { // we're in streaming play
        struct sound_drv *drv = container_of(dev, struct sound_drv, dev);
        nChunkSize = GetChunkFromQueue(drv, dev->m_pDMABuffer[next], dev->m_nChunkSize); 
    }

    V("GetNextChunk: nChunkSize %u", nChunkSize); 

    if (nChunkSize == 0)
        return FALSE;

    unsigned nTransferLength = nChunkSize * sizeof(u32);
    assert(nTransferLength <= TXFR_LEN_MAX_LITE);

    assert(dev->m_pControlBlock[next] != 0);
    dev->m_pControlBlock[next]->nTransferLength = nTransferLength;

    V("GetNextChunk: nChunkSize %u nTransferLength %u", nChunkSize, nTransferLength); 

    // fxl: not flush, but invalidate?? TODO
    __asm_invalidate_dcache_range(dev->m_pDMABuffer[next], 
        (char *)dev->m_pDMABuffer[next] + nTransferLength - 1);
    __asm_invalidate_dcache_range(dev->m_pControlBlock[next],
        (char *)dev->m_pControlBlock[next] + sizeof(struct TDMAControlBlock)-1);

    dev->m_nNextBuffer ^= 1;

    return TRUE;
}

// CSoundBaseDevice::SetWriteFormat
static void SetWriteFormat(struct sound_drv *drv, TSoundFormat Format,
                           unsigned nChannels) {
    assert(Format < SoundFormatUnknown);
    drv->m_WriteFormat = Format;

    assert(1 <= nChannels && nChannels <= 2);
    drv->m_nWriteChannels = nChannels;

    switch (drv->m_WriteFormat) {
    case SoundFormatUnsigned8:
        drv->m_nWriteSampleSize = sizeof(u8);
        break;

    case SoundFormatSigned16:
        drv->m_nWriteSampleSize = sizeof(s16);
        break;

    case SoundFormatSigned24:
        drv->m_nWriteSampleSize = sizeof(u8) * 3;
        break;

    default:
        assert(0);
        break;
    }
    drv->m_nWriteFrameSize = drv->m_nWriteChannels * drv->m_nWriteSampleSize;
}

#if 0 // not used?
// CSoundBaseDevice::GetQueueSizeFrames
static unsigned GetQueueSizeFrames (struct sound_drv *drv) {
	assert (drv->m_nQueueSize > 0);
	return drv->m_nQueueSize / drv->m_nHWFrameSize;
}
#endif

static void dump_dmachan_status(int ch) {
    u32 reg = read32(ARM_DMACHAN_CS (ch));
    printf("%s: CS_ERROR %d CS_WAITING_FOR_OUTSTANDING_WRITES %d \n"
    "CS_DREQ_STOPS_DMA %d CS_PAUSED %d CS_INT %d CS_END %d CS_ACTIVE %d\n",
    __func__, !!(reg & CS_ERROR), !!(reg & CS_WAITING_FOR_OUTSTANDING_WRITES), 
    !!(reg & CS_DREQ_STOPS_DMA), !!(reg & CS_PAUSED), !!(reg & CS_INT), 
    !!(reg & CS_END), reg & CS_ACTIVE);
}

static void DoDMAIRQ(void *para) {
    struct sound_dev *dev = (struct sound_dev *)para; 

    V("DoDMAIRQ");

    // check channel 
	assert (dev && dev->m_State != PWMSoundIdle);
	assert (dev->m_nDMAChannel <= DMA_CHANNEL_MAX);

	PeripheralEntry ();

	u32 nIntStatus = read32 (ARM_DMA_INT_STATUS);
	u32 nIntMask = 1 << dev->m_nDMAChannel;

    if (!(nIntStatus & nIntMask)) {
        W("nIntStatus %x nIntMask %x ARM_DMACHAN_CS %x", nIntStatus, nIntMask, 
        read32 (ARM_DMACHAN_CS (dev->m_nDMAChannel)));
        dump_dmachan_status(dev->m_nDMAChannel); 
        // fxl: this could happen, why? just do nothing as of now?
        PeripheralExit ();
        return; 
    }

	assert (nIntStatus & nIntMask);     // fxl: this assertion could fail, why?
	write32 (ARM_DMA_INT_STATUS, nIntMask);

	u32 nCS = read32 (ARM_DMACHAN_CS (dev->m_nDMAChannel));
	assert (nCS & CS_INT);  // fxl: this assertion could fail, why?
	write32 (ARM_DMACHAN_CS (dev->m_nDMAChannel), nCS);	// reset CS_INT

	PeripheralExit ();

    if (nCS & CS_ERROR) {
        dev->m_State = PWMSoundError;
        return;
    }

    acquire(&dev->m_SpinLock); 

	switch (dev->m_State)
	{
	case PWMSoundRunning:
		if (GetNextChunk (dev))
		{
			break;
		}
		// fall through  (fail to get chunk -- then cancel). 

	case PWMSoundCancelled:
		PeripheralEntry ();
		write32 (ARM_DMACHAN_NEXTCONBK (dev->m_nDMAChannel), 0);
		PeripheralExit ();

		// avoid clicks
		PeripheralEntry ();
		write32 (PWM_CTL, read32 (PWM_CTL) | ARM_PWM_CTL_RPTL1 | ARM_PWM_CTL_RPTL2);
		PeripheralExit ();

        dev->m_pSoundData = 0;  
		dev->m_State = PWMSoundTerminating;
		break;

	case PWMSoundTerminating:
		dev->m_State = PWMSoundIdle;
		break;

	default:
		assert (0);
		break;
	}
	release(&dev->m_SpinLock); 
}

// CSoundBaseDevice::ConvertSoundFormat
// fxl: convert one sample at a time? this feels really slow. 
//  fortunatley only on slow path (dont fully understand the func though
static void ConvertSoundFormat(struct sound_drv *drv, void *pTo, 
    const void *pFrom) {
    s32 nValue = 0;

    switch (drv->m_WriteFormat) {
    case SoundFormatUnsigned8: {
        const u8 *pValue = (const u8 *)(pFrom);
        nValue = *pValue;
        nValue -= 128;
        nValue <<= 24;
    } break;

    case SoundFormatSigned16: {
        const s16 *pValue = (const s16 *)(pFrom);
        nValue = *pValue;
        nValue <<= 16;
    } break;

    case SoundFormatSigned24: {
        const u32 *pValue = (const u32 *)(pFrom);
        nValue = *pValue & 0xFFFFFF;
        nValue <<= 8;
    } break;

    case SoundFormatUnknown:
        memcpy(pTo, pFrom, drv->m_nWriteSampleSize);
        return;

    default:
        assert(0);
        break;
    }

    switch (drv->m_HWFormat) {
    case SoundFormatSigned16: {
        s16 *pValue = (s16 *)(pTo);
        *pValue = nValue >> 16;
    } break;

    case SoundFormatSigned24: {
        s32 *pValue = (s32 *)(pTo);
        *pValue = nValue >> 8;
    } break;

    case SoundFormatUnsigned32: {
        s64 llValue = (s64)nValue;
        llValue += 1U << 31;
        llValue *= drv->m_nRangeMax;
        llValue >>= 32;

        u32 *pValue = (u32 *)(pTo);
        *pValue = (u32)llValue;
    } break;

    default:
        assert(0);
        break;
    }
}

// fxl: copy to ringbuf (w format conversion, etc)
// CSoundBaseDevice::Write
// int Write(struct sound_drv *drv, const void *pBuffer, size_t nCount) {
//     assert(m_WriteFormat < SoundFormatUnknown);

//     assert(pBuffer != 0);
//     const u8 *pBuffer8 = static_cast<const u8 *>(pBuffer);

//     int nResult = 0;

//     m_SpinLock.Acquire();

//     if (m_HWFormat == m_WriteFormat && m_nWriteChannels == SOUND_HW_CHANNELS && !m_bSwapChannels) {
//         // fast path for Stereo samples without bit depth conversion or channel swapping

//         unsigned nBytes = GetQueueBytesFree();
//         if (nBytes > nCount) {
//             nBytes = nCount;
//         }
//         nBytes &= ~(m_nWriteFrameSize - 1); // must be a multiple of frame size

//         if (nBytes > 0) {
//             Enqueue(pBuffer, nBytes);

//             nResult = nBytes;
//         }
//     } else {
//         while (nCount >= m_nWriteFrameSize && GetQueueBytesFree() >= m_nHWFrameSize) {
//             u8 Frame[SOUND_MAX_FRAME_SIZE];

//             if (!m_bSwapChannels) {
//                 ConvertSoundFormat(Frame, pBuffer8);
//                 pBuffer8 += m_nWriteSampleSize;

//                 if (m_nWriteChannels == 2) {
//                     ConvertSoundFormat(Frame + m_nHWSampleSize, pBuffer8);
//                     pBuffer8 += m_nWriteSampleSize;
//                 } else {
//                     memcpy(Frame + m_nHWSampleSize, Frame, m_nHWSampleSize);
//                 }
//             } else {
//                 ConvertSoundFormat(Frame + m_nHWSampleSize, pBuffer8);
//                 pBuffer8 += m_nWriteSampleSize;

//                 if (m_nWriteChannels == 2) {
//                     ConvertSoundFormat(Frame, pBuffer8);
//                     pBuffer8 += m_nWriteSampleSize;
//                 } else {
//                     memcpy(Frame, Frame + m_nHWSampleSize, m_nHWSampleSize);
//                 }
//             }

//             Enqueue(Frame, m_nHWFrameSize);

//             nCount -= m_nWriteFrameSize;
//             nResult += m_nWriteFrameSize;
//         }
//     }

//     m_SpinLock.Release();

//     return nResult;
// }

// write to drv ring buf. does NOT automatically start the device (or program the hw). 
// return # of bytes actually written (enqueued). -1 on error
// if there's some queue space, caller will write whatever is allowed and return
// if there's 0 queue space, caller will sleep 
// Circle CSoundBaseDevice::Write
int sound_write(struct sound_drv *drv, uint64 src, size_t nCount) {
    // W("drv->m_WriteFormat %d", drv->m_WriteFormat); 
    assert(drv->m_WriteFormat < SoundFormatUnknown);
    
    int nResult = 0;
    unsigned nBytes; 

    acquire(&drv->m_SpinLock);

    while ((nBytes = GetQueueBytesFree(drv)) == 0) // sleep until queue has room
        sleep(&drv->m_nNeedDataThreshold, &drv->m_SpinLock);

    // fxl: but said "writeformat" cannot be u32? (only u8 s16)
    if (drv->m_HWFormat == drv->m_WriteFormat && 
        drv->m_nWriteChannels == SOUND_HW_CHANNELS && !drv->m_bSwapChannels) {
        // fast path for Stereo samples without bit depth conversion or channel swapping
        if (nBytes > nCount) {
            nBytes = nCount;
        }
        nBytes &= ~(drv->m_nWriteFrameSize - 1); // must be a multiple of frame size

        if (nBytes > 0) {
            EnqueueFromUser(drv, src, nBytes);
            nResult = nBytes;
        }
    } else { // convert one frame at a time... a cold path. right now we copy 
        // the samples twice. TODO: optimize (need overhaul ConvertSoundFormat)
        u8 *p, *pBuffer8 = malloc(nCount); assert(pBuffer8); 
        if (either_copyin(pBuffer8, 1, src, nCount) == -1)
            {free(pBuffer8); goto out;} 
        p = pBuffer8; 
        while (nCount >= drv->m_nWriteFrameSize 
            && GetQueueBytesFree(drv) >= drv->m_nHWFrameSize) {
            u8 Frame[SOUND_MAX_FRAME_SIZE];
            if (!drv->m_bSwapChannels) {
                ConvertSoundFormat(drv, Frame, p);
                p += drv->m_nWriteSampleSize;

                if (drv->m_nWriteChannels == 2) { // convert channel 2...
                    ConvertSoundFormat(drv, Frame + drv->m_nHWSampleSize, p);
                    p += drv->m_nWriteSampleSize;
                } else { // fxl: only 1 ch, duplicate ch 1
                    memcpy(Frame + drv->m_nHWSampleSize, Frame, drv->m_nHWSampleSize);
                }
            } else { // ditto
                ConvertSoundFormat(drv, Frame + drv->m_nHWSampleSize, p);
                p += drv->m_nWriteSampleSize;

                if (drv->m_nWriteChannels == 2) {
                    ConvertSoundFormat(drv, Frame, p);
                    p += drv->m_nWriteSampleSize;
                } else {
                    memcpy(Frame, Frame + drv->m_nHWSampleSize, drv->m_nHWSampleSize);
                }
            }
            Enqueue(drv, Frame, drv->m_nHWFrameSize);
            nCount -= drv->m_nWriteFrameSize;
            nResult += drv->m_nWriteFrameSize;
        }
        free(pBuffer8);        
    }

out: 
    release(&drv->m_SpinLock);

    return nResult;
}

// CPWMSoundBaseDevice::Start
int sound_start(struct sound_drv *drv) {

    struct sound_dev *dev = &(drv->dev); 
    assert(dev->m_State == PWMSoundIdle);

    acquire(&dev->m_SpinLock); 

    // fill buffer 0
    dev->m_nNextBuffer = 0;

    if (!GetNextChunk(dev)) {
        release(&dev->m_SpinLock); 
        return 0;
    }

    dev->m_State = PWMSoundRunning;

    // connect IRQ
    if (!dma_irq) { dma_irq = DoDMAIRQ; dma_irq_param = dev; }

    assert(dev->m_nDMAChannel <= DMA_CHANNEL_MAX);

    // enable PWM DMA operation
    PeripheralEntry();
	write32 (PWM_DMAC,   ARM_PWM_DMAC_ENAB
			   | (7 << ARM_PWM_DMAC_PANIC__SHIFT)
			   | (7 << ARM_PWM_DMAC_DREQ__SHIFT));

    // switched this on when playback stops to avoid clicks, switch it off here
    write32(PWM_CTL, read32(PWM_CTL) & ~(ARM_PWM_CTL_RPTL1 | ARM_PWM_CTL_RPTL2));

    PeripheralExit();

    // start DMA
    PeripheralEntry();

    assert(!(read32(ARM_DMACHAN_CS(dev->m_nDMAChannel)) & CS_INT));
    assert(!(read32(ARM_DMA_INT_STATUS) & (1 << dev->m_nDMAChannel)));

    // write dma control blk0 addr to reg 
    assert(dev->m_pControlBlock[0] != 0);
    write32(ARM_DMACHAN_CONBLK_AD(dev->m_nDMAChannel), 
        BUS_ADDRESS((uintptr)dev->m_pControlBlock[0]));

    // kick off dma 
    I("kick dma ch %u", dev->m_nDMAChannel); 

	write32 (ARM_DMACHAN_CS (dev->m_nDMAChannel),   CS_WAIT_FOR_OUTSTANDING_WRITES
					         | (DEFAULT_PANIC_PRIORITY << CS_PANIC_PRIORITY_SHIFT)
					         | (DEFAULT_PRIORITY << CS_PRIORITY_SHIFT)
					         | CS_ACTIVE);

    PeripheralExit();

    // fill buffer 1
    if (!GetNextChunk(dev)) {   // bail out...
        // acquire(&dev->m_SpinLock); 

        if (dev->m_State == PWMSoundRunning) {
            PeripheralEntry();
            write32(ARM_DMACHAN_NEXTCONBK(dev->m_nDMAChannel), 0);
            PeripheralExit();

            dev->m_State = PWMSoundTerminating;
        }

        // release(&dev->m_SpinLock); 
    }
    release(&dev->m_SpinLock); 
    return 1;
}

static int IsActive (struct sound_dev *dev) {
	return dev->m_State != PWMSoundIdle ? 1 : 0;
}

// one shot playback. load samples, .. then start() DMAs...
// can be used for in-kernel testing of sound device
// CPWMSoundDevice::Playback
void sound_playback (struct sound_drv *drv, 
            void	*pSoundData,		// sample rate 44100 Hz
            unsigned  nSamples,		// for Stereo the L/R samples are count as one
            unsigned  nChannels,		// 1 (Mono) or 2 (Stereo)
            unsigned  nBitsPerSample) {	// 8 (unsigned sound data) or 16 (signed sound data)

    struct sound_dev *dev =  &drv->dev; 
    assert(!IsActive(dev)); 
	assert (pSoundData != 0);
	assert (nChannels == 1 || nChannels == 2);
	assert (nBitsPerSample == 8 || nBitsPerSample == 16);

	dev->m_pSoundData	 = (u8 *) pSoundData;
	dev->m_nSamples	 = nSamples;
	dev->m_nChannels	 = nChannels;
	dev->m_nBitsPerSample = nBitsPerSample;

    int ret = sound_start(drv); 
    W("sound_playback returns %d", ret); 
    W("ARM_DMA_ENABLE %x PWM_DMAC %x", 
        get32va(ARM_DMA_ENABLE),
        get32va(PWM_DMAC));
    // enable_irq();    // fixed
}

// CPWMSoundDevice::PlaybackActive
int sound_playback_active(struct sound_drv *drv) {
    return IsActive(&drv->dev); 
}

//CPWMSoundBaseDevice::Cancel
void sound_cancel(struct sound_drv *drv)
{
    struct sound_dev *dev = &(drv->dev);
	acquire(&dev->m_SpinLock);
    if (dev->m_State == PWMSoundRunning) {
        dev->m_State = PWMSoundCancelled;
    }
    release(&dev->m_SpinLock); 
}

#define MAX_SOUND_DRV 1
static int next_free = 0; 
static struct sound_drv drvs[MAX_SOUND_DRV]; 

// cal range, frame size, format etc.
// CSoundBaseDevice::CSoundBaseDevice()
static void sound_drv_init(struct sound_drv *this, TSoundFormat HWFormat,
                           u32 nRange32, unsigned nSampleRate, boolean bSwapChannels) {

    initlock(&this->m_SpinLock, "soundrv");
    this->valid = 1; 
    this->m_HWFormat = HWFormat;
    this->m_nSampleRate = nSampleRate;
    this->m_bSwapChannels = bSwapChannels;
    this->m_nQueueSize = 0;
    this->m_nNeedDataThreshold = 0;
    this->m_WriteFormat = SoundFormatUnknown;
    this->m_nWriteChannels = 0;
    this->m_pQueue = 0;
    this->m_nInPtr = 0;
    this->m_nOutPtr = 0;
    // this->m_pCallback = 0;

    memset(this->m_NullFrame, 0, sizeof this->m_NullFrame);

    switch (this->m_HWFormat) {
    case SoundFormatSigned16:
        this->m_nHWSampleSize = sizeof(s16);
        this->m_nRangeMin = -32768;
        this->m_nRangeMax = 32767;
        break;

    case SoundFormatSigned24:
        this->m_nHWSampleSize = sizeof(u32);
        this->m_nRangeMin = -(1 << 23) + 1;
        this->m_nRangeMax = (1 << 23) - 1;
        break;

    case SoundFormatUnsigned32: {
        this->m_nHWSampleSize = sizeof(u32);
        this->m_nRangeMin = 0;
        this->m_nRangeMax = (int)(nRange32 - 1);
        assert(this->m_nRangeMax > 0);

        u32 *pSample32 = (u32 *)(this->m_NullFrame);
        pSample32[0] = pSample32[1] = this->m_nRangeMax / 2;
    } break;

    default:
        assert(0);
        break;
    }
    this->m_nHWFrameSize = SOUND_HW_CHANNELS * this->m_nHWSampleSize;
}

// CSoundBaseDevice::~CSoundBaseDevice
static void sound_drv_fini(struct sound_drv *this) {
    acquire(&this->m_SpinLock);
    // this->m_pCallback = 0; 
    free(this->m_pQueue); 
    this->m_pQueue = 0; 
    this->valid = 0; 
    release(&this->m_SpinLock);
}   

// Playback works with 44100 Hz, Stereo only.
// Other sound formats will be converted on the fly.
#define SAMPLE_RATE		44100

// nChunkSize=0 means using default
struct sound_drv * sound_init(unsigned nChunkSize)
{   
    struct sound_drv *drv = 0; 
    unsigned nSampleRate = SAMPLE_RATE; 

    if (!nChunkSize) nChunkSize = 2048; 

    // XXX: improve
    int id = __atomic_fetch_add(&next_free, 1, __ATOMIC_SEQ_CST); 
    if (id >= MAX_SOUND_DRV)    
        return 0; 

    drv = drvs + id; V("id = %d", id); 
    
    // CSoundBaseDevice::CSoundBaseDevice()
    sound_drv_init(drv, SoundFormatUnsigned32,
			  (CLOCK_RATE + nSampleRate/2) / nSampleRate, nSampleRate, 
              CMachineInfo_ArePWMChannelsSwapped()); 

    // CPWMSoundBaseDevice::CPWMSoundBaseDevice
    struct sound_dev *dev = &drv->dev; 
    initlock(&dev->m_SpinLock, "soundev");

	dev->m_nChunkSize = (nChunkSize);
	dev->m_nRange = ((CLOCK_RATE + nSampleRate/2) / nSampleRate);

    // CPWMSoundDevice::CPWMSoundDevice
    dev->m_nRangeBits = 0; 
    dev->m_bChannelsSwapped = drv->m_bSwapChannels;
    dev->m_pSoundData = 0; 
	// assert (GetRangeMin () == 0);
    for (unsigned nBits = 2; nBits < 16; nBits++) {
        if ((dev->m_nRange-1) < (1 << nBits)) {
            dev->m_nRangeBits = nBits - 1;
            break;
        }
    }
    assert(dev->m_nRangeBits > 0);

    // Ensure PWMs is mapped to GPIO 40/41  Circle CMachineInfo::GetGPIOPin()

    /* 
        Notes about GPIO pins and PWM channels on Raspberry Pi 3:
        Nov 2024
        
        The Raspberry Pi 3 has two PWM channels, each can be routed to a set of
        GPIO pins; one channel can only be routed to multiple pins at a time
        (verified).

        For the Raspberry Pi 3, GPIO pins 12, 18, 40, and 52 can output PWM0,
        while GPIO pins 13, 19, 41, and 45 can output PWM1.    

        Some of these pins are on 40-pin headers; some of them are not. Among
        these "PWM"-capable pins, GPIO 40 and 41 are routed to a level-shifter,
        then filter circuitry, then to the audio jack; so these signals are
        specifically "tuned" for the audio jack. Accordingly, they are NOT
        exposed on 40-pin headers, and I don't know an easy way to access them
        (maybe they are not supposed to be).

        For the Waveshare game hat, audio from the HDMI signal is extracted by a
        dedicated chip, then amplified and sent to the two speakers. We don't
        support HDMI audio, the driver seems too complicated. But we wanted to
        reuse the speakers on the game hat, multiple things I've tried: 

        Disconnect existing input (RIN, LIN) to the game hat amplifier
        (APA2068), by desoldering inline capacitors. 

        (1) Solder wires from the 3.5mm audio jack from the rpi3 (there are test
        pads on the back of the board, easy) to the input of the game hat
        amplifier (APA2068). DID NOT WORK: speakers only have very dim sound
        because the signal from the audio jack is likely much lower voltage
        (~1V?) than the amplifier expected input (5V). 

        (2) Solder wires from GPIO12/13 (on 40 pin headers) to the input of the
        game hat amplifier. Then in software, configure PWM output to be on
        these pins (in addition to GPIO 40/41).

            gpio_useAsAlt0(12);     // PWM0
            gpio_useAsAlt0(13);     // PWM1

        KINDA WORKED: loud sound, but noisy. (Have to plug in HDMI cable,
        otherwise the HDMI chip seems to disable/mute the amplifier). Likely
        cause: the output from PWM must be filtered (RC filter). AN EVEN BIGGER
        problem: if the RC filter is wired, the GPIO12/13 can no longer be used
        as input. And, GPIO12/13 are indeed connected to GAMEHAT buttons.

        (3) TBD: add a small driver board which has a 3.5mm input jack.
        Disconnect (or disable) the onboard amplifier. Solder wires from the
        driver board outputs to the onboard speakers. Use a 3.5mm cable to
        connect the rpi3 audio output and the driver board.
    */
        
    gpio_useAsAlt0(40);     // PWM0
    gpio_useAsAlt0(41);     // PWM1

    ms_delay(2); 
    gpioclock_init(&dev->m_Clock, GPIOClockPWM); 
	dev->m_State = PWMSoundIdle;
	dev->m_nDMAChannel = CMachineInfo_AllocateDMAChannel(DMA_CHANNEL_LITE);

	assert (dev->m_nChunkSize > 0);
	assert ((dev->m_nChunkSize & 1) == 0);

    // setup and concatenate DMA buffers and control blocks
    setup_dma_control_block(dev, 0); 
    setup_dma_control_block(dev, 1);
	dev->m_pControlBlock[0]->nNextControlBlockAddress = 
        BUS_ADDRESS ((uintptr) dev->m_pControlBlock[1]);
	dev->m_pControlBlock[1]->nNextControlBlockAddress = 
        BUS_ADDRESS ((uintptr) dev->m_pControlBlock[0]);

    // fxl: needed???
    __asm_flush_dcache_range(dev->m_pControlBlock[0], 
        (char *)dev->m_pControlBlock[0] + sizeof (struct TDMAControlBlock) + 30); 
    __asm_flush_dcache_range(dev->m_pControlBlock[1], 
        (char *)dev->m_pControlBlock[1] + sizeof (struct TDMAControlBlock) + 30);

    // start clock and PWM device
    RunPWM(dev); 

	// enable and reset DMA channel
	PeripheralEntry ();

	assert (dev->m_nDMAChannel <= DMA_CHANNEL_MAX);
	write32 (ARM_DMA_ENABLE, read32 (ARM_DMA_ENABLE) | (1 << dev->m_nDMAChannel));
	ms_delay(1);

	write32 (ARM_DMACHAN_CS (dev->m_nDMAChannel), CS_RESET);
	while (read32 (ARM_DMACHAN_CS (dev->m_nDMAChannel)) & CS_RESET)
		; // do nothing

	PeripheralExit ();

    // configuration, cf Circle 34-sounddevices.cpp
    if (!AllocateQueue(drv, 100 /*ms*/)) { BUG(); sound_fini(drv); return 0; }

    // can be changed later via /proc/sbctl
    //  hw native: SoundFormatUnsigned32, 2 channels. 
    SetWriteFormat(drv, SoundFormatUnsigned8, 1 /*chs*/); 

    // init devfs
extern int devsb_write(int user_src, uint64 src, int off, int n, void *content); // sound.c
    devsw[DEVSB].read = 0; 
    devsw[DEVSB].write = devsb_write; 

    I("audio init ok"); 

    return drv; 
}

// CPWMSoundBaseDevice::~CPWMSoundBaseDevice
void sound_fini(struct sound_drv *drv) {
    struct sound_dev *dev = &drv->dev; 

    W("sound fini"); 

    acquire(&dev->m_SpinLock); 
	if (dev->m_State != PWMSoundIdle) {
        E("dev->m_State %d, should be %d", dev->m_State, PWMSoundIdle);
    }

	// stop PWM device and clock
	StopPWM (dev);

	// reset and disable DMA channel
	PeripheralEntry ();

	assert (dev->m_nDMAChannel <= DMA_CHANNEL_MAX);

	write32 (ARM_DMACHAN_CS (dev->m_nDMAChannel), CS_RESET);
	while (read32 (ARM_DMACHAN_CS (dev->m_nDMAChannel)) & CS_RESET)
		; // do nothing

	write32 (ARM_DMA_ENABLE, read32 (ARM_DMA_ENABLE) & ~(1 << dev->m_nDMAChannel));

	PeripheralExit ();

	// disconnect IRQ
	dma_irq = 0; 

	// free DMA channel
	CMachineInfo_FreeDMAChannel (dev->m_nDMAChannel);

	// free buffers
	dev->m_pControlBlock[0] = 0;
	dev->m_pControlBlock[1] = 0;
	free(dev->m_pControlBlockBuffer[0]);
	dev->m_pControlBlockBuffer[0] = 0;
	free(dev->m_pControlBlockBuffer[1]);
	dev->m_pControlBlockBuffer[1] = 0;

	free(dev->m_pDMABuffer[0]);
	dev->m_pDMABuffer[0] = 0;
	free(dev->m_pDMABuffer[1]);
	dev->m_pDMABuffer[1] = 0;

    release(&dev->m_SpinLock);

    sound_drv_fini(drv); 

    __atomic_fetch_add(&next_free, -1, __ATOMIC_SEQ_CST); 
}

#define TXTSIZE  256 
// should works even if sound_init() not called yet
int procfs_sbctl_gen(char *txtbuf, int sz) {
    struct sound_drv *drv = 0; 
    char line[TXTSIZE]; int len, len1; char *p = txtbuf; 

    len = snprintf(line, TXTSIZE, 
        "# %6s %6s %6s %6s %6s %6s %s\n",
        "id","HwFmt","SplRate","QSize", "BytesFree", "WrFmt", "WrChans"); 
    if (len > sz) return 0; 
    memmove(p, line, len);
    p += len; 

    for (int i=0; i<MAX_SOUND_DRV; i++) {
        drv = drvs + i;
        if (!drv->valid) continue;   // w/o locking...(problem?
        acquire(&drv->m_SpinLock); 
        len = snprintf(line, TXTSIZE, "%6d %6d %6d %6d %6d %6d %6d\n",
            i, drv->m_HWFormat, drv->m_nSampleRate, drv->m_nQueueSize, 
                GetQueueBytesFree(drv), drv->m_WriteFormat, drv->m_nWriteChannels); 
        release(&drv->m_SpinLock);
        len1 = MIN(len, sz-(p-txtbuf));
        memmove(p, line, len1); p += len1; 
        if (p >= txtbuf) 
            break; 
    }
    return p-txtbuf; 
}

///// sound driver test 
#include "sound-sample.h"
#define SOUND_SAMPLES		(sizeof Sound / sizeof Sound[0] / SOUND_CHANNELS)

// cf Circle sample/12-pwmsound/kernel.cpp
void test_sound() {
    struct sound_drv *p = sound_init(0);
    sound_playback(p, Sound, SOUND_SAMPLES, SOUND_CHANNELS, SOUND_BITS);

// busy wait, may not work in syscall conext, as we may be holding spinlocks (
// e.g. ftable's spinlock which turned off irq already
#if 0 
	for (unsigned nCount = 0; sound_playback_active(p); nCount++)
        ;
    W("playback done"); 
    sound_fini(p);  // it's a test, we can skip this
#endif    
}

#include "fcntl.h"
// format: command [drvid]
// command: 0-fini, 1-init, 2-start, 3-cancel, 9-test
// unused/extra args will be ignored
// return 0 on error 
int procfs_parse_sbctl(int args[PROCFS_MAX_ARGS]) {  
    int id = -1; 
    int cmd = args[0]; 
    int ret = 0; 

    switch (cmd)
    {  // fxl: XXX refactor lock acquire/release below
    case SB_CMD_FINI: // arg1=devid
        id = args[1]; 
        if (id>=MAX_SOUND_DRV) return 0; 
        if (!drvs[id].valid) return 0;    
        // acquire(&(drvs+id)->m_SpinLock);    // fxl: TODO dont grab lock here. grab inside sound_fini. and only take dev lock
        I("sound fini drv %d", id);
        sound_fini(drvs+id); 
        // release(&(drvs+id)->m_SpinLock); 
        ret = 2; 
        break;
    case SB_CMD_INIT: // init
        if (args[1] >= 0) {
            I("sound init chunksize=%d", args[1]);
            sound_init(args[1]);
            ret = 1; 
        }
        break; 
    case SB_CMD_START: // arg1=devid
        id = args[1]; 
        if (id>=MAX_SOUND_DRV) return 0; 
        if (!drvs[id].valid) return 0;    
        // acquire(&(drvs+id)->m_SpinLock);  // fxl XXX shouldn't lock. as GetNextChunk will grab drv lock
        I("sound_start drv %d", id);
        sound_start(drvs+id);
        // release(&(drvs+id)->m_SpinLock); 
        ret = 2; 
        break; 
    case SB_CMD_CANCEL: // arg1=devid
        id = args[1]; 
        if (id>=MAX_SOUND_DRV) return 0; 
        if (!drvs[id].valid) return 0;
        // acquire(&(drvs+id)->m_SpinLock); // fxl XXX shouldn't lock.
        I("sound_cancel drv %d", id);
        sound_cancel(drvs+id);
        // release(&(drvs+id)->m_SpinLock); 
        ret = 2; 
        break; 
    case SB_CMD_WR_FMT: // arg1=devid, arg2=write fmt, arg3=channels
        id = args[1]; 
        if (id>=MAX_SOUND_DRV) return 0; 
        if (!drvs[id].valid) return 0;
        if (args[2]>=SoundFormatUnsigned32 || args[3]>2) return 0;         
        I("SetWriteFormat drv %d wrfmt %d nchans %d", id, args[2], args[3]);
        SetWriteFormat(drvs+id, args[2], args[3]); 
        ret = 2; 
        break;
    case SB_CMD_TEST:  // play sound samples built-in the kernel
        I("test sound");
        test_sound(); 
        break; 
    default:
        W("unknown cmd %d", cmd); 
        break;
    }

    return ret; 
}

int devsb_write(int user_src, uint64 src, int off, int n, void *content) {
    short minor = (short)(unsigned long)content; 
    if (minor >= MAX_SOUND_DRV) return -2;
    struct sound_drv *drv = drvs + minor; 
    if (!drv->valid) return -3; 

    return sound_write(drv, src, n); 
}