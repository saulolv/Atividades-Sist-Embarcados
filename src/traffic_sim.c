#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>

LOG_MODULE_REGISTER(traffic_sim, LOG_LEVEL_INF);

// Get GPIOs (same as sensor thread, but we will try to configure them to trigger logic or just logs)
// NOTE: On real hardware, we can't drive an INPUT pin high internally without loopback.
// Since we want to verify the logic without rewiring the whole sensor_thread to use software events,
// we will use a trick: We will re-open the pins and toggle them if possible, 
// OR simpler: We will just emit log messages saying "Simulating Car..." 
// and manually inject messages into the queues to demonstrate the REST of the system (Main -> Display -> Camera).
//
// HOWEVER, to test the Sensor Thread logic itself, we really need GPIO interrupts.
// On QEMU MPS2, we can't easily inject GPIO interrupts from software unless we use the QEMU Monitor.
//
// ALTERNATIVE: We will inject messages directly into 'sensor_msgq' to simulate the OUTPUT of the sensor thread.
// This verifies Main Logic, Classification, Display, and Camera.

#include "common.h"

void traffic_sim_thread_entry(void *p1, void *p2, void *p3) {
    LOG_INF("Traffic Simulator Started (Auto-generating vehicles every 5s)");

    k_sleep(K_SECONDS(2)); // Wait for system to settle

    while (1) {
        sensor_data_t s_data;

        // 1. Simulate a Light Vehicle (Normal Speed)
        // Distance 5m, Speed 50km/h
        // Time = Dist / Speed = 0.005 km / 50 km/h = 0.0001 h = 0.36 s = 360ms
        s_data.timestamp_start = k_uptime_get();
        s_data.duration_ms = 360; 
        s_data.timestamp_end = s_data.timestamp_start + 360;
        s_data.axle_count = 2;
        s_data.type = VEHICLE_LIGHT;
        
        LOG_INF("SIMULATION: Generating Light Vehicle (50 km/h)");
        k_msgq_put(&sensor_msgq, &s_data, K_NO_WAIT);
        
        k_sleep(K_SECONDS(5));

        // 2. Simulate a Heavy Vehicle (Infraction)
        // Limit is 40 km/h. Let's go 50 km/h.
        // Time = 360ms (same speed as above, but for heavy it's a violation)
        s_data.timestamp_start = k_uptime_get();
        s_data.duration_ms = 360; 
        s_data.timestamp_end = s_data.timestamp_start + 360;
        s_data.axle_count = 3;
        s_data.type = VEHICLE_HEAVY;

        LOG_INF("SIMULATION: Generating Heavy Vehicle (50 km/h - Infraction!)");
        k_msgq_put(&sensor_msgq, &s_data, K_NO_WAIT);

        k_sleep(K_SECONDS(5));
        
        // 3. Simulate High Speed Light Vehicle (Infraction)
        // 80 km/h. Time = 225ms
        s_data.timestamp_start = k_uptime_get();
        s_data.duration_ms = 225; 
        s_data.timestamp_end = s_data.timestamp_start + 225;
        s_data.axle_count = 2;
        s_data.type = VEHICLE_LIGHT;

        LOG_INF("SIMULATION: Generating Light Vehicle (80 km/h - Infraction!)");
        k_msgq_put(&sensor_msgq, &s_data, K_NO_WAIT);
        
        k_sleep(K_SECONDS(5));
    }
}

K_THREAD_DEFINE(traffic_sim_tid, 1024, traffic_sim_thread_entry, NULL, NULL, NULL, 8, 0, 0);

