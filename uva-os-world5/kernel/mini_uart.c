/* 
  mini uart driver, which implements: 

  tx: * polling. used for kernel printf(), crucial for debugging

      the current impl: sycall write() goes to console, which invokes uartputc() in
      this file, which calls uartstart() waiting for all chars to go out. 

      * (NOT implemented) irq driven tx: caller task wait on tx
      queue until non-full. primary use is syscall write(), e.g. for console      
      I found it tough to debug so ditched it 

  rx: polling (not quite useful), irq driven. 
      as a simple input device for kernel testing
      as stdin for user tasks (via console device)
      not used for kernel debugging. 

  uart may not be the best device to testing irq-driven tx, b/c debugging can be
  hard. but the code is good for demonstration purpose
*/

#define K2_DEBUG_WARN 

#include <stdint.h>
#include "plat.h"
#include "mmu.h"
#include "utils.h"
#include "spinlock.h"

// cf: https://github.com/bztsrc/raspi3-tutorial/blob/master/03_uart1/uart.c
// cf: https://github.com/futurehomeno/RPI_mini_UART/tree/master

#define PBASE   0x3F000000

// ---------------- gpio ------------------------------------ //
// cf BCM2837 manual, chap 6, "General Purpose I/O (GPIO)"
#define GPFSEL1         (PBASE+0x00200004)    // "GPIO Function Select"
#define GPSET0          (PBASE+0x0020001C)    // "GPIO Pin Output Set"
#define GPCLR0          (PBASE+0x00200028)    // "GPIO Pin Output Clear"
#define GPPUD           (PBASE+0x00200094)    // "GPIO Pin Pull-up/down Enable"
#define GPPUDCLK0       (PBASE+0x00200098)    // "GPIO Pin Pull-up/down Enable Clock"

// ---------------- mini uart ------------------------------------ //
// "The Device has three Auxiliary peripherals: One mini UART and two SPI masters. These
// three peripheral are grouped together as they share the same area in the peripheral register
// map and they share a common interrupt."
#define AUXIRQ          (PBASE+0x00215000)    // bit0: "If set the mini UART has an interrupt pending"
#define AUX_ENABLES     (PBASE+0x00215004)    // "AUXENB" in datasheet
#define AUX_MU_IO_REG   (PBASE+0x00215040)
#define AUX_MU_IER_REG  (PBASE+0x00215044)    // enable tx/rx irqs
  #define AUX_MU_IER_RXIRQ_ENABLE 1U      
  #define AUX_MU_IER_TXIRQ_ENABLE 2U      

#define AUX_MU_IIR_REG  (PBASE+0x00215048)    // check irq cause, fifo clear
  #define IS_TRANSMIT_INTERRUPT(x) (x & 0x2)  //0b010  tx empty
  #define IS_RECEIVE_INTERRUPT(x) (x & 0x4)   //0b100  rx ready
  #define FLUSH_UART 0xC6

#define AUX_MU_LCR_REG  (PBASE+0x0021504C)
#define AUX_MU_MCR_REG  (PBASE+0x00215050)

#define AUX_MU_LSR_REG  (PBASE+0x00215054)
  #define IS_TRANSMITTER_EMPTY(x) (x & 0x10)
  #define IS_TRANSMITTER_IDLE(x)  (x & 0x20)
  #define IS_DATA_READY(x) (x & 0x1)
  #define IS_RECEIVER_OVEERUN(x) (x & 0x2)
#define AUX_MU_MSR_REG  (PBASE+0x00215058)

#define AUX_MU_SCRATCH  (PBASE+0x0021505C)
#define AUX_MU_CNTL_REG (PBASE+0x00215060)

#define AUX_MU_STAT_REG (PBASE+0x00215064)

#define AUX_MU_BAUD_REG (PBASE+0x00215068)

// the transmit output buffer.
struct spinlock uart_tx_lock;
#define UART_TX_BUF_SIZE 32
char uart_tx_buf[UART_TX_BUF_SIZE];
uint64 uart_tx_w=0; // write next to uart_tx_buf[uart_tx_w % UART_TX_BUF_SIZE]
uint64 uart_tx_r=0; // read next from uart_tx_buf[uart_tx_r % UART_TX_BUF_SIZE]

long uart_send_profile (char c)
{
  long cnt = 0; 
	while(1) {
    cnt ++; 
		if(get32va(AUX_MU_LSR_REG) & 0x20) 
			break;
	}
	put32va(AUX_MU_IO_REG, c);
  return cnt; 
}

void uart_send (char c)
{
	while(1) {
		if(get32va(AUX_MU_LSR_REG) & 0x20) 
			break;
	}
	put32va(AUX_MU_IO_REG, c);
}
 
char uart_recv (void)
{
	while(1) {
		if(get32va(AUX_MU_LSR_REG) & 0x01) 
			break;
	}
	return(get32va(AUX_MU_IO_REG) & 0xFF);
}

void uart_send_string(char* str)
{
	for (int i = 0; str[i] != '\0'; i ++) {
		uart_send((char)str[i]);
	}
}

/* ------------------------ new apis ---------------------- */
void uartstart();

__attribute__ ((unused)) static void uart_enable_rx_irq() {
  unsigned int ier = get32va(AUX_MU_IER_REG); 
  put32va(AUX_MU_IER_REG, ier | AUX_MU_IER_RXIRQ_ENABLE);
}

__attribute__ ((unused)) static void uart_enable_tx_irq() {
  unsigned int ier = get32va(AUX_MU_IER_REG); 
  put32va(AUX_MU_IER_REG, ier | AUX_MU_IER_TXIRQ_ENABLE);
  // E("enable tx irq");
}

__attribute__ ((unused)) static void uart_disable_tx_irq() {
  // unsigned int ier = get32va(AUX_MU_IER_REG); 
  // put32va(AUX_MU_IER_REG, ier & (~AUX_MU_IER_TXIRQ_ENABLE));
  put32va(AUX_MU_IER_REG, 0);
  // E("disable tx irq");
}

__attribute__ ((unused)) static unsigned int uart_tx_empty() {
  unsigned int lsr = get32va(AUX_MU_LSR_REG);
  return IS_TRANSMITTER_EMPTY(lsr);
}

__attribute__ ((unused)) static unsigned int uart_tx_idle() {
  unsigned int lsr = get32va(AUX_MU_LSR_REG);
  return IS_TRANSMITTER_IDLE(lsr);
}


void uart_init (void) {

	unsigned int selector;

  // code below also showcases how to configure GPIO pins
  // cf: https://github.com/bztsrc/raspi3-tutorial/blob/master/03_uart1/uart.c#L45

  // select gpio functions for pin14,15. note 3bits per pin.
	selector = get32va(GPFSEL1);
	selector &= ~(7<<12);                   // clean gpio14 (12 is not a typo)
	selector |= 2<<12;                      // set alt5 for gpio14
	selector &= ~(7<<15);                   // clean gpio15
	selector |= 2<<15;                      // set alt5 for gpio15
	put32va(GPFSEL1,selector);
  
  // Below: set up GPIO pull modes. protocol recommended by the bcm2837 manual 
  //    (pg 101, "GPIO Pull-up/down Clock Registers")
  // We need neither the pull-up nor the pull-down state, because both 
  //  the 14 and 15 pins are going to be connected all the time. 
	put32va(GPPUD,0);   // disable pull up/down control (for pins below)
	delay(150);
  // "control the actuation of internal pull-downs on the respective GPIO pins."
	put32va(GPPUDCLK0,(1<<14)|(1<<15));  // "clock the control signal into the GPIO pads"
	delay(150);
	put32va(GPPUDCLK0,0);                   // remote the clock, flush GPIO setup
  put32va(AUX_MU_IIR_REG, FLUSH_UART);    // flush FIFO

	put32va(AUX_ENABLES,1);                   //Enable mini uart (this also enables access to it registers)
	put32va(AUX_MU_CNTL_REG,0);               //Disable auto flow control and disable receiver and transmitter (for now)
	
  // put32va(AUX_MU_IER_REG, 0);                //Disable receive and transmit interrupts
  put32va(AUX_MU_IER_REG,  (3<<2) | (0xf<<4));    // bit 7:4 3:2 must be 1
  uart_enable_rx_irq(); 
	
  put32va(AUX_MU_LCR_REG,3);                //Enable 8 bit mode
	put32va(AUX_MU_MCR_REG,0);                //Set RTS line to be always high
	put32va(AUX_MU_BAUD_REG,270);             //Set baud rate to 115200

	put32va(AUX_MU_CNTL_REG,3);               //Finally, enable transmitter and receiver

  initlock(&uart_tx_lock, "uart");


  // profile 
  // uart_send_string("a very long string that exceeds the size of the fifo buffer");
  // delay_cycles = uart_send_profile('T'); 
  // long cnt = 0; 
  // while(1) {
  //   cnt ++; 
  //   if(get32va(AUX_MU_LSR_REG) & 0x20) 
  //     break;
  // }
  // printf("cnt = %ld", cnt); 
}

// add a character to the output buffer and tell the
// UART to start sending if it isn't already.
// blocks if the output buffer is full.
// because it may block, it can't be called
// from interrupts; it's only suitable for use
// by write().
void uartputc(int c) {
    acquire(&uart_tx_lock);
    V("called %c", c);

    // if (panicked) { // freeze uart out from other cpus. we dont need it (fxl)
    //     for (;;)
    //         ;
    // }
    while (uart_tx_w == uart_tx_r + UART_TX_BUF_SIZE) {
        // buffer is full.
        // wait for uartstart() to open up space in the buffer.
        W("going to sleep...");
        sleep(&uart_tx_r, &uart_tx_lock);
    }
    uart_tx_buf[uart_tx_w % UART_TX_BUF_SIZE] = c;
    uart_tx_w += 1;
    uartstart();
    release(&uart_tx_lock);
    
    if (kconsole_printf) 
      kconsole_putc(c);    
}

// alternate version of uartputc() that doesn't 
// use interrupts, for use by kernel printf() and
// to echo characters. it spins waiting for the uart's
// output register to be empty.
void uartputc_sync(int c) {
  push_off();

//   if(panicked){while(1);}         // cf above

  while(1) {    
		if(get32va(AUX_MU_LSR_REG) & 0x20) 
			break;
	}
	put32va(AUX_MU_IO_REG, c);

  pop_off();
}

// if the UART is idle, and a character is waiting
// in the transmit buffer, send it.
// caller must hold uart_tx_lock.
// called from both the top- and bottom-half.
void uartstart() {
  // char need_tx_irq = 0; 

  while(1) {
    if(uart_tx_w == uart_tx_r){
      // transmit buffer is empty.
      // if (need_tx_irq)
      //   uart_enable_tx_irq(); 
      return;
    }

      // fxl: if called from irq, should spin here until all previous
      //    uart writes (printf()) are done? otherwise the line below
      //    may never get a chance to write to uart ....

    while (1) {
        // if (get32va(AUX_MU_LSR_REG) & 0x20)
            // if(get32va(AUX_MU_LSR_REG) & 0x10) // not working
            // if (uart_tx_empty())
        if (uart_tx_idle())   // works??
            break;
    }

#if 0 
    if(!uart_tx_empty()) {
      // E("tx busy...");      
      // fxl: almost always busy....??? no chance to write??
      // should alwasy enable tx irwq here???

      // the UART transmit holding register is full,
      // so we cannot give it another byte.
      // it will interrupt when it's ready for a new byte.
      // if (need_tx_irq)
        uart_enable_tx_irq(); 
      return;
    }
#endif

    int c = uart_tx_buf[uart_tx_r % UART_TX_BUF_SIZE];
    uart_tx_r += 1;
    
    // maybe uartputc() is waiting for space in the buffer.
    wakeup(&uart_tx_r);
    
    put32va(AUX_MU_IO_REG, c); 
    // need_tx_irq = 1; 
    V("send a byte...");
  }
}

// fxl: AUX_MU_LSR_REG seems not working. always return -1
// read one input character from the UART.
// return -1 if none is waiting.
// int
// uartgetc(void)
// {
//   if(!(get32va(AUX_MU_LSR_REG) & 0x01)) {
//     // input data is ready.
//     return (int)(get32va(AUX_MU_IO_REG) & 0xFF);
//   } else {
//     return -1;
//   }
// }

int uartgetc(void)
{
  if (IS_DATA_READY(get32va(AUX_MU_LSR_REG))) {  
    // rx fifo has bytes
    return (int)(get32va(AUX_MU_IO_REG) & 0xFF);
  } else {
    return -1;
  }

#if 0 
/* Below also works on realy rpi3, but triggered a qemu bug that only got fixed in
  July 2024. will crash earlier qemu, e.g. when paste a long command line to qemu.
  https://github.com/qemu/qemu/commit/546d574b11e02bfd5b15cdf1564842c14516dfab
*/
  if(!(get32va(AUX_MU_STAT_REG) & 0xF0000)) {
    return -1;
  } else {
    // rx fifo has bytes
    return (int)(get32va(AUX_MU_IO_REG) & 0xFF);
  }
#endif  
}

// handle a uart interrupt, raised because input has
// arrived, or the uart is ready for more output, or
// both. called from handle_irq()
void uartintr(void) {
  // TODO: check AUX_MU_IIR_REG bit0 for pending irq
  //    and 2:1 for irq causes
  uint iir = get32va(AUX_MU_IIR_REG); 
  if (iir & 1) 
    return; 

  V("pending irq: p %d w %d r %d", (iir & 1), (iir & 2), (iir & 4));
    
  // clear rx irq, must be done before we read 
  // no need for mini uart??

#if 1 
  // read and process incoming characters.
  if (IS_RECEIVE_INTERRUPT(iir)) {
    while(1){
      int c = uartgetc();
      if(c == -1) {
        // W("no char");
        break;
      }
      // W("read a char");
      consoleintr(c);
    }
  }
#endif

  // clear tx irq XXX no need for mini uart?
  // no need for mini uart??
#if 1
  if (IS_TRANSMIT_INTERRUPT(iir)) {
    // uart_disable_tx_irq(); 
    // send buffered characters.
    acquire(&uart_tx_lock);
    uartstart();
    release(&uart_tx_lock);
  }
#endif  
}

// called by printf() 

void putc (void* p, char c)
{
	uart_send(c);
  if (kconsole_printf) 
    kconsole_putc(c);
}