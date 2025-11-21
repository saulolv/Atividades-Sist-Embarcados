#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <string.h>
#include "common.h"
#include "threads.h"

LOG_MODULE_REGISTER(main_control, LOG_LEVEL_INF);

K_MSGQ_DEFINE(sensor_msgq, sizeof(sensor_data_t), 10, 4); // Message Queue for Sensor Data
K_MSGQ_DEFINE(display_msgq, sizeof(display_data_t), 10, 4); // Message Queue for Display Data

// ZBUS Channels
ZBUS_CHAN_DEFINE(camera_trigger_chan, camera_trigger_t, NULL, NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));
ZBUS_CHAN_DEFINE(camera_result_chan, camera_result_t, NULL, NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

// Thread Definitions
K_THREAD_DEFINE(sensor_tid, 2048, sensor_thread_entry, NULL, NULL, NULL, 7, 0, 0);
K_THREAD_DEFINE(display_tid, 2048, display_thread_entry, NULL, NULL, NULL, 7, 0, 0);
K_THREAD_DEFINE(camera_tid, 2048, camera_thread_entry, NULL, NULL, NULL, 7, 0, 0);

// Subscriber for Main Thread
ZBUS_SUBSCRIBER_DEFINE(main_camera_sub, 4);

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
            uint32_t speed_kmh = 0;
            if (s_data.duration_ms > 0) {
                // Speed (km/h) = (dist_mm / time_ms) * 3.6
                // = (dist * 36) / (time * 10)
                // Use uint64_t to prevent overflow before division
                speed_kmh = (uint32_t)(((uint64_t)distance_mm * 36) / (s_data.duration_ms * 10));
            }

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
            // Send the display data to the display queue
            k_msgq_put(&display_msgq, &d_data, K_NO_WAIT);

            // Trigger Camera if Infraction
            if (status == STATUS_INFRACTION) {
                camera_trigger_t trig;
                trig.speed_kmh = speed_kmh;
                trig.type = s_data.type;
                zbus_chan_pub(&camera_trigger_chan, &trig, K_NO_WAIT);
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
                    
                    // Send plate info to display
                    display_data_t d_data;
                    d_data.speed_kmh = 0; 
                    d_data.limit_kmh = 0;
                    d_data.type = VEHICLE_UNKNOWN;
                    d_data.status = STATUS_INFRACTION;
                    strncpy(d_data.plate, res.plate, sizeof(d_data.plate));
                    
                    // Send the display data to the display queue
                    k_msgq_put(&display_msgq, &d_data, K_NO_WAIT);
                } else {
                    LOG_WRN("Invalid Plate or Read Error");
                }
            }
        }

        k_msleep(10);
    }
    return 0;
}
