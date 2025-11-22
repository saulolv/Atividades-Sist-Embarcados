#include "infraction_log.h"
#include <string.h>

#ifndef CONFIG_RADAR_INFRACTION_LOG_SIZE
#define CONFIG_RADAR_INFRACTION_LOG_SIZE 32
#endif

static infraction_record_t records[CONFIG_RADAR_INFRACTION_LOG_SIZE];
static size_t head_index;
static size_t total_count;
static uint32_t count_light;
static uint32_t count_heavy;
static uint32_t count_valid_read;
static uint32_t count_invalid_read;
static struct k_spinlock log_lock;

/**
 * Adds an infraction record to the log.
 * @param record The infraction record to add.
 */
void infraction_log_add(const infraction_record_t *record)
{
	k_spinlock_key_t key = k_spin_lock(&log_lock);

	records[head_index] = *record;
	head_index = (head_index + 1) % CONFIG_RADAR_INFRACTION_LOG_SIZE;
	if (total_count < CONFIG_RADAR_INFRACTION_LOG_SIZE) {
		total_count++;
	}

	if (record->type == VEHICLE_HEAVY) {
		count_heavy++;
	} else if (record->type == VEHICLE_LIGHT) {
		count_light++;
	}
	if (record->valid_read) {
		count_valid_read++;
	} else {
		count_invalid_read++;
	}

	k_spin_unlock(&log_lock, key);
}

/**
 * Gets the most recent infraction records from the log.
 * @param max_records The maximum number of records to get.
 * @param out_records The array to store the records.
 * @return The number of records copied.
 */
size_t infraction_log_get_recent(size_t max_records, infraction_record_t *out_records)
{
	if (max_records == 0 || out_records == NULL) {
		return 0;
	}

	k_spinlock_key_t key = k_spin_lock(&log_lock);

	size_t available = total_count;
	if (available > CONFIG_RADAR_INFRACTION_LOG_SIZE) {
		available = CONFIG_RADAR_INFRACTION_LOG_SIZE;
	}
	size_t to_copy = (max_records < available) ? max_records : available;

	/* Copy from newest to oldest into out_records[0..to_copy-1] */
	for (size_t i = 0; i < to_copy; i++) {
		/* Newest element is at (head_index - 1 + size) % size */
		size_t idx = (head_index + CONFIG_RADAR_INFRACTION_LOG_SIZE - 1 - i) % CONFIG_RADAR_INFRACTION_LOG_SIZE;
		out_records[i] = records[idx];
	}

	k_spin_unlock(&log_lock, key);
	return to_copy;
}

/**
 * Gets the counters for the infraction log.
 * @param light_count The count of light vehicles.
 * @param heavy_count Pointer to the count of heavy vehicles.
 * @param valid_reads Pointer to the count of valid reads.
 * @param invalid_reads Pointer to the count of invalid reads.
 */
void infraction_log_get_counters(uint32_t *light_count, uint32_t *heavy_count, uint32_t *valid_reads, uint32_t *invalid_reads)
{
	k_spinlock_key_t key = k_spin_lock(&log_lock);
	if (light_count) {
		*light_count = count_light;
	}
	if (heavy_count) {
		*heavy_count = count_heavy;
	}
	if (valid_reads) {
		*valid_reads = count_valid_read;
	}
	if (invalid_reads) {
		*invalid_reads = count_invalid_read;
	}
	k_spin_unlock(&log_lock, key);
}


