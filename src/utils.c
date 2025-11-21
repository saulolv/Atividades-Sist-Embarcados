#include <zephyr/kernel.h>
#include <string.h>
#include "common.h"

/**
 * Validates a Mercosul plate number.
 * @param plate The plate number to validate.
 * @return True if the plate number is valid, false otherwise.
 */
bool validate_plate(const char *plate) {
    if (strlen(plate) != 7) return false;
    // Check LLL
    for (int i = 0; i < 3; i++) if (plate[i] < 'A' || plate[i] > 'Z') return false;
    // Check N
    if (plate[3] < '0' || plate[3] > '9') return false;
    // Check L
    if (plate[4] < 'A' || plate[4] > 'Z') return false;
    // Check NN
    for (int i = 5; i < 7; i++) if (plate[i] < '0' || plate[i] > '9') return false;
    return true;
}


/**
 * Calculates the speed in km/h based on the distance and duration.
 * @param distance_mm The distance in millimeters.
 * @param duration_ms The duration in milliseconds.
 * @return The speed in km/h.
 */
uint32_t calculate_speed(uint32_t distance_mm, uint32_t duration_ms) {
    if (duration_ms == 0) return 0;
    // Speed (km/h) = (dist_mm / time_ms) * 3.6
    // = (dist * 36) / (time * 10)
    return (uint32_t)(((uint64_t)distance_mm * 36) / (duration_ms * 10));
}
