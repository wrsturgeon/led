// Will Sturgeon for Spectral Motion, Inc.

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%% READ THIS FIRST:
//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

// The signal for LED number [N] is sent through pin D[N].
// For example, LED number 3 should get its signal from pin D3,
// which is labelled PD3 in the pinout below:
// https://docs.arduino.cc/resources/pinouts/ABX00028-full-pinout.pdf

// If you'd like to edit settings like beats per minute or luminosity range,
// look past the short import section (which has to stay at the top)
// to the next section, labelled "SETTINGS."

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%% Import some information about the board:
//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

#include <avr/interrupt.h>
#include <avr/io.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%% SETTINGS:
//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

// Frame rate:
#define FRAME_RATE 100

// Beats per minute:
#define BPM 45

// Which pins should send a signal to which LEDs, in order?
static char const *const PIN_NAMES[] = {"D0", "D1", "D2", "D3"};

// Minimum luminosity percent (/100):
#define MIN_LUMINOSITY_PERCENT 10

// Maximum luminosity percent (/100):
#define MAX_LUMINOSITY_PERCENT 90

// Calculate an LED's position based on its time within a cycle (from 0 to 1)
// and its ID. NOTE: Make sure the output is between 0 and 1!
float luminosity(float const percent_of_cycle, uint8_t const id) {
  return 0.5F * (1.F + sinf(6.2831853072 * percent_of_cycle + id));
}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%% Below this line is complex: tread carefully!
//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

// Luminosity sanity-checks:
#if MIN_LUMINOSITY_PERCENT < 0
#error Minimum luminosity is less than 0%
#endif
#if MAX_LUMINOSITY_PERCENT > 100
#error Maximum luminosity is more than 100%
#endif
#if MIN_LUMINOSITY_PERCENT > MAX_LUMINOSITY_PERCENT
#error Minimum luminosity is more than maximum luminosity
#endif

/* CALCULATIONS:
 * - On p. 200 of
 * <https://ww1.microchip.com/downloads/aemDocuments/documents/MCU08/ProductDocuments/DataSheets/ATmega4808-09-DataSheet-DS40002173C.pdf>:
 *   - Single-slope PWM frequency = f_CLK_PER / N(PER + 1) where
 *     - N represents the prescaler divider
 *     - PER (presumably) represents the period variable
 *       w/ a minimum resolution of 2 bits (i.e. 0x3)
 *     - f_CLK_PER is 16MHz prescaled by 6:
 *       16MHz / 6 = 2.666...Hz ~= 2,666,666Hz
 *     - Note that "+ 1" means that the clock goes up to *and including* `PER`.
 *   - We can work backwards to yield a specified frame rate:
 *     - f_CLK_PER / PRESCALE(PERIOD + 1) = FRAME_RATE
 *     - 2,666,666Hz / PRESCALE(PERIOD + 1) = FRAME_RATE
 *     - 2,666,666 / FRAME_RATE = PRESCALE(PERIOD + 1)
 *   - In any sane LED-dimming program, the frame rate will be at least 50Hz,
 *     and we want `PERIOD` to be as large as possible,
 *     so let's work backward again:
 *     - 2,666,666/50 <= PRESCALE * (0xFFFF + 1)
 *     - 53,333.33... <= PRESCALE * (0xFFFF + 1)
 *     - 53,333.33... <= PRESCALE * 65,536
 *     - 0.8138021... <= PRESCALE
 *   - Okay, so PRESCALE=1 (i.e. no prescaling) is the closest we can get
 *   - Now we can calculate `PERIOD`:
 *     - 2,666,666 / FRAME_RATE     = (PERIOD + 1) * PRESCALE
 *     - 2,666,666 / FRAME_RATE     = (PERIOD + 1) * 1
 *     - 2,666,666 / FRAME_RATE     = (PERIOD + 1)
 *     - 2,666,666 / FRAME_RATE - 1 =  PERIOD
 */
#define CLOCKS_PER_FRAME (2666666ULL / FRAME_RATE)
#define PERIOD (CLOCKS_PER_FRAME - 1ULL)
#if PERIOD > 65535
#error Frame rate is too slow!
#endif
#define HALF_PERIOD (PERIOD >> 1U)

#define PLUS_MINUS_MIN                                                         \
  (((MIN_LUMINOSITY_PERCENT * HALF_PERIOD) + 50ULL) / 100ULL)
#define PLUS_MINUS_MAX                                                         \
  (((MAX_LUMINOSITY_PERCENT * HALF_PERIOD) + 50ULL) / 100ULL)
#define PLUS_MINUS_RANGE (PLUS_MINUS_MAX - PLUS_MINUS_MIN)

// How many LEDs are we using?
// static uint8_t const N_LEDS = (sizeof(PIN_NAMES) / sizeof(char const
// *const));
#define N_LEDS (sizeof(PIN_NAMES) / sizeof(char const *const))

// Convert beats per minute to a heartbeat period in terms of PWM pulses:
#define PERIOD_IN_CYCLES ((60ULL * FRAME_RATE) / BPM)

typedef struct led {
  uint16_t pulse_width;
  uint8_t id;
} led;

static uint16_t cycle_count;
static led pulse_widths[N_LEDS];

// Bubble sort is not only fine but actually the most efficient here
// because (1) it doesn't allocate any memory and (2) servo positions
// move very slightly if at all between frames, so sorting is usually
// very close to a no-op, in which case one pass is the fastest.
static void sort_pulse_widths(void) {
  uint8_t i;
  bool changed;
  do {
    i = (N_LEDS - 1);
    changed = 0;
    do {
      if (pulse_widths[i].pulse_width < pulse_widths[i - 1].pulse_width) {
        changed = 1;
        led const tmp = pulse_widths[i];
        pulse_widths[i] = pulse_widths[i - 1];
        pulse_widths[i - 1] = tmp;
      }
    } while (--i); // this intentionally omits (i = 0)
  } while (changed);

  // Check that this worked:
  /*
  do {
    if (pulse_widths[i].pulse_width > pulse_widths[i + 1].pulse_width) {
      // stop the program:
      do {
      } while (1);
    }
  } while ((++i) < (N_LEDS - 1));
  */
}

static void recalculate_pulse_widths(void) {
  float const percent_of_cycle = (((float)cycle_count) / PERIOD_IN_CYCLES);
  for (uint8_t i = 0; i < N_LEDS; ++i) {
    pulse_widths[i].pulse_width =
        (PLUS_MINUS_MIN +
         (uint16_t)(PLUS_MINUS_RANGE *
                    luminosity(percent_of_cycle, pulse_widths[i].id)));
  }
  sort_pulse_widths();
}

static void init_pulse_widths(void) {
  for (uint8_t id = 0; id < N_LEDS; ++id) {
    pulse_widths[id].id = id;
  }
  recalculate_pulse_widths();
}

typedef struct pin {
  register8_t *unmasked;
  uint8_t on_mask;
  uint8_t off_mask;
} pin;

inline static void pin_on(pin const *const p) {
  (*(p->unmasked)) |= (p->on_mask);
}
inline static void pin_off(pin const *const p) {
  (*(p->unmasked)) &= (p->off_mask);
}

static pin PINS[N_LEDS];

static void init_pins(void) {
  for (uint8_t id = 0; id < N_LEDS; ++id) {
    uint8_t const digit = (PIN_NAMES[id][1] - '0');
    uint8_t const mask = (1U << digit);
    PINS[id].on_mask = mask;
    PINS[id].off_mask = ~mask;

    switch (PIN_NAMES[id][0]) {
    case 'A':
      PORTA.DIR |= mask;
      PINS[id].unmasked = &PORTA.OUT;
      break;
    case 'B':
      PORTB.DIR |= mask;
      PINS[id].unmasked = &PORTB.OUT;
      break;
    case 'C':
      PORTC.DIR |= mask;
      PINS[id].unmasked = &PORTC.OUT;
      break;
    case 'D':
      PORTD.DIR |= mask;
      PINS[id].unmasked = &PORTD.OUT;
      break;
    case 'E':
      PORTE.DIR |= mask;
      PINS[id].unmasked = &PORTE.OUT;
      break;
    case 'F':
      PORTF.DIR |= mask;
      PINS[id].unmasked = &PORTF.OUT;
      break;
    default:
      break;
    }

    pin_off(&(PINS[id]));
  }
}

// Subroutine to initialize the clock:
inline static void TCA0_init(void) {

  // Set the timer's period to 50ms:
  TCA0.SINGLE.PERBUF = PERIOD;

  // Run at the usual clock frequency (disable the prescaler):
  TCA0.SINGLE.CTRLA = TCA_SINGLE_CLKSEL_DIV1_gc;

  // Turn it on!
  TCA0.SINGLE.CTRLA |= TCA_SINGLE_ENABLE_bm;
}

// A specific two-byte region on the microcontroller holds the timer's count:
inline static uint16_t clock(void) { return TCA0.SINGLE.CNT; }

int main(void) {

  // Reset the cycle count:
  cycle_count = 0;

  // Initialize pulse-width array:
  init_pulse_widths();

  // Disable interrupts (temporarily):
  cli();

  // Initialize output pins:
  init_pins();

  // Turn PWM on:
  TCA0_init();

  // Enable interrupts
  sei();

  uint8_t i = (N_LEDS - 1);
  uint16_t moment;

  // Nothing else to do, so timing can use busy-waiting for readability:
  do {

    // Start with the longest pulse width (i.e. keep `i` at `N_LEDS - 1`),
    // since it will start first:
    do {
      moment = (HALF_PERIOD - (pulse_widths[i].pulse_width));
      do { // nothing
      } while (TCA0.SINGLE.CNT < moment);
      pin_on(&(PINS[pulse_widths[i].id]));
    } while (i--); // down to, and including, (i = 0)

    // Then start with the shortest pulse width (i.e. keep `i` at 0),
    // since it will end first:
    do {
      moment = (HALF_PERIOD + (pulse_widths[i].pulse_width));
      do { // nothing
      } while (TCA0.SINGLE.CNT < moment);
      pin_off(&(PINS[pulse_widths[i].id]));
      if (i == (N_LEDS - 1)) {
        break;
      }
      ++i;
    } while (1);

    // Update cycle count & reset if we've finished a set:
    if (PERIOD_IN_CYCLES == ++cycle_count) {
      cycle_count = 0;
    }

    // Recalculate luminosities for the next cycle:
    recalculate_pulse_widths();

    // And start over!
  } while (1);
}
