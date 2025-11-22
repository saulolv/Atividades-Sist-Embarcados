#ifndef COMMON_H
#define COMMON_H

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

// Vehicle Types
typedef enum {
    VEHICLE_LIGHT,
    VEHICLE_HEAVY,
    VEHICLE_UNKNOWN
} vehicle_type_t;

// Data from Sensor Thread to Main Thread
typedef struct {
    int64_t timestamp_start;
    int64_t timestamp_end;
    uint32_t duration_ms;
    uint32_t axle_count;
    vehicle_type_t type;
} sensor_data_t;

// Display Status
typedef enum {
    STATUS_NORMAL,
    STATUS_WARNING,
    STATUS_INFRACTION
} display_status_t;

// Data for Display
typedef struct {
    uint32_t speed_kmh;
    uint32_t limit_kmh;
    vehicle_type_t type;
    display_status_t status;
    char plate[10]; // Optional, for result display
    uint32_t axle_count; // For UX display
    uint32_t warning_kmh; // Threshold for yellow status
} display_data_t;

// ZBUS: Camera Trigger
typedef struct {
    uint32_t speed_kmh;
    vehicle_type_t type;
} camera_trigger_t;

// ZBUS: Camera Result
typedef struct {
    char plate[10];
    bool valid_read; // If the camera successfully read a plate
} camera_result_t;

// Channels
ZBUS_CHAN_DECLARE(camera_trigger_chan);
ZBUS_CHAN_DECLARE(camera_result_chan);

// Message Queues
extern struct k_msgq sensor_msgq;
extern struct k_msgq display_msgq;

// Helper functions
bool validate_plate(const char *plate);
uint32_t calculate_speed(uint32_t distance_mm, uint32_t duration_ms);

#endif
