// cf: BCM2835 manual, pg 105, "General Purpose GPIO Clocks"
// "The General Purpose clocks can be output to GPIO pins. They run from the peripherals clock
// sources and use clock generators with noise-shaping MASH dividers. These allow the GPIO
// clocks to be used to drive audio devices."

// cf Circle gpioclock.h gpioclock.cpp

// clock types. used to locate the corresponding control regs (CM_GPxCTL CM_GPxDIV)
//    NB: the manual only specifies 0/1/2, missing PCM/PWM clocks. 
//    the defs below is right per https://elinux.org/BCM2835_registers#CM
typedef enum _TGPIOClock		
{
	GPIOClock0   = 0,			// on GPIO4 Alt0 or GPIO20 Alt5
	GPIOClock1   = 1,			// RPi 4: on GPIO5 Alt0 or GPIO21 Alt5
	GPIOClock2   = 2,			// on GPIO6 Alt0
	GPIOClockPCM = 5,
	GPIOClockPWM = 6            // fxl: pwm clock has nothing to do with gpio?
} TGPIOClock;

typedef enum _TGPIOClockSource       // cf manual, pg 107, "Clock Manager General Purpose Clocks Control"
{										// RPi 1-3:		RPi 4:
	GPIOClockSourceOscillator = 1,		// 19.2 MHz		54 MHz
	GPIOClockSourcePLLC       = 5,		// 1000 MHz (varies)	1000 MHz (may vary)
	GPIOClockSourcePLLD       = 6,		// 500 MHz		750 MHz
	GPIOClockSourceHDMI       = 7,		// 216 MHz		unused
	GPIOClockSourceUnknown    = 16
} TGPIOClockSource;

#define GPIO_CLOCK_SOURCE_ID_MAX	15		// source ID is 0-15
#define GPIO_CLOCK_SOURCE_UNUSED	0		// returned for unused clock sources

struct gpioclock_dev {
    TGPIOClock clock; 
    TGPIOClockSource source; 
};

void gpioclock_init(struct gpioclock_dev *desc, TGPIOClock clock);
int gpioclock_start_rate(struct gpioclock_dev *desc, unsigned nRateHZ);
void gpioclock_stop(struct gpioclock_dev *desc);

void gpio_useAsAlt0(unsigned int pin_number);
