#include <avr/interrupt.h>
#include <avr/io.h>

#include <math.h>
#include <stdint.h>

/* CALCULATIONS:
 * - Empirically, it's exactly 2.7Hz when prescaling by 8 with PERIOD=0xFFFF
 * - On p. 200 of <https://ww1.microchip.com/downloads/aemDocuments/documents/MCU08/ProductDocuments/DataSheets/ATmega4808-09-DataSheet-DS40002173C.pdf>:
 *   - Dual-slope PWM frequency = f_CLK_PER / (2N * PER) where
 *     - N represents the prescaler divider
 *     - PER (presumably) represents the period variable w/ a minimum resolution of 2 bits (i.e. 0x3)
 *     - f_CLK_PER is 16MHz prescaled by 6 = 16MHz / 6 = 2.666...Hz ~= 2,666,666Hz
 *   - So, under the conditions described on the first line, this should be 2,666,666Hz / (2 * 8 * 65535) ~= 2.54317Hz
 *   - Which means it's running slightly but meaningfully faster than it should be:
 *     - ??? / (2 * 8 * 65535) ~= 2.7Hz
 *     - ??? / 1,048,560 ~= 2.7Hz
 *     - ??? ~= 1,048,560 * 2.7Hz
 *     - ??? ~= 2,831,112Hz
 *   - We want a frequency of 50Hz for a period of 20ms
 *     - 2,831,112Hz / (2 * PRS * PER) = 50Hz
 *     - 2,831,112Hz / (PRS * PER) = 100Hz
 *     - 2,831,112Hz / 100Hz = PRS * PER
 *     - 28,311.12 = PRS * PER
 *     - PRS = 1, PER = 28,311.12 ~= 28,311
 *   - Actual frequency would be
 *     - 2,666,666Hz / (2 * PRS * PER) = 50Hz
 *     - 2,666,666Hz / (PRS * PER) = 100Hz
 *     - 2,666,666Hz / 100Hz = PRS * PER
 *     - 26,666.66 = PRS * PER
 *     - PRS = 1, PER = 26,666.66 ~= 26,667
 */

// #define PERIOD      (28311ULL)
#define PERIOD      (26667ULL)
#define MILLISECOND ((PERIOD + 10ULL) / 20ULL)
#define MIN_PULSE MILLISECOND
#define PULSE_RANGE MILLISECOND
// #define MIN_PULSE 0
// #define PULSE_RANGE PERIOD
#define TIME_TO_OPEN (50000ULL)

static volatile uint32_t counter;

static uint8_t last_counter = 0;
static _Bool open;
static float f;
static uint16_t duty;

ISR(TCB0_INT_vect) {
  ++counter;
}

inline static void TCA0_init(void) {
  PORTMUX.TCAROUTEA // <-- PWM signal to which port (set of pins)?
    = PORTMUX_TCA0_PORTA_gc // e.g. if `PORT_LTR` is A, then Port A (pins PA0, PA1, etc.)
    ;
  TCA0.SINGLE.CTRLB // <-- One set of configuration options for timer TCA
    = TCA_SINGLE_CMP0EN_bm // Enable comparison channel 0
    | TCA_SINGLE_WGMODE_DSBOTTOM_gc // Dual-slope mode
    ;
  TCA0.SINGLE.EVCTRL // <-- Another set of options about event reactions
    &= ~TCA_SINGLE_CNTEI_bm // Don't increment a counter each event
    ;
  TCA0.SINGLE.PERBUF // <-- Pulse period
    = PERIOD // Macro defined above
    ;
  TCA0.SINGLE.CMP0BUF // <-- Pulse duty cycle
    = MIN_PULSE; // Macro defined above
  TCA0.SINGLE.CTRLA // <-- Prescaler & on/off switch
    = TCA_SINGLE_CLKSEL_DIV1_gc // Run this at 1/4 the clock frequency (4x its cycle)
    | TCA_SINGLE_ENABLE_bm // Turn it on!
    ;
}

inline static void PORT_init(void) {    
  PORTA.DIR // <-- Which pin in this port gets the PWM signal?
    |= PIN0_bm // Route to pin PA0 (not PA1, PA2, etc.)
    ;
}

inline static void TCB0_init(void) {
  // p. 242 under 21.3.2 "Initialization"
  TCB0.CCMP = 0xFFFF;
  TCB0.INTCTRL |= 0b1;
  TCB0.CTRLA |= 0b1;
}

inline static void input_init(void) {
  // Pin PD5
  PORTD.DIRCLR |= (1 << 5); // Disable output on PD5 (make it input-only)
  PORTD.PIN5CTRL |= (1 << 3); // Enable the pull-up resistor on PD5
}

int main(void) {
  counter = 0;
  open = 0;
  last_counter = 0;

  cli(); // Disable interrupts (temporarily)
  PORT_init(); // Select our pin
  TCA0_init(); // Turn PWM on
  TCB0_init(); // Enable our slower timer
  input_init();
  sei(); // Enable interrupts
  // do {} while (FUSE.OSCCFG & (1 << 7)); // so FUSE.OSCCFG bit 7 is 0, which means that the system oscillator can be modified at runtime
  // do {} while (!(FUSE.OSCCFG & (1 << 0))); // so FUSE.OSCCFG bit 0 is 1, which means that the system oscillator runs at 16MHz
  // do {} while (FUSE.OSCCFG & (1 << 1)); // so FUSE.OSCCFG bit 1 is 0, which means that the system oscillator is correctly configured to run at 16MHz
  // do {} while (CLKCTRL.MCLKCTRLA & (1 << 0)); // so CLKCTRL.MCLKCTRLA bit 0 is 0, which needs the below...
  // do {} while (CLKCTRL.MCLKCTRLA & (1 << 1)); // so CLKCTRL.MCLKCTRLA bit 1 is 0, which means that CLK_MAIN comes from OSC20M at 16MHz
  // do {} while (!(CLKCTRL.MCLKCTRLB & (1 << 0))); // so CLKCTRL.MCLKCTRLB bit 0 is 1, which means the CLK_PER prescaler is enabled
  // do {} while (CLKCTRL.MCLKCTRLB & (1 << 1)); // so CLKCTRL.MCLKCTRLB bit 1 is 0
  // do {} while (CLKCTRL.MCLKCTRLB & (1 << 2)); // so CLKCTRL.MCLKCTRLB bit 2 is 0
  // do {} while (CLKCTRL.MCLKCTRLB & (1 << 3)); // so CLKCTRL.MCLKCTRLB bit 3 is 0
  // do {} while (!(CLKCTRL.MCLKCTRLB & (1 << 4))); // so CLKCTRL.MCLKCTRLB bit 4 is 1, which means CLK_PER is prescaled over 6 (p. 95)
  do {

    do {} while (PORTD.IN & (1 << 5));
    counter = last_counter = 0;
    open = !open;

    do {
      do {} while (last_counter == (uint8_t)counter);
      last_counter = counter;
      f = counter;
      f *= (3.14159265358979323846F / (float)TIME_TO_OPEN);
      f = cosf(f);
      f += 1.F;
      f *= (0.5F * PULSE_RANGE);
      duty = f;
      if (open) {
        duty = -duty;
        duty += (MIN_PULSE + PULSE_RANGE);
      } else {
        duty += MIN_PULSE;
      }
      TCA0.SINGLE.CMP0BUF = duty;
    } while (counter < TIME_TO_OPEN);

  } while (1);
}
