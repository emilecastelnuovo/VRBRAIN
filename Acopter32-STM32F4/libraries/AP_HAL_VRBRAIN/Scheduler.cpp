
#include "Scheduler.h"
#include <delay.h>
#include <timer.h>
#include <HardwareTimer.h>
#include <systick.h>
#include <stm32f4xx_pwr.h>

using namespace VRBRAIN;

extern const AP_HAL::HAL& hal;

AP_HAL::Proc VRBRAINScheduler::_failsafe = NULL;
volatile bool VRBRAINScheduler::_timer_suspended = false;
volatile bool VRBRAINScheduler::_timer_event_missed = false;
volatile bool VRBRAINScheduler::_in_timer_proc = false;
AP_HAL::MemberProc VRBRAINScheduler::_timer_proc[VRBRAIN_SCHEDULER_MAX_TIMER_PROCS] = {NULL};
uint8_t VRBRAINScheduler::_num_timer_procs = 0;
uint64_t VRBRAINScheduler::_scheduler_last_call = 0;
uint16_t VRBRAINScheduler::_scheduler_led = 0;

VRBRAINScheduler::VRBRAINScheduler()
:
	    _delay_cb(NULL),
	    _min_delay_cb_ms(65535),
	    _initialized(false)
{}

void VRBRAINScheduler::init(void* machtnichts)
{

    uint32_t period = (2000000UL / 1000) - 1; // 1000 Hz = 1KHz
    uint32_t prescaler =  (uint16_t) ((SystemCoreClock /2) / 2000000UL) - 1; //2MHz 0.5us ticks

    delay(1000);

    timer_init(TIMER7);
    timer_pause(TIMER7);
    timer_set_prescaler(TIMER7,prescaler);
    timer_set_count(TIMER7,0);
    timer_set_reload(TIMER7,period);
    timer_attach_interrupt(TIMER7, TIMER_UPDATE_INTERRUPT, _timer_isr_event);
    NVIC_SetPriority(TIM7_IRQn,5);
    timer_resume(TIMER7);

    //systick_attach_callback(_timer_isr_event);
}

void VRBRAINScheduler::delay(uint16_t ms)
{
   //"ERROR: delay() from timer process\n
    if (in_timerprocess()) {
        return;
    }
    
    uint64_t start = micros64();
    while((micros64() - start)/1000 < ms) {
	delay_microseconds(1000);
        if (_min_delay_cb_ms <= ms) {
            if (_delay_cb) {
                _delay_cb();
            }
        }
    }


}
uint32_t VRBRAINScheduler::millis() {
    return millis64() & 0xffffffff;
}
uint64_t VRBRAINScheduler::millis64() {
    return systick_uptime();
}

uint32_t VRBRAINScheduler::micros() {
return micros64() & 0xffffffff;
}

uint64_t VRBRAINScheduler::micros64() {
    uint64_t fms, lms;
    uint32_t cycle_cnt;
    uint64_t res;
    do {
        // make sure millis() return the same value before and after
        // getting the systick count
        fms = millis64();
        cycle_cnt = systick_get_count();
        lms = millis64();
    } while (lms != fms);

#define US_PER_MS               1000
    /* SYSTICK_RELOAD_VAL is 1 less than the number of cycles it
       actually takes to complete a SysTick reload */
    res = (fms * US_PER_MS) +
        (SYSTICK_RELOAD_VAL + 1 - cycle_cnt) / CYCLES_PER_MICROSECOND;

    return res;
#undef US_PER_MS
}

void VRBRAINScheduler::delay_microseconds(uint16_t us)
{
    delay_us((uint32_t)us);
}

void VRBRAINScheduler::register_delay_callback(AP_HAL::Proc proc,
            uint16_t min_time_ms)
{
    _delay_cb = proc;
    _min_delay_cb_ms = min_time_ms;
}

void VRBRAINScheduler::register_timer_process(AP_HAL::MemberProc proc)
{
    for (int i = 0; i < _num_timer_procs; i++) {
        if (_timer_proc[i] == proc) {
            return;
        }
    }

    if (_num_timer_procs < VRBRAIN_SCHEDULER_MAX_TIMER_PROCS) {
        /* this write to _timer_proc can be outside the critical section
         * because that memory won't be used until _num_timer_procs is
         * incremented. */
        _timer_proc[_num_timer_procs] = proc;
        /* _num_timer_procs is used from interrupt, and multiple bytes long. */
        noInterrupts();
        _num_timer_procs++;
        interrupts();
    }
}

void VRBRAINScheduler::register_io_process(AP_HAL::MemberProc proc) 
{
    // IO processes not supported on AVR
}

void VRBRAINScheduler::register_timer_failsafe(AP_HAL::Proc failsafe, uint32_t period_us) {
    /* XXX Assert period_us == 1000 */
    _failsafe = failsafe;
}
void VRBRAINScheduler::suspend_timer_procs()
{
    _timer_suspended = true;
}


void VRBRAINScheduler::resume_timer_procs()
{
    _timer_suspended = false;
    if (_timer_event_missed == true) {
        _run_timer_procs(false);
        _timer_event_missed = false;
    }
}

bool VRBRAINScheduler::in_timerprocess()
{
    return _in_timer_proc;
}

#define LED_GRN (*((unsigned long int *) 0x424102B4))
#define LED_YLW (*((unsigned long int *) 0x424102B8))
#define LED_RED (*((unsigned long int *) 0x424102BC))


void VRBRAINScheduler::_timer_isr_event() {

    uint64_t fms = systick_uptime();
/*
    if(fms - _scheduler_last_call >= 100)
	{
	 if (_scheduler_led == 1)
	     {
	     LED_YLW=0;
	     _scheduler_led=0;
	     }
	     else
	     {
             LED_YLW=1;
             _scheduler_led=1;
	     }
	 _scheduler_last_call = fms;
	}
*/
    _run_timer_procs(true);
}

void VRBRAINScheduler::_run_timer_procs(bool called_from_isr) {

    if (_in_timer_proc) {
        return;
    }
    _in_timer_proc = true;

    if (!_timer_suspended) {
        // now call the timer based drivers
        for (int i = 0; i < _num_timer_procs; i++) {
            if (_timer_proc[i] != NULL) {
                _timer_proc[i]();
            }
        }
    } else if (called_from_isr) {
        _timer_event_missed = true;
    }

    // and the failsafe, if one is setup
    if (_failsafe != NULL) {
        _failsafe();
    }

    _in_timer_proc = false;
}



bool VRBRAINScheduler::system_initializing()
{
    return !_initialized;
}

void VRBRAINScheduler::system_initialized()
{
    if (_initialized) {
        panic(PSTR("PANIC: scheduler::system_initialized called"
                   "more than once"));
    }
    _initialized = true;
}

void VRBRAINScheduler::panic(const prog_char_t* errormsg) {
   hal.uartA->println_P(errormsg);
   hal.uartC->println_P(errormsg);
    //for(;;);
}


void VRBRAINScheduler::reboot(bool hold_in_bootloader) {

    if(hold_in_bootloader){
	PWR_BackupAccessCmd(ENABLE);

	/* XXX wow, this is evil - write a magic number into backup register zero */
	*(uint32_t *)0x40002850 = 0xb007b007;
    }

    NVIC_SystemReset();
}
