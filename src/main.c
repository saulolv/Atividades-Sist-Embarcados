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
	/* Configuração manual para ESP32-S3 */
	#define MANUAL_LED_GPIO_CONTROLLER DT_NODELABEL(gpio0)
	#define MANUAL_LED_GPIO_PIN 2
	#define MANUAL_LED_GPIO_FLAGS GPIO_ACTIVE_HIGH
	static const struct gpio_dt_spec led = {
		.port = DEVICE_DT_GET(MANUAL_LED_GPIO_CONTROLLER),
		.pin = MANUAL_LED_GPIO_PIN,
		.dt_flags = MANUAL_LED_GPIO_FLAGS
	};
	#define USE_NATIVE_LED 0
	#pragma message "LED: usando GPIO2 (configuração manual)"
#endif

/* Botão GPIO - Tenta usar sw0, senão usa configuração manual */
#define BUTTON_NODE DT_ALIAS(sw0)
#if DT_NODE_HAS_STATUS(BUTTON_NODE, okay)
	static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(BUTTON_NODE, gpios);
	#define USE_NATIVE_BUTTON 1
#else
	/* Configuração manual para ESP32-S3 (GPIO0 = botão BOOT) */
	#define MANUAL_BUTTON_GPIO_CONTROLLER DT_NODELABEL(gpio0)
	#define MANUAL_BUTTON_GPIO_PIN 0
	#define MANUAL_BUTTON_GPIO_FLAGS (GPIO_ACTIVE_LOW | GPIO_PULL_UP)
	static const struct gpio_dt_spec button = {
		.port = DEVICE_DT_GET(MANUAL_BUTTON_GPIO_CONTROLLER),
		.pin = MANUAL_BUTTON_GPIO_PIN,
		.dt_flags = MANUAL_BUTTON_GPIO_FLAGS
	};
	#define USE_NATIVE_BUTTON 0
	#pragma message "Botão: usando GPIO0/BOOT (configuração manual)"
#endif

/* PWM - Requer overlay para funcionar */
#define PWM_LED_NODE DT_ALIAS(pwm_led0)
#if DT_NODE_EXISTS(PWM_LED_NODE) && DT_NODE_HAS_STATUS(PWM_LED_NODE, okay)
	#define PWM_AVAILABLE 1
	static const struct pwm_dt_spec pwm_led = PWM_DT_SPEC_GET(PWM_LED_NODE);
	#pragma message "PWM: usando pwm-led0 do device tree"
#else
	#define PWM_AVAILABLE 0
	#pragma message "PWM: desabilitado. Para habilitar, crie um overlay com pwm-led0"
#endif

/* ============================================
 * Constantes e Variáveis Globais
 * ============================================ */

#define BLINK_INTERVAL_MS 500      // Intervalo de piscar do LED em ms
#define PWM_PERIOD_US 20000        // Período PWM: 20ms (50Hz)
#define PWM_FADE_STEP 5            // Incremento do duty cycle
#define PWM_FADE_DELAY_MS 20       // Delay entre steps do fade

/* Estados do sistema */
typedef enum {
	MODE_DIGITAL = 0,   // Modo 1: On/Off digital
	MODE_PWM = 1        // Modo 2: Fade PWM
} operation_mode_t;

static operation_mode_t current_mode = MODE_DIGITAL;
static struct gpio_callback button_cb_data;
static volatile bool button_pressed = false;

/* ============================================
 * Funções de Callback e Controle
 * ============================================ */

/**
 * Callback do botão - detecta pressão
 */
static void button_pressed_callback(const struct device *dev, 
                                    struct gpio_callback *cb, 
                                    uint32_t pins)
{
	button_pressed = true;
}

/**
 * Etapa 1 e 2: Controle digital do LED (on/off com piscar)
 */
static void digital_led_blink(void)
{
	static bool led_state = false;
	
	led_state = !led_state;
	gpio_pin_set_dt(&led, led_state);
	
	LOG_INF("MODO DIGITAL: LED %s", led_state ? "ON" : "OFF");
}

/**
 * Etapa 3: Controle PWM do LED com fade in/out
 */
static void pwm_led_fade(void)
{
#if PWM_AVAILABLE
	static uint32_t duty_cycle = 0;
	static int8_t direction = 1;  // 1 = aumentando, -1 = diminuindo
	
	// Calcula o duty cycle em us
	uint32_t pulse_us = (PWM_PERIOD_US * duty_cycle) / 100;
	
	// Aplica o PWM
	pwm_set_dt(&pwm_led, PWM_USEC(PWM_PERIOD_US), PWM_USEC(pulse_us));
	
	LOG_DBG("MODO PWM: Duty Cycle = %u%%", duty_cycle);
	
	// Atualiza duty cycle para próximo step
	duty_cycle += direction * PWM_FADE_STEP;
	
	// Inverte direção nos extremos
	if (duty_cycle >= 100) {
		duty_cycle = 100;
		direction = -1;
		LOG_INF("PWM: Fade OUT iniciado");
	} else if (duty_cycle <= 0 || (int32_t)duty_cycle < 0) {
		duty_cycle = 0;
		direction = 1;
		LOG_INF("PWM: Fade IN iniciado");
	}
#else
	LOG_WRN("PWM não disponível - pulando fade");
#endif
}

/**
 * Etapa 4: Alterna entre modos de operação
 */
static void toggle_mode(void)
{
	current_mode = (current_mode == MODE_DIGITAL) ? MODE_PWM : MODE_DIGITAL;
	
	LOG_INF("===========================================");
	LOG_INF("MODO ALTERADO: %s", 
	        current_mode == MODE_DIGITAL ? "DIGITAL (On/Off)" : "PWM (Fade)");
	LOG_INF("===========================================");
	
	// Limpa estado anterior
	if (current_mode == MODE_DIGITAL) {
		// Ao voltar para digital, garante PWM desligado
#if PWM_AVAILABLE
		pwm_set_dt(&pwm_led, PWM_USEC(PWM_PERIOD_US), 0);
#endif
		gpio_pin_set_dt(&led, 0);
	} else {
		// Ao entrar em PWM, garante GPIO desligado
		gpio_pin_set_dt(&led, 0);
	}
}

/**
 * Thread para processar pressões do botão
 */
static void button_handler_thread(void)
{
	while (1) {
		if (button_pressed) {
			button_pressed = false;
			
			// Debounce simples
			k_sleep(K_MSEC(50));
			
			// Verifica se botão ainda está pressionado
			if (gpio_pin_get_dt(&button) == 1) {
				toggle_mode();
			}
		}
		
		k_sleep(K_MSEC(10));
	}
}

/* ============================================
 * Inicialização e Main
 * ============================================ */

/**
 * Configura GPIO do LED
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
	
	LOG_INF("LED GPIO configurado com sucesso");
	return 0;
}

/**
 * Configura GPIO do botão
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
	
	// Configura interrupção do botão
	ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure button interrupt");
		return ret;
	}
	
	// Inicializa callback
	gpio_init_callback(&button_cb_data, button_pressed_callback, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb_data);
	
	LOG_INF("Botão GPIO configurado com sucesso");
	return 0;
}

/**
 * Configura PWM
 */
static int setup_pwm(void)
{
#if PWM_AVAILABLE
	if (!pwm_is_ready_dt(&pwm_led)) {
		LOG_ERR("PWM device not ready");
		return -ENODEV;
	}
	
	LOG_INF("PWM configurado com sucesso");
	return 0;
#else
	LOG_WRN("PWM não disponível nesta placa");
	return -ENOTSUP;
#endif
}

/**
 * Thread principal de controle do LED
 */
K_THREAD_DEFINE(button_thread_id, 1024, button_handler_thread, NULL, NULL, NULL, 7, 0, 0);

int main(void)
{
	int ret;
	
	printk("\n");
	printk("=================================================\n");
	printk("  Atividade 2 - Controle de LED com GPIO e PWM\n");
	printk("=================================================\n");
	printk("\n");
	
	// Inicializa LED GPIO
	ret = setup_led_gpio();
	if (ret < 0) {
		LOG_ERR("Falha na inicialização do LED");
		return ret;
	}
	
	// Inicializa botão GPIO
	ret = setup_button_gpio();
	if (ret < 0) {
		LOG_ERR("Falha na inicialização do botão");
		return ret;
	}
	
	// Inicializa PWM
	ret = setup_pwm();
	if (ret < 0 && ret != -ENOTSUP) {
		LOG_ERR("Falha na inicialização do PWM");
		return ret;
	}
	
	LOG_INF("Sistema inicializado com sucesso!");
	LOG_INF("Pressione o botão para alternar entre modos");
	LOG_INF("Modo inicial: DIGITAL (On/Off)");
	printk("\n");
	
	// Loop principal
	while (1) {
		if (current_mode == MODE_DIGITAL) {
			// Modo 1: Piscar LED digitalmente
			digital_led_blink();
			k_sleep(K_MSEC(BLINK_INTERVAL_MS));
		} else {
			// Modo 2: Fade PWM
			pwm_led_fade();
			k_sleep(K_MSEC(PWM_FADE_DELAY_MS));
		}
	}
	
	return 0;
}
