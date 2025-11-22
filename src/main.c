#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <string.h>
#include <zephyr/sys/atomic.h>
#include "common.h"
#include "threads.h"
#include "infraction_log.h"

LOG_MODULE_REGISTER(main_control, LOG_LEVEL_INF);

K_MSGQ_DEFINE(sensor_msgq, sizeof(sensor_data_t), CONFIG_RADAR_QUEUE_DEPTH, 4); // Message Queue for Sensor Data
K_MSGQ_DEFINE(display_msgq, sizeof(display_data_t), CONFIG_RADAR_QUEUE_DEPTH, 4); // Message Queue for Display Data

// ZBUS Channels
ZBUS_CHAN_DEFINE(camera_trigger_chan, camera_trigger_t, NULL, NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));
ZBUS_CHAN_DEFINE(camera_result_chan, camera_result_t, NULL, NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

// Thread Definitions
K_THREAD_DEFINE(sensor_tid, 2048, sensor_thread_entry, NULL, NULL, NULL, 7, 0, 0);
K_THREAD_DEFINE(display_tid, 2048, display_thread_entry, NULL, NULL, NULL, 7, 0, 0);
K_THREAD_DEFINE(camera_tid, 2048, camera_thread_entry, NULL, NULL, NULL, 7, 0, 0);

// Subscriber for Main Thread
ZBUS_SUBSCRIBER_DEFINE(main_camera_sub, 4);

// Telemetry counters
static atomic_t vehicle_light_count;
static atomic_t vehicle_heavy_count;
static atomic_t status_normal_count;
static atomic_t status_warning_count;
static atomic_t status_infraction_count;

/**
 * Main entry point for the telemetry thread.
 * @param p1 Pointer to the telemetry thread data.
 * @param p2 Pointer to the telemetry thread data.
 * @param p3 Pointer to the telemetry thread data.
 */
static void telemetry_thread_entry(void *p1, void *p2, void *p3)
{
	while (1) {
        // Get the telemetry counters
		k_msleep(CONFIG_RADAR_TELEMETRY_INTERVAL_MS);
		uint32_t light = (uint32_t)atomic_get(&vehicle_light_count);
		uint32_t heavy = (uint32_t)atomic_get(&vehicle_heavy_count);
		uint32_t normal = (uint32_t)atomic_get(&status_normal_count);
		uint32_t warn = (uint32_t)atomic_get(&status_warning_count);
		uint32_t infr = (uint32_t)atomic_get(&status_infraction_count);
		uint32_t inf_light = 0, inf_heavy = 0, valid_reads = 0, invalid_reads = 0;
		infraction_log_get_counters(&inf_light, &inf_heavy, &valid_reads, &invalid_reads);
		LOG_INF("Telemetry: Vehicles [Leve=%u, Pesado=%u] | Status [Normal=%u, Alerta=%u, Infracao=%u] | Camera [Validas=%u, Invalidas=%u]",
			light, heavy, normal, warn, infr, valid_reads, invalid_reads);
	}
}

K_THREAD_DEFINE(telemetry_tid, 1024, telemetry_thread_entry, NULL, NULL, NULL, 8, 0, 0);

typedef struct {
	bool active;
	int64_t timestamp_ms;
	uint32_t speed_kmh;
	uint32_t limit_kmh;
	vehicle_type_t type;
} pending_infraction_t;

// Pending infraction context
static pending_infraction_t pending_infraction_ctx;

int main(void) {
    LOG_INF("Radar System Initializing...");

	// Subscribe to the camera result channel
    zbus_chan_add_obs(&camera_result_chan, &main_camera_sub, K_FOREVER);

    sensor_data_t s_data;
    const struct zbus_channel *chan; // ZBUS channel for camera results

    while (1) {
        // Check for new sensor data
        if (k_msgq_get(&sensor_msgq, &s_data, K_NO_WAIT) == 0) {
            // Calculate Speed
            uint32_t distance_mm = CONFIG_RADAR_SENSOR_DISTANCE_MM;
            uint32_t speed_kmh = calculate_speed(distance_mm, s_data.duration_ms);


            // Determine Limit
            uint32_t limit = (s_data.type == VEHICLE_LIGHT) ? 
                             CONFIG_RADAR_SPEED_LIMIT_LIGHT_KMH : 
                             CONFIG_RADAR_SPEED_LIMIT_HEAVY_KMH;
            
            // Determine Status
            display_status_t status = STATUS_NORMAL;
            if (speed_kmh > limit) {
                status = STATUS_INFRACTION;
            } else {
                uint32_t warning_thr = (limit * CONFIG_RADAR_WARNING_THRESHOLD_PERCENT) / 100;
                if (speed_kmh >= warning_thr) {
                    status = STATUS_WARNING;
                }
            }

            LOG_INF("Speed Calc: %d km/h (Limit: %d). Status: %d", speed_kmh, limit, status);

            // Update Display
            display_data_t d_data;
            d_data.speed_kmh = speed_kmh;
            d_data.limit_kmh = limit;
            d_data.type = s_data.type;
            d_data.status = status;
            d_data.plate[0] = '\0';
            d_data.axle_count = s_data.axle_count;
            d_data.warning_kmh = (limit * CONFIG_RADAR_WARNING_THRESHOLD_PERCENT) / 100;

            // Update telemetry counters
            if (s_data.type == VEHICLE_LIGHT) {
                atomic_inc(&vehicle_light_count);
            } else if (s_data.type == VEHICLE_HEAVY) {
                atomic_inc(&vehicle_heavy_count);
            }
            switch (status) {
                case STATUS_NORMAL: atomic_inc(&status_normal_count); break;
                case STATUS_WARNING: atomic_inc(&status_warning_count); break;
                case STATUS_INFRACTION: atomic_inc(&status_infraction_count); break;
            }
            // Send the display data to the display queue
            {
                int put_ret = k_msgq_put(&display_msgq, &d_data, K_NO_WAIT);
                if (put_ret != 0) {
                    display_data_t dropped;
                    (void)k_msgq_get(&display_msgq, &dropped, K_NO_WAIT);
                    put_ret = k_msgq_put(&display_msgq, &d_data, K_NO_WAIT);
                    if (put_ret != 0) {
                        LOG_WRN("display_msgq full, dropping update");
                    }
                }
            }

            // Trigger Camera if Infraction
            if (status == STATUS_INFRACTION) {
                camera_trigger_t trig;
                trig.speed_kmh = speed_kmh;
                trig.type = s_data.type;
                /* Record pending infraction context */
                pending_infraction_ctx.active = true;
                pending_infraction_ctx.timestamp_ms = k_uptime_get();
                pending_infraction_ctx.speed_kmh = speed_kmh;
                pending_infraction_ctx.limit_kmh = limit;
                pending_infraction_ctx.type = s_data.type;
                int pub_ret = zbus_chan_pub(&camera_trigger_chan, &trig, K_NO_WAIT);
                if (pub_ret != 0) {
                    LOG_WRN("ZBUS publish to camera_trigger_chan failed: %d", pub_ret);
                }
            }
        }

        // Check for Camera Results
        if (zbus_sub_wait(&main_camera_sub, &chan, K_NO_WAIT) == 0) {
			// Check if the channel is the camera result channel
			if (chan == &camera_result_chan) {
                camera_result_t res;
				// Read the camera result
                zbus_chan_read(&camera_result_chan, &res, K_NO_WAIT);
                
                // Check if the plate is valid
                if (res.valid_read && validate_plate(res.plate)) {
                    LOG_INF("Valid Plate: %s. Infraction Recorded.", res.plate);
                    /* Store infraction record */
                    infraction_record_t rec = {
                        .timestamp_ms = pending_infraction_ctx.active ? pending_infraction_ctx.timestamp_ms : k_uptime_get(),
                        .type = pending_infraction_ctx.active ? pending_infraction_ctx.type : VEHICLE_UNKNOWN,
                        .speed_kmh = pending_infraction_ctx.active ? pending_infraction_ctx.speed_kmh : 0,
                        .limit_kmh = pending_infraction_ctx.active ? pending_infraction_ctx.limit_kmh : 0,
                        .valid_read = true
                    };
                    strncpy(rec.plate, res.plate, sizeof(rec.plate));
                    rec.plate[sizeof(rec.plate)-1] = '\0';
                    infraction_log_add(&rec);
                    /* Send plate info to display with context */
                    display_data_t d_data;
                    d_data.speed_kmh = pending_infraction_ctx.active ? pending_infraction_ctx.speed_kmh : 0; 
                    d_data.limit_kmh = pending_infraction_ctx.active ? pending_infraction_ctx.limit_kmh : 0;
                    d_data.type = pending_infraction_ctx.active ? pending_infraction_ctx.type : VEHICLE_UNKNOWN;
                    d_data.status = STATUS_INFRACTION;
                    d_data.axle_count = 0;
                    d_data.warning_kmh = (d_data.limit_kmh * CONFIG_RADAR_WARNING_THRESHOLD_PERCENT) / 100;
                    strncpy(d_data.plate, res.plate, sizeof(d_data.plate));
                    {
                        int put_ret = k_msgq_put(&display_msgq, &d_data, K_NO_WAIT);
                        if (put_ret != 0) {
                            display_data_t dropped;
                            (void)k_msgq_get(&display_msgq, &dropped, K_NO_WAIT);
                            put_ret = k_msgq_put(&display_msgq, &d_data, K_NO_WAIT);
                            if (put_ret != 0) {
                                LOG_WRN("display_msgq full, dropping update");
                            }
                        }
                    }
                    pending_infraction_ctx.active = false;

                } else {
                    LOG_WRN("Invalid Plate or Read Error");
                    /* Still store infraction record with invalid read */
                    infraction_record_t rec = {
                        .timestamp_ms = pending_infraction_ctx.active ? pending_infraction_ctx.timestamp_ms : k_uptime_get(),
                        .type = pending_infraction_ctx.active ? pending_infraction_ctx.type : VEHICLE_UNKNOWN,
                        .speed_kmh = pending_infraction_ctx.active ? pending_infraction_ctx.speed_kmh : 0,
                        .limit_kmh = pending_infraction_ctx.active ? pending_infraction_ctx.limit_kmh : 0,
                        .valid_read = false
                    };
                    rec.plate[0] = '\0';
                    infraction_log_add(&rec);
                    /* Also update display with known context (no plate) */
                    display_data_t d_data;
                    d_data.speed_kmh = pending_infraction_ctx.active ? pending_infraction_ctx.speed_kmh : 0; 
                    d_data.limit_kmh = pending_infraction_ctx.active ? pending_infraction_ctx.limit_kmh : 0;
                    d_data.type = pending_infraction_ctx.active ? pending_infraction_ctx.type : VEHICLE_UNKNOWN;
                    d_data.status = STATUS_INFRACTION;
                    d_data.axle_count = 0;
                    d_data.warning_kmh = (d_data.limit_kmh * CONFIG_RADAR_WARNING_THRESHOLD_PERCENT) / 100;
                    d_data.plate[0] = '\0';
                    {
                        int put_ret = k_msgq_put(&display_msgq, &d_data, K_NO_WAIT);
                        if (put_ret != 0) {
                            display_data_t dropped;
                            (void)k_msgq_get(&display_msgq, &dropped, K_NO_WAIT);
                            put_ret = k_msgq_put(&display_msgq, &d_data, K_NO_WAIT);
                            if (put_ret != 0) {
                                LOG_WRN("display_msgq full, dropping update");
                            }
                        }
                    }
                    pending_infraction_ctx.active = false;
                }
            }
        }

        k_msleep(10);
    }
    return 0;
}
