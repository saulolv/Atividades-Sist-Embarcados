#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "common.h"

LOG_MODULE_REGISTER(sensor_thread, LOG_LEVEL_INF);

// Get GPIOs from aliases
static const struct gpio_dt_spec sensor_start_spec = GPIO_DT_SPEC_GET(DT_ALIAS(sensor0), gpios);
static const struct gpio_dt_spec sensor_end_spec = GPIO_DT_SPEC_GET(DT_ALIAS(sensor1), gpios);

// State Machine
typedef enum {
    SENSOR_IDLE,
    SENSOR_ACTIVE
} sensor_state_t;

static sensor_state_t state = SENSOR_IDLE; // State machine for the sensor
static int64_t start_time = 0; // Start time of the vehicle
static int64_t end_time = 0; // End time of the vehicle
static uint32_t axle_count = 0; // Number of axles of the vehicle
static bool speed_measured = false; // If the speed was measured

// Timer for Axle Counting Timeout
static struct k_timer axle_timer;
/**
 * Timer expiry callback for the axle counting timeout.
 * @param timer_id Pointer to the timer.
 */
static void axle_timer_expiry(struct k_timer *timer_id);

// GPIO Callbacks
static struct gpio_callback start_cb_data;
static struct gpio_callback end_cb_data;

/**
 * Start interrupt service routine for the sensor.
 * @param dev Pointer to the device.
 * @param cb Pointer to the callback.
 * @param pins Pins that triggered the interrupt.
 */
static void start_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
    int64_t now = k_uptime_get();
    
    if (state == SENSOR_IDLE) {
        state = SENSOR_ACTIVE;
        start_time = now;
        axle_count = 1;
        speed_measured = false;
        // Start timeout timer (2 seconds to clear)
        k_timer_start(&axle_timer, K_SECONDS(2), K_NO_WAIT);
    } else {
        axle_count++;
        // Refresh timer
        k_timer_start(&axle_timer, K_SECONDS(2), K_NO_WAIT);
    }
}

/**
 * End interrupt service routine for the sensor.
 * @param dev Pointer to the device.
 * @param cb Pointer to the callback.
 * @param pins Pins that triggered the interrupt.
 */
static void end_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
    if (state == SENSOR_ACTIVE && !speed_measured) {
        end_time = k_uptime_get();
        speed_measured = true;
    }
}

/**
 * Timer expiry callback for the axle counting timeout.
 * @param timer_id Pointer to the timer.
 */
static void axle_timer_expiry(struct k_timer *timer_id) {
    // Timeout reached, assume vehicle passed
    if (state == SENSOR_ACTIVE) {
        if (speed_measured && end_time > start_time) {
            // Valid reading
            sensor_data_t data;
            data.timestamp_start = start_time;
            data.timestamp_end = end_time;
            data.duration_ms = (uint32_t)(end_time - start_time);
            data.axle_count = axle_count;
            
            // Classification
            if (axle_count <= 2) {
                data.type = VEHICLE_LIGHT;
            } else {
                data.type = VEHICLE_HEAVY;
            }
            
            LOG_INF("Vehicle Detected: Axles=%d, Time=%d ms, Type=%s", 
                    axle_count, data.duration_ms, 
                    data.type == VEHICLE_LIGHT ? "Light" : "Heavy");
            
            k_msgq_put(&sensor_msgq, &data, K_NO_WAIT);
        } else {
            if (!speed_measured) {
                LOG_WRN("Timeout: End sensor not triggered. Ignored.");
            } else {
                LOG_WRN("Invalid timing (End <= Start). Ignored.");
            }
        }
        state = SENSOR_IDLE;
    }
}

/**
 * Main entry point for the sensor thread.
 * @param p1 Pointer to the sensor thread data.
 * @param p2 Pointer to the sensor thread data.
 * @param p3 Pointer to the sensor thread data.
 */
void sensor_thread_entry(void *p1, void *p2, void *p3) {
    int ret;

    // Check if the start sensor is ready
    if (!gpio_is_ready_dt(&sensor_start_spec)) {
        LOG_ERR("Sensor Start GPIO not ready");
        return;
    }

    // Check if the end sensor is ready
    if (!gpio_is_ready_dt(&sensor_end_spec)) {
        LOG_ERR("Sensor End GPIO not ready");
        return;
    }

    // Configure the start sensor as input
    ret = gpio_pin_configure_dt(&sensor_start_spec, GPIO_INPUT);
    if (ret < 0) {
        LOG_ERR("Error configuring sensor start: %d", ret);
        return;
    }

    // Configure the end sensor as input
    ret = gpio_pin_configure_dt(&sensor_end_spec, GPIO_INPUT);
    if (ret < 0) {
        LOG_ERR("Error configuring sensor end: %d", ret);
        return;
    }

    // Configure the start sensor as interrupt
    ret = gpio_pin_interrupt_configure_dt(&sensor_start_spec, GPIO_INT_EDGE_RISING);
    if (ret < 0) {
        LOG_ERR("Error configuring interrupt start: %d", ret);
        return;
    }

    // Configure the end sensor as interrupt
    ret = gpio_pin_interrupt_configure_dt(&sensor_end_spec, GPIO_INT_EDGE_RISING);
    if (ret < 0) {
        LOG_ERR("Error configuring interrupt end: %d", ret);
        return;
    }

    // Initialize the start callback
    gpio_init_callback(&start_cb_data, start_isr, BIT(sensor_start_spec.pin));
    gpio_add_callback(sensor_start_spec.port, &start_cb_data);

    // Initialize the end callback
    gpio_init_callback(&end_cb_data, end_isr, BIT(sensor_end_spec.pin));
    gpio_add_callback(sensor_end_spec.port, &end_cb_data);
    
    k_timer_init(&axle_timer, axle_timer_expiry, NULL);

    // Initialize the axle timer
    LOG_INF("Sensor Thread Initialized");

    // Keep thread alive
    while (1) {
        k_sleep(K_FOREVER);
    }
}

