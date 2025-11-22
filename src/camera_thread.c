#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>
#include "common.h"

LOG_MODULE_REGISTER(camera_thread, LOG_LEVEL_INF);

ZBUS_SUBSCRIBER_DEFINE(camera_sub, 4);

/**
 * Generates a random Mercosul plate number.
 * @param buf The buffer to store the plate number.
 */
static void generate_plate(char *buf) {
    // Mercosul format: ABC1D23
    const char letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const char numbers[] = "0123456789";
#if IS_ENABLED(CONFIG_TEST)
    /* Deterministic RNG for tests */
    static uint32_t rng_state = 0x12345678u;
    static inline uint32_t rand32_local(void) {
        /* xorshift32 */
        uint32_t x = rng_state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        rng_state = x;
        return x;
    }
#define RAND32() rand32_local()
#else
#define RAND32() sys_rand32_get()
#endif
    
    buf[0] = letters[RAND32() % 26];
    buf[1] = letters[RAND32() % 26];
    buf[2] = letters[RAND32() % 26];
    buf[3] = numbers[RAND32() % 10];
    buf[4] = letters[RAND32() % 26];
    buf[5] = numbers[RAND32() % 10];
    buf[6] = numbers[RAND32() % 10];
    buf[7] = '\0';
}

/**
 * Main entry point for the camera thread.
 * @param p1 Pointer to the camera thread data.
 * @param p2 Pointer to the camera thread data.
 * @param p3 Pointer to the camera thread data.
 */
void camera_thread_entry(void *p1, void *p2, void *p3) {
    const struct zbus_channel *chan;
    
    // Subscribe to the trigger channel
    zbus_chan_add_obs(&camera_trigger_chan, &camera_sub, K_FOREVER);

    LOG_INF("Camera System Ready");

    while (1) {
        // Wait for a trigger from the sensor thread
        if (zbus_sub_wait(&camera_sub, &chan, K_FOREVER) == 0) {
            if (chan == &camera_trigger_chan) {
                // Read the trigger data
                camera_trigger_t trigger;
                zbus_chan_read(&camera_trigger_chan, &trigger, K_NO_WAIT);
                
                LOG_INF("Camera Triggered! Processing...");
                
                // Simulate processing time
                k_msleep(500); 
                
                camera_result_t result;
                // Generate a random failure rate
                uint32_t failure_rate = CONFIG_RADAR_CAMERA_FAILURE_RATE_PERCENT;
                uint32_t chance = sys_rand32_get() % 100;
                
                // If the chance is less than the failure rate, the read failed
                if (chance < failure_rate) {
                    LOG_WRN("Camera simulation: Read Failed");
                    result.valid_read = false;
                    result.plate[0] = '\0';
                } else {
                    generate_plate(result.plate);
                    result.valid_read = true;
                    LOG_INF("Camera Result: %s", result.plate);
                }
                
                int pub_ret = zbus_chan_pub(&camera_result_chan, &result, K_NO_WAIT);
                if (pub_ret != 0) {
                    LOG_WRN("ZBUS publish to camera_result_chan failed: %d", pub_ret);
                }
            }
        }
    }
}

