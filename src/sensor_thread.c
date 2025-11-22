#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "common.h"
#include "sensor_fsm.h"

LOG_MODULE_REGISTER(sensor_thread, LOG_LEVEL_INF);

// Get GPIOs from aliases
static const struct gpio_dt_spec sensor_start_spec = GPIO_DT_SPEC_GET(DT_ALIAS(sensor0), gpios);
static const struct gpio_dt_spec sensor_end_spec = GPIO_DT_SPEC_GET(DT_ALIAS(sensor1), gpios);

// FSM + lock
static sensor_fsm_t fsm;
static struct k_spinlock fsm_lock;

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
    k_spinlock_key_t key = k_spin_lock(&fsm_lock);
    sensor_fsm_handle_start(&fsm, now);
    k_spin_unlock(&fsm_lock, key);
    // Start or refresh timeout timer (configurable)
    k_timer_start(&axle_timer, K_MSEC(CONFIG_RADAR_AXLE_TIMEOUT_MS), K_NO_WAIT);
}

/**
 * End interrupt service routine for the sensor.
 * @param dev Pointer to the device.
 * @param cb Pointer to the callback.
 * @param pins Pins that triggered the interrupt.
 */
static void end_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
    int64_t now = k_uptime_get();
    k_spinlock_key_t key = k_spin_lock(&fsm_lock);
    sensor_fsm_handle_end(&fsm, now);
    k_spin_unlock(&fsm_lock, key);
}

/**
 * Timer expiry callback for the axle counting timeout.
 * @param timer_id Pointer to the timer.
 */
static void axle_timer_expiry(struct k_timer *timer_id) {
    // Timeout reached, check if we can finalize a measurement
    sensor_data_t data;
    bool produced = false;
    k_spinlock_key_t key = k_spin_lock(&fsm_lock);
    produced = sensor_fsm_finalize(&fsm, &data);
    k_spin_unlock(&fsm_lock, key);

    if (produced) {
        LOG_INF("Vehicle Detected: Axles=%d, Time=%d ms, Type=%s", 
                data.axle_count, data.duration_ms, 
                data.type == VEHICLE_LIGHT ? "Light" : "Heavy");
        int ret = k_msgq_put(&sensor_msgq, &data, K_NO_WAIT);
        if (ret != 0) {
            /* Drop oldest and retry once */
            sensor_data_t dropped;
            (void)k_msgq_get(&sensor_msgq, &dropped, K_NO_WAIT);
            ret = k_msgq_put(&sensor_msgq, &data, K_NO_WAIT);
            if (ret != 0) {
                LOG_WRN("sensor_msgq full, dropping measurement");
            }
        }
    } else {
        LOG_WRN("Measurement window ended without valid timing. Ignored.");
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

    sensor_fsm_init(&fsm);

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

