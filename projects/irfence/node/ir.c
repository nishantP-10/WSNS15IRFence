/******************************************************************************
*  This program is used to perform basic functions on the IR LEDS 
*  and monitor the recievers
*******************************************************************************/

#include <nrk.h>
#include <nrk_error.h>
#include <include.h>
#include <stdlib.h>
#include <avr/interrupt.h>
#include <ulib.h>
#include <nrk_error.h>
#include <nrk_timer.h>
#include <nrk_ext_int.h>

#include "cfg.h"
#include "output.h"

#include "ir.h"

#undef LOG_CATEGORY
#define LOG_CATEGORY LOG_CATEGORY_IR

/* PC_INT_0 (SSN) pin is not usable in Firefly v3 because it is connected to
 * the reset pin, which looks like a circuit mistake. The workaround is
 * to use EXT_INT_3 (TX1 pin). */

#define PWM_TIMER_PIN NRK_PORTE_3

#define SYS_CLOCK_KHZ 16000

/* Signal raised whenever a change in receiver signal is detected */
nrk_sig_t ir_rcv_signal;

static nrk_task_type MONITOR_TASK;
static NRK_STK monitor_task_stack[STACKSIZE_IR];

static uint8_t pulse_tick = 0;
static uint16_t pulse_on_ticks;

static uint8_t armed = 0;

/* Poor man's event (signaling an event from ISR crashes)*/
static bool rcv_state_changed = false;

static void set_pwm_freq(uint8_t freq_khz)
{
    /* f = Fclk/(2*N*TOP)
     * TOP = Fclk / (f*2*N)
     */
    uint16_t period = SYS_CLOCK_KHZ / (freq_khz * 2);
    ICR3 = period;
    OCR3A = period / 2; /* TODO: make a duty cycle config param */
}

static void configure_pwm_timer()
{
  // Setup PWM Timer timer with:
  //       Prescaler = 1
  //       Compare Match = 209
  //       Sys Clock = 16 MHz
  // Prescaler 1 means divide sys clock by 1
  // 16000000 / 1 = 16000000 MHz clock
  // 1 / 16000000 = 0.0625 us per tick
  // f=38kHz; fclk=16MHz; Prescalar(N)=1;
  // TOP= 210~209(Freq calculation more accurate)  ICR3=209 and OCR3A=TOP/2=105	
  // TCCR3A=0x80 Setting output Level to low on compare match for PWM  
  // TCCR3B 0x11 0x01: for selecting the clock based on prescalars   0x10 for Waveform Generation PWM 8bit phase correct

    nrk_gpio_direction(PWM_TIMER_PIN, NRK_PIN_OUTPUT);
    nrk_gpio_clr(PWM_TIMER_PIN);

    set_pwm_freq(ir_carrier_freq_khz);

    /* Check that nobody messed with the timer since restart */
    ASSERT(TCCR3A == 0 && TCCR3B == 0);

    TCCR3B |= BM(WGM33);  /* phase and freq correct PWM mode */
}

static inline void start_pwm_timer()
{
    TCCR3A |= BM(COM3A1); /* non-inverted pwm output */
    TCNT3 = 0;
    TCCR3B |= BM(CS30);
}

static void stop_pwm_timer()
{
    TCCR3B &= ~(BM(CS32) | BM(CS31) | BM(CS30));
    TCCR3A = 0; /* revert pin to GPIO mode */
}

static void configure_pulse_timer()
{
    // Setup application timer with:
    //       Prescaler = 1
    //       Compare Match = 8320
    //       Sys Clock = 16 MHz
    // Prescaler 1 means divide sys clock by 1
    // 16000000 / 1 = 16000000 MHz clock
    // 1 / 16000000 = 0.0625 us per tick
    // 0.0625 us * 8320 = ~520us / per interrupt callback

    uint16_t period = 8320; // this will give a 20*38kHZ timer int

    ASSERT(100 % ir_pulse_ticks == 0);
    ASSERT(ir_pulse_duty_cycle % (100 / ir_pulse_ticks) == 0);
    pulse_on_ticks = ir_pulse_duty_cycle * ir_pulse_ticks / 100;
    pulse_tick = 0;

    ASSERT(TCCR4A == 0 && TCCR4B == 0); /* nobody messed with this timer */

    TCCR4B |= BM(WGM43) | BM(WGM42); /* CTC with TOP = ICR */
    ICR4 = period;
    TIMSK4 |= BM(OCIE4A);
}

static inline void start_pulse_timer()
{
    TCNT4 = 0;
    TCCR4B |= BM(CS40);
}

static inline void stop_pulse_timer()
{
    TCCR4B &= ~(BM(CS42) | BM(CS41) | BM(CS40));
}

static inline void select_led(uint8_t led)
{
    PORTF &= ~0x0F;
    PORTF |= led & 0x0F;
}

uint8_t ir_rcv_state(uint8_t rcvers)
{
    uint8_t state = 0;
    if (rcvers & (1 << 0))
        state |= !nrk_gpio_get(NRK_PORTB_1) << 0;
    if (rcvers & (1 << 1))
        state |= !nrk_gpio_get(NRK_PORTB_2) << 1;
    if (rcvers & (1 << 2))
        state |= !nrk_gpio_get(NRK_PORTB_3) << 2;
    if (rcvers & (1 << 3))
        state |= !nrk_gpio_get(NRK_PORTD_3) << 3;
    return state;
}

void ir_led_on(uint8_t led)
{
    LOG("led on: ");
    LOGP("%d\r\n", led);
    select_led(led);
    start_pulse_timer();
}

void ir_led_off()
{
    LOG("led off\r\n");
    stop_pulse_timer();
    stop_pwm_timer();
}

void ir_arm(uint8_t rcvers)
{
    LOG("arm receivers: ");
    LOGP("%d\r\n", rcvers);

    armed |= rcvers;

    /* TODO: turn on hardware */
}

void ir_disarm(uint8_t rcvers)
{
    LOG("disarm receivers: ");
    LOGP("%d\r\n", rcvers);

    armed &= ~rcvers;

    /* TODO: shut down hardware */
}

int8_t cmd_irled(uint8_t argc, char **argv)
{
    uint8_t led;

    if (!(argc == 1 || argc == 2)) {
        OUT("usage: irled [<led>] \r\n");
        return NRK_ERROR;
    }

    if (argc == 2) {
        led = atoi(argv[1]);
        ir_led_on(led);
    } else {
        ir_led_off();
    }

    return NRK_OK;
}

int8_t cmd_irrcv(uint8_t argc, char **argv)
{
    uint8_t rcver;
    bool state;

    if (!(argc == 1 || argc == 2)) {
        OUT("usage: irrcv [<rcver>]\r\n");
        return NRK_ERROR;
    }

    if (argc == 2) {
        rcver = atoi(argv[1]);

        state = ir_rcv_state(1 << rcver);
        OUT("rcver state: ");
        OUTP("%d", state);
    } else {
        OUT("rcver signals: ");
        for (rcver = 0; rcver < NUM_IR_RECEIVERS; ++rcver) {
            state = ir_rcv_state(1 << rcver);
            OUTP("%d", state);
        }
        OUT("\r\n");
    }
    return NRK_OK;
}

int8_t cmd_irfreq(uint8_t argc, char **argv)
{
    if (!(argc == 1 || argc == 2)) {
        OUT("usage: irfreq <freq_khz>\r\n");
        return NRK_ERROR;
    }

    if (argc == 2) {
        ir_carrier_freq_khz = atoi(argv[1]);
        set_pwm_freq(ir_carrier_freq_khz);
    } else {
        OUT("freq: "); OUTP("%u", ir_carrier_freq_khz); OUT("\r\n");
    }
    return NRK_OK;
}

static void receiver_isr()
{
    /* Can't call nrk_event_signal() from the ISR: the function
     * is too long. TODO: maybe it enables interrupts too early. */
    rcv_state_changed = true;
}

static void monitor_task()
{
    int8_t rc;
    uint8_t state;
    uint8_t rcver;

    rc = nrk_signal_register(ir_rcv_signal);
    if (rc != NRK_OK)
        ABORT("failed to register ir rcv signal\r\n");

    while (1) {

        /* Poor man's event: a periodic polling loop */
        if (rcv_state_changed) {
            rcv_state_changed = false;

            state = ir_rcv_state(IR_ALL_RECEIVERS);
            LOG("rcver signals: ");
            for (rcver = 0; rcver < NUM_IR_RECEIVERS; ++rcver)
                LOGP("%d", (bool)(state & (1 << rcver)));
            LOGP("\r\n");

            nrk_event_signal(ir_rcv_signal);
        }

        nrk_wait_until_next_period();
    }
}

uint8_t init_ir(uint8_t priority)
{
    uint8_t num_tasks = 0;

    LOG("init: prio "); LOGP("%u\r\n", priority);

    ir_rcv_signal = nrk_signal_create();
    if (ir_rcv_signal == NRK_ERROR)
        ABORT("failed to create ir rcv signal\r\n");

    /* Receivers */

    nrk_gpio_direction(NRK_PORTB_1, NRK_PIN_INPUT);
    nrk_gpio_direction(NRK_PORTB_2, NRK_PIN_INPUT);
    nrk_gpio_direction(NRK_PORTB_3, NRK_PIN_INPUT);
    nrk_gpio_direction(NRK_PORTD_3, NRK_PIN_INPUT); /* SSN pin workaround */

    /* Enable pull up resistors on receiver interrupt pins */
    /* This is optional: useful when daugher board is disconnected */
    PORTB |= 0xF;
    PORTD |= 1 << 3; /* SSN pin workaround */

    nrk_ext_int_configure(NRK_PC_INT_1, NRK_LEVEL_TRIGGER, receiver_isr);
    nrk_ext_int_configure(NRK_PC_INT_2, NRK_LEVEL_TRIGGER, receiver_isr);
    nrk_ext_int_configure(NRK_PC_INT_3, NRK_LEVEL_TRIGGER, receiver_isr);
    nrk_ext_int_configure(NRK_EXT_INT_3, NRK_LEVEL_TRIGGER, receiver_isr);

    nrk_ext_int_enable(NRK_PC_INT_1);
    nrk_ext_int_enable(NRK_PC_INT_2);
    nrk_ext_int_enable(NRK_PC_INT_3);
    nrk_ext_int_enable(NRK_EXT_INT_3); /* SSN pin workaround */

    /* Transmitters */

    /* De-multiplexer select lines */
    nrk_gpio_direction(NRK_PORTF_0, NRK_PIN_OUTPUT);
    nrk_gpio_direction(NRK_PORTF_1, NRK_PIN_OUTPUT);
    nrk_gpio_direction(NRK_PORTF_2, NRK_PIN_OUTPUT);
    nrk_gpio_direction(NRK_PORTF_3, NRK_PIN_OUTPUT);

    configure_pwm_timer();
    configure_pulse_timer();

    num_tasks++;
    MONITOR_TASK.task = monitor_task;
    MONITOR_TASK.Ptos = (void *) &monitor_task_stack[STACKSIZE_IR - 1];
    MONITOR_TASK.Pbos = (void *) &monitor_task_stack[0];
    MONITOR_TASK.prio = priority;
    MONITOR_TASK.FirstActivation = TRUE;
    MONITOR_TASK.Type = BASIC_TASK;
    MONITOR_TASK.SchType = PREEMPTIVE;
    MONITOR_TASK.period.secs = 0;
    MONITOR_TASK.period.nano_secs = 500 * NANOS_PER_MS;
    MONITOR_TASK.cpu_reserve.secs = 0;
    MONITOR_TASK.cpu_reserve.nano_secs = 0 * NANOS_PER_MS;
    MONITOR_TASK.offset.secs = 0;
    MONITOR_TASK.offset.nano_secs = 0;
    nrk_activate_task (&MONITOR_TASK);


    ASSERT(num_tasks == NUM_TASKS_IR);
    return num_tasks;
}

SIGNAL(TIMER4_COMPA_vect)
{
    if (pulse_tick == 0)
        start_pwm_timer();
    else if (pulse_tick == pulse_on_ticks)
        stop_pwm_timer();
    pulse_tick = (pulse_tick + 1) % ir_pulse_ticks;
}
