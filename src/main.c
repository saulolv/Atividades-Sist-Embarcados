#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_REGISTER(led_control, LOG_LEVEL_DBG);

#define LED_NODE DT_ALIAS(led0)
#if DT_NODE_HAS_STATUS(LED_NODE, okay)
	static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);
	#define USE_NATIVE_LED 1
#else
	/* Manual configuration for ESP32-S3 */
	#define MANUAL_LED_GPIO_CONTROLLER DT_NODELABEL(gpio0)
	#define MANUAL_LED_GPIO_PIN 2
	#define MANUAL_LED_GPIO_FLAGS GPIO_ACTIVE_HIGH
	static const struct gpio_dt_spec led = {
		.port = DEVICE_DT_GET(MANUAL_LED_GPIO_CONTROLLER),
		.pin = MANUAL_LED_GPIO_PIN,
		.dt_flags = MANUAL_LED_GPIO_FLAGS
	};
	#define USE_NATIVE_LED 0
	#pragma message "LED: using GPIO0 (manual configuration)"
#endif

/* Button GPIO - Try to use sw0, otherwise use manual configuration */
#define BUTTON_NODE DT_ALIAS(sw0)
#if DT_NODE_HAS_STATUS(BUTTON_NODE, okay)
	static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(BUTTON_NODE, gpios);
	#define USE_NATIVE_BUTTON 1
#else
	/* Manual configuration for ESP32-S3 (GPIO0 = button BOOT) */
	#define MANUAL_BUTTON_GPIO_CONTROLLER DT_NODELABEL(gpio0)
	#define MANUAL_BUTTON_GPIO_PIN 0
	#define MANUAL_BUTTON_GPIO_FLAGS (GPIO_ACTIVE_LOW | GPIO_PULL_UP)
	static const struct gpio_dt_spec button = {
		.port = DEVICE_DT_GET(MANUAL_BUTTON_GPIO_CONTROLLER),
		.pin = MANUAL_BUTTON_GPIO_PIN,
		.dt_flags = MANUAL_BUTTON_GPIO_FLAGS
	};
	#define USE_NATIVE_BUTTON 0
	#pragma message "Button: using GPIO0/BOOT (manual configuration)"
#endif

#define PWM_LED_NODE DT_ALIAS(pwm_led0)
#if DT_NODE_EXISTS(PWM_LED_NODE) && DT_NODE_HAS_STATUS(PWM_LED_NODE, okay)
	#define PWM_AVAILABLE 1
	static const struct pwm_dt_spec pwm_led = PWM_DT_SPEC_GET(PWM_LED_NODE);
	#pragma message "PWM: using pwm-led0 from device tree"
#else
	#define PWM_AVAILABLE 0
	#pragma message "PWM: disabled. To enable, create an overlay with pwm-led0"
#endif

/* ============================================
 * Constants and Global Variables
 * ============================================ */

#define BLINK_INTERVAL_MS 500      // LED blink interval in ms
#define PWM_PERIOD_US 20000        // PWM period: 20ms (50Hz)
#define PWM_FADE_STEP 5            // Incremento do duty cycle
#define PWM_FADE_DELAY_MS 20       // Delay between steps of the fade

/* System states */
typedef enum {
	MODE_DIGITAL = 0,   // Mode 1: On/Off digital
	MODE_PWM = 1        // Mode 2: Fade PWM
} operation_mode_t;

static operation_mode_t current_mode = MODE_DIGITAL;
static struct gpio_callback button_cb_data;
static volatile bool button_pressed = false;

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
 * toggle_mode is a function that toggles the mode between digital and PWM.
 * It sets the current_mode variable to the opposite of the current mode.
 * It logs the mode that was changed to.
 * It clears the previous state of the LED.
 * It sets the LED to off.
 */
static void toggle_mode(void)
{
	current_mode = (current_mode == MODE_DIGITAL) ? MODE_PWM : MODE_DIGITAL;
	
	LOG_INF("===========================================");
	LOG_INF("MODE CHANGED: %s", 
	        current_mode == MODE_DIGITAL ? "DIGITAL (On/Off)" : "PWM (Fade)");
	LOG_INF("===========================================");
	
	// Clear the previous state of the LED
	if (current_mode == MODE_DIGITAL) {
		// When returning to digital, ensure PWM is off
#if PWM_AVAILABLE
		pwm_set_dt(&pwm_led, PWM_USEC(PWM_PERIOD_US), 0);
#endif
		gpio_pin_set_dt(&led, 0);
	} else {
		// When entering PWM, ensure GPIO is off
		gpio_pin_set_dt(&led, 0);
	}
}

/**
 * button_handler_thread is a thread that handles the button presses.
 * It waits for the button to be pressed.
 * It debounces the button press.
 * It toggles the mode between digital and PWM.
 */
static void button_handler_thread(void)
{
	while (1) {
		if (button_pressed) {
			button_pressed = false;
			
			// Debounce the button press
			k_sleep(K_MSEC(50));
			
			// Check if the button is still pressed
			if (gpio_pin_get_dt(&button) == 1) {
				toggle_mode();
			}
		}
		
		k_sleep(K_MSEC(10));
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
 * setup_button_gpio is a function that configures the button GPIO.
 * It checks if the button GPIO is ready.
 * It configures the button GPIO as input.
 * It logs the success of the button GPIO configuration.
 * It configures the button interrupt.
 * It initializes the button callback.
 * It logs the success of the button callback initialization.
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
	
	// Configure the button interrupt
	ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure button interrupt");
		return ret;
	}
	
	// Initialize the button callback
	gpio_init_callback(&button_cb_data, button_pressed_callback, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb_data);
	
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

int main(void)
{
	int ret;
	
	printk("\n");
	printk("=================================================\n");
	printk("  Activity 2 - LED control with GPIO and PWM\n");
	printk("=================================================\n");
	printk("\n");
		
	// Initialize LED GPIO
	ret = setup_led_gpio();
	if (ret < 0) {
		LOG_ERR("Failed to initialize LED");
		return ret;
	}
	
	// Initialize button GPIO
	ret = setup_button_gpio();
	if (ret < 0) {
		LOG_ERR("Failed to initialize button");
		return ret;
	}
	
	// Initialize PWM
	ret = setup_pwm();
	if (ret < 0 && ret != -ENOTSUP) {
		LOG_ERR("Failed to initialize PWM");
		return ret;
	}
	
	LOG_INF("System initialized successfully!");
	LOG_INF("Press the button to switch between modes");
	LOG_INF("Initial mode: DIGITAL (On/Off)");
	printk("\n");
	
	// Main loop
	while (1) {
		if (current_mode == MODE_DIGITAL) {
			// Mode 1: Blink LED digitally
			digital_led_blink();
			k_sleep(K_MSEC(BLINK_INTERVAL_MS));
		} else {
			// Mode 2: Fade PWM
			pwm_led_fade();
			k_sleep(K_MSEC(PWM_FADE_DELAY_MS));
		}
	}
	
	return 0;
}
