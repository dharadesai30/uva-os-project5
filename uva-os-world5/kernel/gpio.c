#define K2_DEBUG_WARN

#include "utils.h"
#include "plat.h"
#include "circle-glue.h"
#include "gpio.h"

// re: pull-up/down https://grantwinney.com/using-pullup-and-pulldown-resistors-on-the-raspberry-pi/
// https://fxlin.github.io/p1-kernel/exp1/rpi-os/#init-gpio-alternative-function-selection
enum {
    GPIO_MAX_PIN       = 53,
    GPIO_FUNCTION_IN  = 0,
    GPIO_FUNCTION_OUT  = 1,
    GPIO_FUNCTION_ALT5 = 2,
    GPIO_FUNCTION_ALT3 = 7,
    GPIO_FUNCTION_ALT0 = 4
};

// Change reg bits given GPIO pin number. return 1 on OK
static unsigned int gpio_call(unsigned int pin_number, unsigned int value, 
    unsigned int base, unsigned int field_size, unsigned int field_max) {
    unsigned int field_mask = (1 << field_size) - 1;
  
    if (pin_number > field_max) return 0;
    if (value > field_mask) return 0; 

    unsigned int num_fields = 32 / field_size;
    unsigned int reg = base + ((pin_number / num_fields) * 4);
    unsigned int shift = (pin_number % num_fields) * field_size;

    unsigned int curval = get32va(reg);
    curval &= ~(field_mask << shift);
    curval |= value << shift;
    put32va(reg, curval);

    return 1;
}

unsigned int gpio_set     (unsigned int pin_number, unsigned int value) 
    { return gpio_call(pin_number, value, ARM_GPIO_GPSET0, 1, GPIO_MAX_PIN); }
unsigned int gpio_clear   (unsigned int pin_number, unsigned int value) 
    { return gpio_call(pin_number, value, ARM_GPIO_GPCLR0, 1, GPIO_MAX_PIN); }

// Below: set up GPIO pull modes. protocol recommended by the bcm2837 manual 
//    (pg 101, "GPIO Pull-up/down Clock Registers")
// cf: https://github.com/bztsrc/raspi3-tutorial/blob/master/03_uart1/uart.c
// cf Circle void CGPIOPin::SetPullMode
enum {
    Pull_None = 0,
    Pull_Down = 1, 
    Pull_Up = 2
};
unsigned int gpio_pull (unsigned int pin_number, unsigned int mode) { 
    unsigned long clkreg = ARM_GPIO_GPPUDCLK0 + (pin_number / 32) * 4;
    unsigned int regmask = 1 << (pin_number % 32);

    put32va(ARM_GPIO_GPPUD, mode); 
    delay(150);   // per manual
    put32va(clkreg, regmask); 
    delay(150);  
    put32va(ARM_GPIO_GPPUD, 0); 
    put32va(clkreg, 0); 

    return 0;
}

unsigned int gpio_function(unsigned int pin_number, unsigned int value) 
    { return gpio_call(pin_number, value, ARM_GPIO_GPFSEL0, 3 /*field size*/, GPIO_MAX_PIN); }

// Rpi3 GPIO functions
// https://fxlin.github.io/p1-kernel/exp1/rpi-os/#init-gpio-alternative-function-selection
void gpio_useAsAlt0(unsigned int pin_number) {
    gpio_pull(pin_number, Pull_None);
    gpio_function(pin_number, GPIO_FUNCTION_ALT0);
}

void gpio_useAsAlt3(unsigned int pin_number) {
    gpio_pull(pin_number, Pull_None);
    gpio_function(pin_number, GPIO_FUNCTION_ALT3);
}

void gpio_useAsAlt5(unsigned int pin_number) {
    gpio_pull(pin_number, Pull_None);
    gpio_function(pin_number, GPIO_FUNCTION_ALT5);
}

// fxl: is this right?
void gpio_useAsOutput(unsigned int pin_number) {
    gpio_pull(pin_number, Pull_None);
    gpio_function(pin_number, GPIO_FUNCTION_OUT);
}

void gpio_useAsInput(unsigned int pin_number) {
    gpio_pull(pin_number, Pull_None);
    gpio_function(pin_number, GPIO_FUNCTION_IN);
}

//// gpio clock 
// cf: BCM2835 manual, pg 105, "General Purpose GPIO Clocks"
// "The General Purpose clocks can be output to GPIO pins. They run from the peripherals clock
// sources and use clock generators with noise-shaping MASH dividers. These allow the GPIO
// clocks to be used to drive audio devices."

#define CLK_CTL_MASH(x)		((x) << 9)
#define CLK_CTL_BUSY		(1 << 7)
#define CLK_CTL_KILL		(1 << 5)
#define CLK_CTL_ENAB		(1 << 4)
#define CLK_CTL_SRC(x)		((x) << 0)

#define CLK_DIV_DIVI(x)		((x) << 12)
#define CLK_DIV_DIVF(x)		((x) << 0)

void gpioclock_init(struct gpioclock_dev *desc, TGPIOClock clock) {
    desc->clock = clock; desc->source = GPIOClockSourceUnknown; 
}

void gpioclock_stop(struct gpioclock_dev *desc) {
	unsigned nCtlReg = ARM_CM_GP0CTL + (desc->clock * 8);

	// PeripheralEntry ();

	put32va (nCtlReg, ARM_CM_PASSWD | CLK_CTL_KILL);
	while (get32va (nCtlReg) & CLK_CTL_BUSY)
	{
		// wait for clock to stop
	}

	// PeripheralExit ();
}

// nDivI: 1..4095, allowed minimum depends on MASH
// nDivF: 0..4095
// nMASH: 0..3 MASH control
// cf: SoC manual, table 6-33 "Example of Frequency Spread when using MASH Filtering"
static void gpioclock_start(struct gpioclock_dev *desc, 
            unsigned nDivI, unsigned nDivF, unsigned nMASH) {
	static unsigned MinDivI[] = {1, 2, 3, 5};
	assert (nMASH <= 3);
	assert (MinDivI[nMASH] <= nDivI && nDivI <= 4095);
	assert (nDivF <= 4095);

	unsigned nCtlReg = ARM_CM_GP0CTL + (desc->clock * 8);
	unsigned nDivReg  = ARM_CM_GP0DIV + (desc->clock * 8);

	gpioclock_stop (desc);

	put32va (nDivReg, ARM_CM_PASSWD | CLK_DIV_DIVI (nDivI) | CLK_DIV_DIVF (nDivF));
	us_delay(10); 
	assert (desc->source < GPIOClockSourceUnknown);
	put32va (nCtlReg, ARM_CM_PASSWD | CLK_CTL_MASH (nMASH) | CLK_CTL_SRC (desc->source));
	us_delay(10); 
	put32va (nCtlReg, read32 (nCtlReg) | ARM_CM_PASSWD | CLK_CTL_ENAB);
}

// assigns clock source automatically
// return 1 on ok TODO move to MachineInfo
int gpioclock_start_rate(struct gpioclock_dev *desc, unsigned nRateHZ) {
	assert (nRateHZ > 0);
	// fxl: iterate through all available clock sources, find one aivalable ....
	//			will write the source id to reg in Start()
	for (unsigned nSourceId = 0; nSourceId <= GPIO_CLOCK_SOURCE_ID_MAX; nSourceId++)
	{
		unsigned nSourceRate = CMachineInfo_GetGPIOClockSourceRate (nSourceId);
        if (nSourceRate == GPIO_CLOCK_SOURCE_UNUSED) {
            continue;
        }

        unsigned nDivI = nSourceRate / nRateHZ;
        if (1 <= nDivI && nDivI <= 4095 && nRateHZ * nDivI == nSourceRate) {
            desc->source = (TGPIOClockSource)nSourceId;
            gpioclock_start(desc, nDivI, 0, 0 /*mash*/); // use simplest strategy?
            return 1;
        }
    }
	return 0;
}

////////////////////////////////////////////////////////////
/* 
    GPIO drivers for keys on Waveshare Game HAT
    https://www.waveshare.com/wiki/Game_HAT
    These do not conflict with sound.c: the audio jack uses GPIO pins 40 (PWM0)
    and 41 (PWM1).

    The GPIO keypresses are also delivered via /dev/events, with scan codes defined by ourselves 
    (instead of having a separate /dev for the Game HAT keys).
    Pros: Simplifies user/kernel interface, app lib/code does not have to change much.
    Cons: USB keyboard and GPIO keypads are more coupled.
*/

// keys and their gpio numbers
#define GAMEHAT_SELECT      4
// these two are swapped in the manual?
// #define GAMEHAT_TL          18
// #define GAMEHAT_TR          23
#define GAMEHAT_TL          23      
#define GAMEHAT_TR          18
#define GAMEHAT_UP          5
#define GAMEHAT_DOWN        6
#define GAMEHAT_B           12
#define GAMEHAT_LEFT        13
#define GAMEHAT_RIGHT       19
#define GAMEHAT_X           16
#define GAMEHAT_A           26
#define GAMEHAT_Y           20
#define GAMEHAT_START       21

const int gamehat_keys[] = {
    GAMEHAT_SELECT,
    GAMEHAT_TL,
    GAMEHAT_TR,
    GAMEHAT_UP,
    GAMEHAT_DOWN,
    GAMEHAT_B,
    GAMEHAT_LEFT,
    GAMEHAT_RIGHT,
    GAMEHAT_X,
    GAMEHAT_A,
    GAMEHAT_Y,
    GAMEHAT_START
};

const char* gamehat_key_strings[] = {
    [GAMEHAT_SELECT] = "GAMEHAT_SELECT",
    [GAMEHAT_TL] = "GAMEHAT_TL",
    [GAMEHAT_TR] = "GAMEHAT_TR",
    [GAMEHAT_UP] = "GAMEHAT_UP",
    [GAMEHAT_DOWN] = "GAMEHAT_DOWN",
    [GAMEHAT_B] = "GAMEHAT_B",
    [GAMEHAT_LEFT] = "GAMEHAT_LEFT",
    [GAMEHAT_RIGHT] = "GAMEHAT_RIGHT",
    [GAMEHAT_X] = "GAMEHAT_X",
    [GAMEHAT_A] = "GAMEHAT_A",
    [GAMEHAT_Y] = "GAMEHAT_Y",
    [GAMEHAT_START] = "GAMEHAT_START"
};

// GPIO Function Select Registers (GPFSELn) are used to define the operation 
// of the general-purpose I/O pins. 3 bits per pin. Each register covers 10 pins.
void gpio_set_input(int pin) {
    unsigned int reg = ARM_GPIO_GPFSEL0 + ((pin / 10) * 4); // these regs are contig in mem
    unsigned int curval = get32va(reg);
    unsigned int shift = (pin % 10) * 3;
    curval &= ~(0b111 << shift); // Clear bits (set to input mode)
    put32va(reg, curval);
}

// return the pin level of a gpio pin
int gpio_read_level(int pin) {
    unsigned int reg = (pin < 32) ? ARM_GPIO_GPLEV0 : ARM_GPIO_GPLEV1;
    unsigned int curval = get32va(reg);
    pin = (pin < 32) ? pin : (pin - 32);
    return (curval & (1 << pin)) ? 1 : 0;
}

// these regs are for two banks, 32 gpios each. 
void gpio_enable_rising_edge_detect(int pin) {
    unsigned reg = (pin < 32) ? ARM_GPIO_GPREN0 : ARM_GPIO_GPREN1;
    unsigned int curval = get32va(reg);
    pin = (pin < 32) ? pin : (pin - 32);
    curval |= (1 << pin);
    put32va(reg, curval);    
}

void gpio_enable_falling_edge_detect(int pin) {
    unsigned reg = (pin < 32) ? ARM_GPIO_GPFEN0 : ARM_GPIO_GPFEN1;
    unsigned int curval = get32va(reg);
    pin = (pin < 32) ? pin : (pin - 32);
    curval |= (1 << pin);
    put32va(reg, curval);    
}

void gpio_enable_low_detect(int pin) {
    unsigned reg = (pin < 32) ? ARM_GPIO_GPLEN0 : ARM_GPIO_GPLEN1;
    unsigned int curval = get32va(reg);
    pin = (pin < 32) ? pin : (pin - 32);
    curval |= (1 << pin);
    put32va(reg, curval);
}

void gamehat_gpio_init(void) {
    for (int i = 0; i < sizeof(gamehat_keys) / sizeof(gamehat_keys[0]); i++) {
        gpio_set_input(gamehat_keys[i]);
        // must set pull mode explicitly. otherwise, the gpio will be floating
        // without a pull resistor, the GPIO pin is unable to decide if it
        // should read HIGH or LOW, which leads to unreliable or no detection of
        // button presses
        gpio_pull(gamehat_keys[i], Pull_Up); 
        gpio_enable_rising_edge_detect(gamehat_keys[i]);
        gpio_enable_falling_edge_detect(gamehat_keys[i]);
        // gpio_enable_low_detect(GAMEHAT_SELECT);  // works, but not needed
    }

    W("gamehat_gpio_init done");

    // polling, test
    while (0) {
        printf("ARM_GPIO_GPLEV0 0x%x\n", get32va(ARM_GPIO_GPLEV0));
        printf("ARM_GPIO_GPEDS0 0x%x\n", get32va(ARM_GPIO_GPEDS0));

        if (get32va(ARM_GPIO_GPEDS0) & (1 << GAMEHAT_START)) {
            printf("ARM_GPIO_GPEDS0 event detected\n");
            put32va(ARM_GPIO_GPEDS0, 1 << GAMEHAT_START);
        }
        ms_delay(500);
    }
}

// below: emulate keyboard events with gamehat keys

#include "spinlock.h"
#include "kb.h"
// map the HAT keys to standard scancodes commonly used in our apps
// in the future, may define own scancodes
// https://gist.github.com/MightyPork/6da26e382a7ad91b5496ee55fdc73db2
// btw, it's weird that on the Waveshare game hat, "Start" is on the left side while
// "Select" is on the right side, just opposite to most Nintendo controllers
// (and others)

const int gamehat_gpio_to_scancode[] = {
    [GAMEHAT_SELECT] = KEY_TAB,     // as in many console emulators
    [GAMEHAT_TL] = KEY_PAGEUP,
    [GAMEHAT_TR] = KEY_PAGEDOWN,
    [GAMEHAT_UP] = KEY_UP,
    [GAMEHAT_DOWN] = KEY_DOWN,
    [GAMEHAT_B] = KEY_B,
    [GAMEHAT_LEFT] = KEY_LEFT,
    [GAMEHAT_RIGHT] = KEY_RIGHT,
    [GAMEHAT_X] = KEY_X,
    [GAMEHAT_A] = KEY_A,
    [GAMEHAT_Y] = KEY_Y,
    [GAMEHAT_START] = KEY_ENTER    // as in many console emulators
};

// create & deliver an event to kbevent's queue (the_kb, kb.c)
// modeled after kb_intr() in kb.c

extern struct kb_struct the_kb;  // kb.c

// "down": 1 for keydown (falling edge), 0 for keyup (rising)
// return: 0 on success. otherwise -1
int enqueue_kb_event(int gpio_pin, int down, int mod) {    
    int ret = -1; 
    acquire(&the_kb.lock);
    if (the_kb.w-the_kb.r < INPUT_BUF_SIZE) {
        struct kbevent ev = {
            .type = down ? KEYDOWN : KEYUP,
            .mod = mod,
            .scancode = gamehat_gpio_to_scancode[gpio_pin]};
        the_kb.buf[the_kb.w++ % INPUT_BUF_SIZE] = ev;
        wakeup(&the_kb.r);
        ret = 0; 
    }
    release(&the_kb.lock);
    return ret; 
}

// bank: 0 or 1. bank0: gpios 0..31, bank1: gpios 32..53
// only check one specific bank (32 gpios)
void gamehat_gpio_irq(int bank) {
    unsigned int arm_gpio_gpeds[] = {ARM_GPIO_GPEDS0, ARM_GPIO_GPEDS1};
    // unsigned int arm_gpio_gplev[] = {ARM_GPIO_GPLEV0, ARM_GPIO_GPLEV1};

    // pin level status, bank0,1
    unsigned int gplev[] = {get32va(ARM_GPIO_GPLEV0), get32va(ARM_GPIO_GPLEV1)};     
    // event detect status, bank0,1
    unsigned int gpeds[] = {get32va(ARM_GPIO_GPEDS0), get32va(ARM_GPIO_GPEDS1)};
    
    ms_delay(50); // Typical debounce delay. 20ms seem too short (single keypress ->multiple events)

    // trick: to emulate "mod" in keyboard events, which will be used for surface switch (sf.c)
    int mod = 0; 
    // LT+RT -> LCTRL
    if (gpio_read_level(GAMEHAT_TL)==0 && gpio_read_level(GAMEHAT_TR)==0)
        mod |= KEY_MOD_LCTRL; 

    for (int i = 0; i<32; i++) {
        int gpio_pin = i + bank * 32;
        if (gpeds[bank] & (1 << i)) {
            // ms_delay(20); // Typical debounce delay
            if (gplev[bank] & (1 << i)) {
                V("rising edge detected: %s", 
                    gamehat_key_strings[gpio_pin]);
                enqueue_kb_event(gpio_pin, 0, mod); // keyup
            } else {
                if (mod)
                W("falling edge detected: %s", 
                    gamehat_key_strings[gpio_pin]);
                enqueue_kb_event(gpio_pin, 1, mod); // keydown
            }
            // clear the flag, so no more irqs
            put32va(arm_gpio_gpeds[bank], 1<<i);
            // W("GAMEHAT_START is %d SELECT is %d", gpio_read_level(GAMEHAT_START), gpio_read_level(GAMEHAT_SELECT));
        }
    }
}

