/*
 * driving_behav_analys.c
 *
 *  Created on: Nov 25, 2017
 *      Author: tony
 */

#include "driving_behav_analys.h"

TimerFlag timer_flag = {0x1f};


/* callback function for timer, if timeout happened this function will be executed */
void timeout_execute_activity(TimerFlag* timer_flag, TimerEventType timer_event_type)
{
	struct timeval cur_time;
	gettimeofday(&cur_time, NULL);
	printf("function: %s, now second: %ld, timer_flag->timer_val: %d\n", \
			__func__, cur_time.tv_sec, timer_flag->timer_val);

	switch(timer_event_type)
	{
	case engine_start_after_15min:
		timer_flag->bits.engine_start_timer_stat = 0;
		timer_flag->bits.engine_start_afer_15min_flag = 0;
		break;

	case brake_active_after_20s:
		timer_flag->bits.brake_active_timer_stat = 0;
		timer_flag->bits.brake_active_after_20s_flag = 0;
		break;

	case driver_door_close_after_15min:
		timer_flag->bits.driver_door_close_timer_stat = 0;
		timer_flag->bits.driver_door_close_after_15min_flag = 0;
		break;

	case turning_light_active_after_20s:
		timer_flag->bits.turning_light_active_timer_stat = 0;
		timer_flag->bits.turning_light_active_after_20s_flag = 0;
		break;

	case accelerator_active_after_20s:
		timer_flag->bits.accelerator_active_timer_stat = 0;
		timer_flag->bits.accelerator_active_after_20s_flag = 0;
		break;
	}

	printf("function: %s, timer_flag->timer_val: %d\n", \
			__func__,  timer_flag->timer_val);
}
