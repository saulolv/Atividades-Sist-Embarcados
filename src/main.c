/*
 * Multi-thread pipeline with two msg_queues:
 * - Producers (Temperature, Humidity) -> input_msgq
 * - Filter thread validates -> output_msgq (valid) or logs (invalid)
 * - Consumer reads only from output_msgq
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

enum sensor_type {
	SENSOR_TEMPERATURE = 0,
	SENSOR_HUMIDITY = 1,
};

struct sensor_message {
	enum sensor_type type;
	int value; /* Temperature: deg C, Humidity: % RH */
	uint32_t sequence;
};

K_MSGQ_DEFINE(input_msgq, sizeof(struct sensor_message), 16, 4);
K_MSGQ_DEFINE(output_msgq, sizeof(struct sensor_message), 16, 4);


/**
 * @brief Convert sensor type to string
 * 
 * @param type Sensor type
 * @return const char* String representation of sensor type
 */
static const char *sensor_type_to_str(enum sensor_type type)
{
	switch (type) {
	case SENSOR_TEMPERATURE:
		return "temperature";
	case SENSOR_HUMIDITY:
		return "humidity";
	default:
		return "unknown";
	}
}

/**
 * @brief Validate sensor message
 * 
 * @param msg Sensor message
 * @return true if message is valid, false otherwise
 */
static bool validate_message(const struct sensor_message *msg)
{
	if (msg->type == SENSOR_TEMPERATURE) {
		return (msg->value >= 18) && (msg->value <= 30);
	}
	if (msg->type == SENSOR_HUMIDITY) {
		return (msg->value >= 40) && (msg->value <= 70);
	}
	return false;
}

/**
 * @brief Temperature producer
 * 
 * @param p1 Pointer to unused parameters
 * @param p2 Pointer to unused parameters
 * @param p3 Pointer to unused parameters
 */
static void temperature_producer(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	static const int temp_values[] = { 22, 17, 29, 31, 26 };
	uint32_t seq = 0;
	size_t idx = 0;

	while (true) {
		struct sensor_message msg = {
			.type = SENSOR_TEMPERATURE,
			.value = temp_values[idx],
			.sequence = seq++,
		};
		k_msgq_put(&input_msgq, &msg, K_FOREVER);
		LOG_INF("Producer[T]: %s=%d (seq=%u)", sensor_type_to_str(msg.type), msg.value, msg.sequence);
		idx = (idx + 1) % ARRAY_SIZE(temp_values);
		k_msleep(800);
	}
}

/**
 * @brief Humidity producer
 * 
 * @param p1 Pointer to unused parameters
 * @param p2 Pointer to unused parameters
 * @param p3 Pointer to unused parameters
 */
static void humidity_producer(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	static const int rh_values[] = { 45, 35, 60, 75, 50 };
	uint32_t seq = 0;
	size_t idx = 0;

	while (true) {
		struct sensor_message msg = {
			.type = SENSOR_HUMIDITY,
			.value = rh_values[idx],
			.sequence = seq++,
		};
		k_msgq_put(&input_msgq, &msg, K_FOREVER);
		LOG_INF("Producer[H]: %s=%d (seq=%u)", sensor_type_to_str(msg.type), msg.value, msg.sequence);
		idx = (idx + 1) % ARRAY_SIZE(rh_values);
		k_msleep(1000);
	}
}

/**
 * @brief Filter thread
 * 
 * @param p1 Pointer to unused parameters
 * @param p2 Pointer to unused parameters
 * @param p3 Pointer to unused parameters
 */
static void filter_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		struct sensor_message msg;
		k_msgq_get(&input_msgq, &msg, K_FOREVER);

		if (validate_message(&msg)) {
			k_msgq_put(&output_msgq, &msg, K_FOREVER);
			LOG_INF("Filter: valid %s=%d (seq=%u)", sensor_type_to_str(msg.type), msg.value, msg.sequence);
		} else {
			LOG_WRN("Filter: INVALID %s=%d (seq=%u)", sensor_type_to_str(msg.type), msg.value, msg.sequence);
		}
	}
}

/**
 * @brief Consumer thread
 * 
 * @param p1 Pointer to unused parameters
 * @param p2 Pointer to unused parameters
 * @param p3 Pointer to unused parameters
 */
static void consumer_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		struct sensor_message msg;
		k_msgq_get(&output_msgq, &msg, K_FOREVER);
		LOG_INF("Consumer: storing %s=%d (seq=%u)", sensor_type_to_str(msg.type), msg.value, msg.sequence);
		/* Simulate processing time */
		k_msleep(200);
	}
}

#define STACK_SIZE 1024
#define PRIORITY 7

K_THREAD_DEFINE(temp_producer_tid, STACK_SIZE, temperature_producer, NULL, NULL, NULL, PRIORITY, 0, 0);
K_THREAD_DEFINE(humid_producer_tid, STACK_SIZE, humidity_producer, NULL, NULL, NULL, PRIORITY, 0, 0);
K_THREAD_DEFINE(filter_tid, STACK_SIZE, filter_thread, NULL, NULL, NULL, PRIORITY, 0, 0);
K_THREAD_DEFINE(consumer_tid, STACK_SIZE, consumer_thread, NULL, NULL, NULL, PRIORITY, 0, 0);

void main(void)
{
	LOG_INF("Starting pipeline: producers -> input_msgq -> filter -> output_msgq -> consumer");
}


