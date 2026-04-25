#include "circle-glue.h"

// Query machine related info
// Circle machineinfo.cpp. preserve the logic here for future rpi4 porting

enum TMachineModel
{
	MachineModelA,
	MachineModelBRelease1MB256,
	MachineModelBRelease2MB256,
	MachineModelBRelease2MB512,
	MachineModelAPlus,
	MachineModelBPlus,
	MachineModelZero,
	MachineModelZeroW,
	MachineModel2B,
	MachineModel3B,
	MachineModel3APlus,
	MachineModel3BPlus,
	MachineModelCM,
	MachineModelCM3,
	MachineModelCM3Plus,
	MachineModel4B,
	MachineModel400,
	MachineModelCM4,
	MachineModelUnknown
};

enum TSoCType
{
	SoCTypeBCM2835,
	SoCTypeBCM2836,
	SoCTypeBCM2837,
	SoCTypeBCM2711,
	SoCTypeUnknown
};


// channel bit set if channel is free
#if RASPPI <= 3
static unsigned short  m_usDMAChannelMap = 0x1F35;	// default mapping
#else
static unsigned short  m_usDMAChannelMap = 0x71F5;
m_usDMAChannelMap;		
#endif

// nChannel must be DMA_CHANNEL_NORMAL, _LITE, _EXTENDED or an explicit channel number
// returns the allocated channel number or DMA_CHANNEL_NONE on failure
unsigned CMachineInfo_AllocateDMAChannel(unsigned nChannel) {
    if (!(nChannel & ~DMA_CHANNEL__MASK)) {
        // explicit channel allocation
        assert(nChannel <= DMA_CHANNEL_MAX);
        if (m_usDMAChannelMap & (1 << nChannel)) {
            m_usDMAChannelMap &= ~(1 << nChannel);

            return nChannel;
        }
    } else {
        // arbitrary channel allocation
        int i = nChannel == DMA_CHANNEL_NORMAL ? 6 : DMA_CHANNEL_MAX;
        int nMin = 0;
#if RASPPI >= 4
        if (nChannel == DMA_CHANNEL_EXTENDED) {
            i = DMA_CHANNEL_EXT_MAX;
            nMin = DMA_CHANNEL_EXT_MIN;
        }
#endif
        for (; i >= nMin; i--) {
            if (m_usDMAChannelMap & (1 << i)) {
                m_usDMAChannelMap &= ~(1 << i);

                return (unsigned)i;
            }
        }
    }
    return DMA_CHANNEL_NONE;
}

void CMachineInfo_FreeDMAChannel (unsigned nChannel) {
	assert (nChannel <= DMA_CHANNEL_MAX);
	assert (!(m_usDMAChannelMap & (1 << nChannel)));
	m_usDMAChannelMap |= 1 << nChannel;
}

static enum TMachineModel m_MachineModel = MachineModel3B;

boolean CMachineInfo_ArePWMChannelsSwapped (void) {
	return    m_MachineModel >= MachineModelAPlus
	       && m_MachineModel != MachineModelZero
	       && m_MachineModel != MachineModelZeroW;
}

#include "gpio.h"

unsigned CMachineInfo_GetGPIOClockSourceRate(unsigned nSourceId) {
    int m_nModelMajor = 3;  // fxl, rpi3
    if (m_nModelMajor <= 3) {
        switch (nSourceId) {
        case GPIOClockSourceOscillator:
            return 19200000;
        case GPIOClockSourcePLLD:
            return 500000000;
        default:
            break;
        }
    } else {
        switch (nSourceId) {
        case GPIOClockSourceOscillator:
            return 54000000;
        case GPIOClockSourcePLLD:
            return 750000000;
        default:
            break;
        }
    }
    return GPIO_CLOCK_SOURCE_UNUSED;
}
