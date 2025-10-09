#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_REGISTER(led_control, LOG_LEVEL_DBG);

/* ============================================
 * Device Tree Configuration
 * ============================================ */

/* LED Configuration - Required */
#define LED_NODE DT_ALIAS(led0)
#if !DT_NODE_HAS_STATUS(LED_NODE, okay)
	#error "led0 alias is not defined or not enabled in device tree"
#endif
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);

/* Button Configuration - Required */
#define BUTTON_NODE DT_ALIAS(sw0)
#if !DT_NODE_HAS_STATUS(BUTTON_NODE, okay)
	#error "sw0 alias is not defined or not enabled in device tree"
#endif
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(BUTTON_NODE, gpios);

/* PWM Configuration - Optional */
#define PWM_LED_NODE DT_ALIAS(pwm_led0)
#if DT_NODE_EXISTS(PWM_LED_NODE) && DT_NODE_HAS_STATUS(PWM_LED_NODE, okay)
	#define PWM_AVAILABLE 1
	static const struct pwm_dt_spec pwm_led = PWM_DT_SPEC_GET(PWM_LED_NODE);
#else
	#define PWM_AVAILABLE 0
	#warning "PWM not available - only DIGITAL mode will work"
#endif

/* ============================================
 * Constants and Global Variables
 * ============================================ */

#define BLINK_INTERVAL_MS 500           // LED blink interval in ms
#define PWM_PERIOD_US 20000             // PWM period: 20ms (50Hz)
#define PWM_FADE_STEP 5                 // PWM duty cycle increment/decrement
#define PWM_FADE_DELAY_MS 20            // Delay between fade steps
#define BUTTON_DEBOUNCE_MS 50           // Button debounce time
#define BUTTON_POLL_INTERVAL_MS 10      // Button polling interval

/* System states */
typedef enum {
	MODE_DIGITAL = 0,   // Mode 1: On/Off digital
	MODE_PWM = 1        // Mode 2: Fade PWM
} operation_mode_t;

/* Global state */
static operation_mode_t current_mode = MODE_DIGITAL;
static struct gpio_callback button_cb_data;
static volatile bool button_pressed = false;
static bool button_interrupt_enabled = false;  // Track if interrupt mode works

/* ============================================
 * Callback and Control Functions
 * ============================================ */

/**
 * button_pressed_callback is a function that is called when the button is pressed.
 * It sets the button_pressed variable to true.
 * 
 * @param dev: GPIO device that was pressed.
 * @param cb: callback that was registered for the GPIO device.
 * @param pins: bitmask of the pins that were pressed.
 */
static void button_pressed_callback(const struct device *dev, 
                                    struct gpio_callback *cb, 
                                    uint32_t pins)
{
	button_pressed = true;
}



/**
 * digital_led_blink is a function that blinks the LED.
 * It sets the led_state variable to true if the LED is off, and false if the LED is on.
 */
static void digital_led_blink(void)
{
	
	static bool led_state = false;
	
	led_state = !led_state;
	gpio_pin_set_dt(&led, led_state);
	
	LOG_INF("DIGITAL MODE: LED %s", led_state ? "ON" : "OFF");
}

/**
 * pwm_led_fade is a function that fades the LED.
 * It sets the duty_cycle variable to the duty cycle of the LED.
 * It sets the direction variable to the direction of the fade.
 * It sets the pulse_us variable to the pulse duration of the LED.
 * It applies the PWM to the LED.
 * It logs the duty cycle of the LED.
 * It updates the duty cycle for the next step.
 * It inverts the direction at the extremes.
 */
static void pwm_led_fade(void)
{
#if PWM_AVAILABLE
	static uint32_t duty_cycle = 0;
	static int8_t direction = 1;  // 1 = increasing, -1 = decreasing
	
	// Calculate the duty cycle in us
	uint32_t pulse_us = (PWM_PERIOD_US * duty_cycle) / 100;
	
	// Apply the PWM
	pwm_set_dt(&pwm_led, PWM_USEC(PWM_PERIOD_US), PWM_USEC(pulse_us));
	
	LOG_DBG("PWM MODE: Duty Cycle = %u%%", duty_cycle);
	
	// Update the duty cycle for the next step
	duty_cycle += direction * PWM_FADE_STEP;
	
	// Invert the direction at the extremes
	if (duty_cycle >= 100) {
		duty_cycle = 100;
		direction = -1;
		LOG_INF("PWM MODE: Fade OUT started");
	} else if (duty_cycle <= 0 || (int32_t)duty_cycle < 0) {
		duty_cycle = 0;
		direction = 1;
		LOG_INF("PWM MODE: Fade IN started");
	}
#else
	LOG_WRN("PWM MODE: not available - skipping fade");
#endif
}



/**
 * toggle_mode - Toggles between DIGITAL and PWM modes with validation
 * 
 * Switches operational mode if target mode is available. Cleans up the
 * previous mode's LED state before transitioning.
 */
static void toggle_mode(void)
{
	operation_mode_t new_mode = (current_mode == MODE_DIGITAL) ? MODE_PWM : MODE_DIGITAL;
	
	// Validate PWM availability before switching to PWM mode
#if !PWM_AVAILABLE
	if (new_mode == MODE_PWM) {
		LOG_WRN("Cannot switch to PWM mode - PWM not available");
		return;
	}
#endif
	
	current_mode = new_mode;
	
	LOG_INF("===========================================");
	LOG_INF("MODE CHANGED: %s", 
	        current_mode == MODE_DIGITAL ? "DIGITAL (On/Off)" : "PWM (Fade)");
	LOG_INF("===========================================");
	
	// Clean up previous mode state
	if (current_mode == MODE_DIGITAL) {
		// Returning to digital mode: disable PWM
#if PWM_AVAILABLE
		pwm_set_dt(&pwm_led, PWM_USEC(PWM_PERIOD_US), 0);
#endif
		gpio_pin_set_dt(&led, 0);
	} else {
		// Entering PWM mode: ensure GPIO is off
		gpio_pin_set_dt(&led, 0);
	}
}

/**
 * check_button_state - Reads button state considering its polarity
 * 
 * Returns true if button is currently pressed, false otherwise.
 * Handles both ACTIVE_HIGH and ACTIVE_LOW buttons correctly.
 */
static bool check_button_state(void)
{
	int val = gpio_pin_get_dt(&button);
	
	// For ACTIVE_LOW buttons, pressed = 0
	// For ACTIVE_HIGH buttons, pressed = 1
	// gpio_pin_get_dt returns the raw value, so we need to check against flags
	if (button.dt_flags & GPIO_ACTIVE_LOW) {
		return (val == 0);
	} else {
		return (val == 1);
	}
}

/**
 * button_handler_thread - Handles button presses via interrupt or polling
 * 
 * If interrupts are available, waits for interrupt callback.
 * If not (e.g., QEMU), uses polling mode.
 * Includes debouncing and edge detection.
 */
static void button_handler_thread(void)
{
	static bool last_button_state = false;
	bool current_button_state;
	
	while (1) {
		if (button_interrupt_enabled) {
			// Interrupt mode: wait for callback
			if (button_pressed) {
				button_pressed = false;
				
				// Debounce
				k_sleep(K_MSEC(BUTTON_DEBOUNCE_MS));
				
				// Verify button is still pressed
				if (check_button_state()) {
					toggle_mode();
					
					// Wait for button release
					while (check_button_state()) {
						k_sleep(K_MSEC(10));
					}
				}
			}
			k_sleep(K_MSEC(BUTTON_POLL_INTERVAL_MS));
		} else {
			// Polling mode: check button state periodically
			current_button_state = check_button_state();
			
			// Detect rising edge (button press)
			if (current_button_state && !last_button_state) {
				// Debounce
				k_sleep(K_MSEC(BUTTON_DEBOUNCE_MS));
				
				// Verify button is still pressed
				if (check_button_state()) {
					toggle_mode();
					
					// Wait for button release to avoid multiple toggles
					while (check_button_state()) {
						k_sleep(K_MSEC(10));
					}
				}
			}
			
			last_button_state = current_button_state;
			k_sleep(K_MSEC(BUTTON_POLL_INTERVAL_MS));
		}
	}
}

/* ============================================
 * Initialization and Main
 * ============================================ */

/**
 * setup_led_gpio is a function that configures the LED GPIO.
 * It checks if the LED GPIO is ready.
 * It configures the LED GPIO as output.
 * It logs the success of the LED GPIO configuration.
 */
static int setup_led_gpio(void)
{
	int ret;
	
	if (!gpio_is_ready_dt(&led)) {
		LOG_ERR("LED GPIO device not ready");
		return -ENODEV;
	}
	
	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure LED GPIO");
		return ret;
	}
	
	LOG_INF("LED GPIO configured successfully");
	return 0;
}

/**
 * setup_button_gpio - Configures button with interrupt or polling fallback
 * 
 * Attempts to configure GPIO interrupt. If it fails (e.g., on QEMU),
 * gracefully falls back to polling mode. This ensures the application
 * continues to work even on platforms with limited interrupt support.
 * 
 * Returns: 0 on success, negative error code on critical failure
 */
static int setup_button_gpio(void)
{
	int ret;
	
	if (!gpio_is_ready_dt(&button)) {
		LOG_ERR("Button GPIO device not ready");
		return -ENODEV;
	}
	
	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Failed to configure button GPIO");
		return ret;
	}
	
	// Try to configure interrupt mode
	ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret == 0) {
		// Interrupt configuration successful
		gpio_init_callback(&button_cb_data, button_pressed_callback, BIT(button.pin));
		gpio_add_callback(button.port, &button_cb_data);
		button_interrupt_enabled = true;
		LOG_INF("Button configured with INTERRUPT mode");
	} else {
		// Interrupt failed - use polling mode
		button_interrupt_enabled = false;
		LOG_WRN("Button interrupt not supported - using POLLING mode");
	}
	
	LOG_INF("Button GPIO configured successfully");
	return 0;
}

/**
 * setup_pwm is a function that configures the PWM.
 * It checks if the PWM is ready.
 * It logs the success of the PWM configuration.
 */
static int setup_pwm(void)
{
#if PWM_AVAILABLE
	if (!pwm_is_ready_dt(&pwm_led)) {
		LOG_ERR("PWM device not ready");
		return -ENODEV;
	}
	
	LOG_INF("PWM configured successfully");
	return 0;
#else
	LOG_WRN("PWM not available on this board");
	return -ENOTSUP;
#endif
}

/**
 * button_handler_thread is a thread that handles the button presses.
 * It waits for the button to be pressed.
 * It debounces the button press.
 * It toggles the mode between digital and PWM.
 * 
 * @param button_thread_id: thread id for the button handler thread.
 * @param 1024: stack size for the button handler thread.
 * @param button_handler_thread: function to handle the button presses.
 * @param NULL: parameter for the button handler thread.
 * @param NULL: parameter for the button handler thread.
 * @param NULL: parameter for the button handler thread.
 * @param 7: priority for the button handler thread.
 * @param 0: flags for the button handler thread.
 * @param 0: parameter for the button handler thread.
 */
K_THREAD_DEFINE(button_thread_id, 1024, button_handler_thread, NULL, NULL, NULL, 7, 0, 0);

/**
 * main - Application entry point
 * 
 * Initializes all peripherals and enters the main control loop.
 * Handles initialization failures gracefully when possible.
 */
int main(void)
{
	int ret;
	
	printk("\n");
	printk("=================================================\n");
	printk("  Activity 2 - LED Control with GPIO and PWM    \n");
	printk("=================================================\n");
	printk("\n");
		
	// Initialize LED GPIO (critical - must succeed)
	ret = setup_led_gpio();
	if (ret < 0) {
		LOG_ERR("FATAL: Failed to initialize LED (error %d)", ret);
		return ret;
	}
	
	// Initialize button GPIO (critical - must succeed)
	ret = setup_button_gpio();
	if (ret < 0) {
		LOG_ERR("FATAL: Failed to initialize button (error %d)", ret);
		return ret;
	}
	
	// Initialize PWM (optional - continue if not available)
	ret = setup_pwm();
	if (ret < 0 && ret != -ENOTSUP) {
		LOG_ERR("WARNING: PWM initialization failed (error %d)", ret);
		LOG_WRN("Continuing in DIGITAL mode only");
	}
	
	LOG_INF("========================================");
	LOG_INF("System initialized successfully!");
	LOG_INF("Button mode: %s", 
	        button_interrupt_enabled ? "INTERRUPT" : "POLLING");
	LOG_INF("PWM support: %s", PWM_AVAILABLE ? "YES" : "NO");
	LOG_INF("Initial mode: DIGITAL (On/Off)");
	LOG_INF("Press button to switch modes");
	LOG_INF("========================================");
	printk("\n");
	
	// Main control loop
	while (1) {
		if (current_mode == MODE_DIGITAL) {
			digital_led_blink();
			k_sleep(K_MSEC(BLINK_INTERVAL_MS));
		} else {
			pwm_led_fade();
			k_sleep(K_MSEC(PWM_FADE_DELAY_MS));
		}
	}
	
	return 0;
}
