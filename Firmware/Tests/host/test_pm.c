#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "pm_sleep_timer.h"
static uint32_t now;
uint32_t TIME_Millis(void){return now;}
int main(void)
{
    now=0u;pm_sleep_timer_init(60000u);
    now=20000u;pm_sleep_timer_pause();now=620000u;
    assert(pm_sleep_timer_left_ms()==40000u);
    assert(!pm_sleep_timer_expired());
    pm_sleep_timer_resume();now+=40000u;assert(pm_sleep_timer_expired());
    pm_sleep_timer_refresh();pm_sleep_timer_pause();now+=100000u;
    pm_sleep_timer_refresh();assert(pm_sleep_timer_left_ms()==60000u);
    pm_sleep_timer_set(30000u);assert(pm_sleep_timer_left_ms()==30000u);
    pm_sleep_timer_resume();now+=30000u;assert(pm_sleep_timer_expired());
    pm_sleep_timer_disable();assert(!pm_sleep_timer_expired());
    assert(pm_sleep_timer_left_ms()==PM_SLEEP_INFINITE);
    pm_sleep_timer_force_expire();assert(pm_sleep_timer_expired());
    pm_sleep_timer_clear_force();assert(!pm_sleep_timer_expired());
    now=0xfffffff0u;pm_sleep_timer_init(100u);now=0x20u;
    assert(pm_sleep_timer_left_ms()==52u);pm_sleep_timer_pause();
    now=0xfffffff0u;assert(pm_sleep_timer_left_ms()==52u);
    pm_sleep_timer_resume();now=0x24u;assert(pm_sleep_timer_expired());
    puts("PM timer PASS");return 0;
}
