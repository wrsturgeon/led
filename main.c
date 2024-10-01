// Will Sturgeon for Spectral Motion, Inc.

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%% READ THIS FIRST:
//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

// The signal for servo number [N] is sent through pin D[N].
// For example, servo number 3 should get its signal from pin D3,
// which is labelled PD3 in the pinout below:
// https://docs.arduino.cc/resources/pinouts/ABX00028-full-pinout.pdf

// If you'd like to edit settings like beats per minute or servo range,
// look past the short import section (which has to stay at the top)
// to the next section, labelled "SETTINGS."

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%% Import some information about the board:
//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

#include <avr/interrupt.h>
#include <avr/io.h>

#include <math.h>
#include <stdint.h>

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%% SETTINGS:
//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

// Beats per minute:
#define BPM 45

// How many servos are we using?
#define N_SERVOS 3

// Minimum pulse percent (/100):
#define MIN_PULSE_PERCENT 0

// Maximum pulse percent (/100):
#define MAX_PULSE_PERCENT 50

// How far into the last servo's cycle should the next servo's cycle start?
#define PULSE_STAGGER 0.075

// How sharp should the heart's movements be?
// (i.e. delineated "beats" versus smooth oscillation)
#define PULSE_STRENGTH 5

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%% Below this line is complex: tread carefully!
//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

#if N_SERVOS > 8
#error Only 8 servos (or fewer) are supported, since there are only 8 pins in port D
#endif

#if MIN_PULSE_PERCENT < 0
#error Minimum pulse percentage is less than 0
#endif

#if MAX_PULSE_PERCENT < 0
#error Maximum pulse percentage is less than 0
#endif

#if MIN_PULSE_PERCENT > 100
#error Minimum pulse percentage is more than 100
#endif

#if MAX_PULSE_PERCENT > 100
#error Maximum pulse percentage is more than 100
#endif

#if MAX_PULSE_PERCENT < MIN_PULSE_PERCENT
#error Maximum pulse percent is less than minimum pulse percent
#endif

#if PULSE_STRENGTH > 10
#error `PULSE_STRENGTH` is way too high! Each +1 in `PULSE_STRENGTH` doubles actual strength!
#endif

// Convert beats per minute to a heartbeat period in terms of servo pulses:
#define PERIOD_CYCLES ((60ULL * 50ULL) / (unsigned long long)(BPM))

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
 *   - We want the period to be 20ms (i.e. we want the frequency to be 50Hz):
 *     - f_CLK_PER / N(PER + 1) = 50Hz
 *     - 2,666,666Hz / N(PER + 1) = 50Hz
 *     - 2,666,666 / 50 = N(PER + 1)
 *     - 2,666,666 / 50 = N(PER + 1)
 *     - 53,333.3... = N(PER + 1)
 *   - We want `PER` to be as large as possible, so
 *     if we pin `PER` to its max value of 0xFFFF,
 *     we should be just above 53,333.3... total.
 *     - 53,333.3... <= N(0xFFFF + 1)
 *     - 53,333.3... <= 65,536N
 *     - 0.813802... <= N
 *   - Okay, so that's not possible, but N=1 is close enough
 *   - Now we can calculate `PER`:
 *     - 53,333.3... = PER + 1
 *     - 53,332.3... = PER
 *     - 53,332 ~= PER
 */
#define PERIOD (53332ULL)                      // 20ms ~= 53,332
#define MILLISECOND ((PERIOD + 10ULL) / 20ULL) //  1ms ~=  2,667
#define MIN_PULSE                                                              \
  ((MILLISECOND * (unsigned long long)(MIN_PULSE_PERCENT)) / 100ULL)
#define MAX_PULSE                                                              \
  ((MILLISECOND * (unsigned long long)(MAX_PULSE_PERCENT)) / 100ULL)
#define PULSE_RANGE ((uint32_t)(MAX_PULSE - MIN_PULSE))

#define FLOAT_PI (3.14159265359F)

/*
 * Add an extra 1/8 of a millisecond before each pulse
 * (even with 8 servos, we'd have 8(1/8ms + 2ms) = 17ms,
 * so still worst-case 3ms left for calculations, which is plenty):
 */
#define BETWEEN_PULSES ((MILLISECOND << 1ULL) + (MILLISECOND >> 3ULL))

// Subroutine to initialize the clock:
inline static void TCA0_init(void) {

  // Set the timer's period to 50ms:
  TCA0.SINGLE.PERBUF = PERIOD;

  // Run at the usual clock frequency (disable the prescaler):
  TCA0.SINGLE.CTRLA = TCA_SINGLE_CLKSEL_DIV1_gc;

  // Turn it on!
  TCA0.SINGLE.CTRLA |= TCA_SINGLE_ENABLE_bm;
}

// Subroutine to enable output pins:
inline static void PORT_init(void) {
  for (uint8_t i = 0; i < N_SERVOS; ++i) {
    PORTD.DIR |= (1U << i); // Enable pin #[i] by setting is DIRection to OUT
  }
}

// A specific two-byte region on the microcontroller holds the timer's count:
inline static uint16_t clock(void) { return TCA0.SINGLE.CNT; }

// Define variables before we use them to save stack instructions:
static uint8_t cycle_count, pin_mask;
static uint16_t pulse_center, actual_width, moment;
static uint16_t extra_clocks[N_SERVOS];
static float angle;

// This is going to be used twice, so we can save memory and
// make sure it's the same behavior with a function:
static void update_positions(void) {
  angle = (cycle_count * (FLOAT_PI / PERIOD_CYCLES)); // [0, pi)
  for (uint8_t i = 0; i < N_SERVOS; ++i) {

    // Map angle to the first half of `sin` (0-pi, the nonnegative half, only):
    float pos = sinf(angle); // [0, 1] (since we use only half the cycle)

    // Take `pos` to the power of a power of two
    // by repeatedly multiplying it with itself:
    for (uint8_t i = 0; i < PULSE_STRENGTH; ++i) {
      pos *= pos;
    }

    // Map to the full range and implicitly convert to an integer:
    extra_clocks[i] = PULSE_RANGE * pos;

    // Stagger the angle for the next servo:
    angle += (FLOAT_PI * PULSE_STAGGER);
  }
}

int main(void) {

  // Initialize the board:
  cli();       // Disable interrupts (temporarily)
  PORT_init(); // Enable pins
  TCA0_init(); // Turn PWM on
  sei();       // Enable interrupts

  // Reset the cycle count:
  cycle_count = 0;

  // Set servo positions for the first time:
  update_positions();

  // Nothing else to do, so timing can use busy-waiting for readability:
  do {

    // Reset our stopwatch:
    pulse_center = (uint16_t)(-MILLISECOND);
    // and subtract a millisecond to account for
    // the extra millisecond in `BETWEEN_PULSES`

    // Loop over each servo, all in under 20ms total:
    for (uint8_t i = 0; i < N_SERVOS; ++i) {

      // Add 2ms plus a buffer for computation, interrupts, whatnot:
      pulse_center += BETWEEN_PULSES;

      // Figure out which pin to power:
      pin_mask = (1U << i);

      // Now, figure out the actual pulse width in clock cycles (cont'd):

      // First, one millisecond is the absolute minimum,
      // plus the user-defined minimum on top of that:
      actual_width = (MILLISECOND + MIN_PULSE);

      // Then the range-adjusted position:
      actual_width += (extra_clocks[i]);

      // Now, we have to figure out where to start the pulse,
      // which should be *half* the pulse width *before* the center:
      moment = (pulse_center - (actual_width >> 1));

      // Wait until that moment:
      do {
      } while (clock() < moment);

      // Send power to the corresponding pin:
      PORTD.OUT |= pin_mask;

      // Figure out when to end the pulse (easy, since we know the width):
      moment += actual_width;

      // Invert the pin mask (so we're turning it off instead of turning it on):
      pin_mask = (~pin_mask);

      // Wait until that moment:
      do {
      } while (clock() < moment);

      // Cut power from the corresponding pin:
      PORTD.OUT &= pin_mask;

      // Move on to the next servo (back up top):
    }

    // Update cycle count & reset if we've finished a set:
    if (PERIOD_CYCLES == ++cycle_count) {
      cycle_count = 0;
    }

    // Update servo positions:
    update_positions();

    // Wait for the next cycle:
    do {
    } while (clock() > moment);

    // And start over!
  } while (1);
}
