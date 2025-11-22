#ifndef SENSOR_FSM_H
#define SENSOR_FSM_H

#include <zephyr/kernel.h>
#include "common.h"

typedef enum {
	SENSOR_IDLE,
	SENSOR_ACTIVE
} sensor_state_t;

typedef struct {
	sensor_state_t state;
	int64_t start_time;
	int64_t end_time;
	uint32_t axle_count;
	bool speed_measured;
} sensor_fsm_t;

/**
 * Classifies the vehicle type based on the number of axles.
 * @param axle_count The number of axles.
 * @return The vehicle type.
 */
static inline vehicle_type_t classify_axles(uint32_t axle_count)
{
	return (axle_count <= 2) ? VEHICLE_LIGHT : VEHICLE_HEAVY;
}

/**
 * Initializes the sensor FSM.
 * @param fsm Pointer to the sensor FSM.
 */
static inline void sensor_fsm_init(sensor_fsm_t *fsm)
{
	fsm->state = SENSOR_IDLE;
	fsm->start_time = 0;
	fsm->end_time = 0;
	fsm->axle_count = 0;
	fsm->speed_measured = false;
}

/**
 * Handles the start of a sensor measurement.
 * @param fsm Pointer to the sensor FSM.
 * @param timestamp_ms The timestamp of the start of the measurement.
 */
static inline void sensor_fsm_handle_start(sensor_fsm_t *fsm, int64_t timestamp_ms)
{
	// If the sensor is idle, set the sensor to active and set the start time, end time, axle count, and speed measured flag
	if (fsm->state == SENSOR_IDLE) {
		fsm->state = SENSOR_ACTIVE;
		fsm->start_time = timestamp_ms;
		fsm->end_time = 0;
		fsm->axle_count = 1;
		fsm->speed_measured = false;
	} else {
		fsm->axle_count++;
	}
}

/**
 * Handles the end of a sensor measurement.
 * @param fsm Pointer to the sensor FSM.
 * @param timestamp_ms The timestamp of the end of the measurement.
 */
static inline void sensor_fsm_handle_end(sensor_fsm_t *fsm, int64_t timestamp_ms)
{
	// If the sensor is active and the speed has not been measured, set the end time and the speed measured flag
	if (fsm->state == SENSOR_ACTIVE && !fsm->speed_measured) {
		fsm->end_time = timestamp_ms;
		fsm->speed_measured = true;
	}
}

/**
 * Finalizes the sensor measurement.
 * @param fsm Pointer to the sensor FSM.
 * @param out_data Pointer to the sensor data.
 * @return True if the measurement was finalized, false otherwise.
 */
static inline bool sensor_fsm_finalize(sensor_fsm_t *fsm, sensor_data_t *out_data)
{

	if (fsm->state != SENSOR_ACTIVE) {
		return false;
	}

	bool produced = false;
	// If the speed has been measured and the end time is greater than the start time, 
	// set the sensor data
	if (fsm->speed_measured && fsm->end_time > fsm->start_time) {
		out_data->timestamp_start = fsm->start_time;
		out_data->timestamp_end = fsm->end_time;
		out_data->duration_ms = (uint32_t)(fsm->end_time - fsm->start_time);
		out_data->axle_count = fsm->axle_count;
		out_data->type = classify_axles(fsm->axle_count);
		produced = true;
	}

	/* Reset to idle regardless, end of measurement window */
	fsm->state = SENSOR_IDLE;
	fsm->start_time = 0;
	fsm->end_time = 0;
	fsm->axle_count = 0;
	fsm->speed_measured = false;
	return produced;
}

#endif


