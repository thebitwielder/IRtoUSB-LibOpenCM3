#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/timer.h>

/*
 * IR NEC Protocol Decoder — Standalone Test
 * 
 * Hardware:
 *   TFMS 5380 OUT  → PA0
 *   TFMS 5380 VCC  → 3.3V
 *   TFMS 5380 GND  → GND
 *   Onboard LED    → PC13 (active low)
 *
 * NEC Protocol:
 *   Leader:  9000µs LOW + 4500µs HIGH
 *   Bit '0':  562µs LOW +  562µs HIGH
 *   Bit '1':  562µs LOW + 1687µs HIGH
 *   32 bits total, sent LSB first.
 */

// ---------- Pin Definitions ----------
#define IR_PORT     GPIOA
#define IR_PIN      GPIO0

#define LED_PORT    GPIOC
#define LED_PIN     GPIO13

// ---------- NEC Timing (microseconds) ----------
#define NEC_LEADER_LOW_MIN   8000
#define NEC_LEADER_LOW_MAX  10000
#define NEC_LEADER_HIGH_MIN  3500
#define NEC_LEADER_HIGH_MAX  5500
#define NEC_BIT_THRESHOLD    1100   // If HIGH pulse > this, it's a '1'
#define NEC_TIMEOUT         50000   // Give up after 50ms of no signal

// ---------- Clock Setup ----------
static void clock_setup(void) {
    rcc_clock_setup_pll(&rcc_hse_configs[RCC_CLOCK_HSE8_72MHZ]);
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_GPIOC);
    rcc_periph_clock_enable(RCC_TIM2);
}

// ---------- LED Setup ----------
static void led_setup(void) {
    gpio_set_mode(LED_PORT, GPIO_MODE_OUTPUT_2_MHZ,
                  GPIO_CNF_OUTPUT_PUSHPULL, LED_PIN);
    gpio_set(LED_PORT, LED_PIN); // LED off (active low)
}

// ---------- IR GPIO Setup ----------
static void ir_gpio_setup(void) {
    // PA0 as floating input — the TFMS 5380 drives the line itself
    gpio_set_mode(IR_PORT, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, IR_PIN);
}

// ---------- Timer Setup (1µs resolution) ----------
static void ir_timer_setup(void) {
    // TIM2 runs at 72MHz. Prescaler of 71 → 1MHz → 1 tick = 1µs
    timer_set_prescaler(TIM2, 71);
    timer_set_period(TIM2, 0xFFFF);  // 16-bit, wraps every ~65ms
    timer_enable_counter(TIM2);
}

static uint16_t timer_now(void) {
    return timer_get_counter(TIM2);
}

// ---------- Simple Delay ----------
static void delay_ms(uint32_t ms) {
    // Rough delay at 72MHz. ~7200 nop's ≈ 1ms (not exact, good enough for LED)
    for (uint32_t i = 0; i < ms * 7200; i++) {
        __asm__("nop");
    }
}

// ---------- Wait for a specific pin level, return elapsed µs ----------
// Returns 0 on timeout.
static uint16_t wait_level(uint8_t level, uint32_t timeout_us) {
    uint16_t start = timer_now();

    while (gpio_get(IR_PORT, IR_PIN) != level) {
        uint16_t elapsed = timer_now() - start;
        if (elapsed > timeout_us) {
            return 0; // Timed out
        }
    }

    uint16_t end = timer_now();
    return end - start; // Works correctly even if timer wraps (unsigned math)
}

// ---------- Read one NEC frame ----------
// Returns the 32-bit code, or 0 if nothing / error.
static uint32_t ir_read_nec(void) {
    // 1. Wait for the line to go LOW (start of leader pulse)
    //    If it's already HIGH (idle), we wait for the falling edge.
    if (gpio_get(IR_PORT, IR_PIN) != 0) {
        // Currently HIGH (idle). Wait for it to go LOW.
        if (wait_level(0, NEC_TIMEOUT) == 0) {
            return 0; // Nothing happened, no remote pressed
        }
    }

    // 2. Measure the 9ms LOW leader pulse
    uint16_t low_time = wait_level(1, NEC_TIMEOUT);
    if (low_time < NEC_LEADER_LOW_MIN || low_time > NEC_LEADER_LOW_MAX) {
        return 0; // Not a valid leader
    }

    // 3. Measure the 4.5ms HIGH leader pulse
    uint16_t high_time = wait_level(0, NEC_TIMEOUT);
    if (high_time < NEC_LEADER_HIGH_MIN || high_time > NEC_LEADER_HIGH_MAX) {
        return 0; // Not a valid leader
    }

    // 4. Read 32 data bits
    uint32_t code = 0;
    for (int i = 0; i < 32; i++) {
        // Each bit starts with a ~562µs LOW pulse
        uint16_t bit_low = wait_level(1, NEC_TIMEOUT);
        if (bit_low == 0) return 0;

        // Then a HIGH pulse: short = 0, long = 1
        uint16_t bit_high = wait_level(0, NEC_TIMEOUT);
        if (bit_high == 0) return 0;

        // Shift in the bit (LSB first)
        if (bit_high > NEC_BIT_THRESHOLD) {
            code |= (1UL << i); // It's a '1'
        }
        // else it's a '0', bit stays 0
    }

    return code;
}

// ---------- Blink LED to show success ----------
static void blink_success(void) {
    for (int i = 0; i < 3; i++) {
        gpio_clear(LED_PORT, LED_PIN); // LED ON (active low)
        delay_ms(100);
        gpio_set(LED_PORT, LED_PIN);   // LED OFF
        delay_ms(100);
    }
}

// ---------- Main ----------
int main(void) {
    clock_setup();
    led_setup();
    ir_gpio_setup();
    ir_timer_setup();

    // Storage for the last decoded IR code
    uint32_t last_code = 0;

    while (1) {
        uint32_t code = ir_read_nec();

        if (code != 0 && code != last_code) {
            last_code = code;
            blink_success(); // Visual feedback!
            
            // Later we'll send this code over USB as a keystroke.
            // For now, just blink the LED.
        }
    }

    return 0;
}