#ifndef INFRACTION_LOG_H
#define INFRACTION_LOG_H

#include <zephyr/kernel.h>
#include "common.h"

typedef struct {
	int64_t timestamp_ms;
	vehicle_type_t type;
	uint32_t speed_kmh;
	uint32_t limit_kmh;
	bool valid_read;
	char plate[10];
} infraction_record_t;

void infraction_log_add(const infraction_record_t *record);
size_t infraction_log_get_recent(size_t max_records, infraction_record_t *out_records);

void infraction_log_get_counters(uint32_t *light_count, uint32_t *heavy_count, uint32_t *valid_reads, uint32_t *invalid_reads);

#endif


