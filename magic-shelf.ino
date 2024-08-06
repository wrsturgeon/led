// Arduino Nano Every

static constexpr uint8_t INPUT_PIN = 20;
static constexpr uint8_t OUTPUT_PIN = 21;
static constexpr uint16_t CENTER = 500; // 500 (centered) for full range
static constexpr uint16_t RANGE = 500; // 500 (+/-) for full range
static constexpr uint32_t OPEN_US = 1000000;

static constexpr uint16_t PERIOD_US = 20000;
static constexpr uint16_t MIN_PULSE = 1000;
static constexpr uint16_t MAX_PULSE = 2000;
static constexpr uint16_t HALF_MIN_PULSE = (MIN_PULSE >> 1);
static constexpr uint8_t LG_STEPS = 12;
static constexpr uint32_t MAX_STEPS = static_cast<uint32_t>(static_cast<uint32_t>(1) << LG_STEPS);
static constexpr uint32_t SIGN_BIT = static_cast<uint32_t>(static_cast<uint32_t>(1) << 31);
static constexpr float MULTIPLIER = PI / OPEN_US;

static_assert(static_cast<signed long>(CENTER) >= 0);
static_assert(static_cast<signed long>(CENTER) <= (MAX_PULSE - MIN_PULSE));
static_assert(static_cast<signed long>(static_cast<signed long>(CENTER) - RANGE) >= 0);
static_assert(static_cast<signed long>(static_cast<signed long>(CENTER) + RANGE) <= (MAX_PULSE - MIN_PULSE));

static uint32_t pos, start, dus;
static uint32_t center_us, us;
static int32_t tmp;
static float f, fus;
static bool open;

void setup() noexcept {
  pinMode(INPUT_PIN, INPUT_PULLUP);
  pinMode(OUTPUT_PIN, OUTPUT);
  pos = 0;
  open = false;
  center_us = micros();
}

void send_pulse() noexcept {
  center_us += PERIOD_US;
  fus = f;
  fus *= RANGE;
  dus = fus; // type conversion
  dus += HALF_MIN_PULSE;

  noInterrupts();

  // Wait to start the pulse:
  us = center_us;
  us -= dus;
  do {
    tmp = micros();
    tmp -= us;
  } while (tmp & SIGN_BIT);
  digitalWrite(OUTPUT_PIN, HIGH);

  // Wait to end the pulse:
  us = center_us;
  us += dus;
  do {
    tmp = micros();
    tmp -= us;
  } while (tmp & SIGN_BIT);
  digitalWrite(OUTPUT_PIN, LOW);

  interrupts();
}

void loop() noexcept {

  // Wait for the button to be pressed:
  do { send_pulse(); } while (digitalRead(INPUT_PIN)); // Wait for pin to go LOW (!!!)
  start = micros(); // Record the time we started moving
  open = !open; // Toggle which direction we're moving

  // Keep pinging the servo until we've moved all the way over:
  do {
    pos = micros();
    pos -= start;
    if (pos >= OPEN_US) {
      if (open) { f = 1.f; } else { f = 0.f; }
      break;
    }

    f = pos; // type conversion
    f *= MULTIPLIER;
    f = cosf(f); // 0% |-> 1.; 100% |-> 0.
    f += 1.f;
    f *= 0.5f;
    if (open) { f = 1.f - f; }

    send_pulse();
  } while (true);
}
