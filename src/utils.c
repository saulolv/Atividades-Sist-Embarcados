#include <zephyr/kernel.h>
#include <string.h>
#include "common.h"

/**
 * Converts a character to uppercase.
 * @param c The character to convert.
 * @return The uppercase character.
 */
static inline char util_to_upper_char(char c) {
	if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 'A');
	return c;
}

/**
 * Checks if a character is a letter.
 * @param c The character to check.
 * @return True if the character is a letter, false otherwise.
 */
static inline bool util_is_letter(char c) {
	char u = util_to_upper_char(c);
	return (u >= 'A' && u <= 'Z');
}

/**
 * Checks if a character is a digit.
 * @param c The character to check.
 * @return True if the character is a digit, false otherwise.
 */
static inline bool util_is_digit(char c) {
	return (c >= '0' && c <= '9');
}

/**
 * Validates a Mercosul plate number.
 * @param plate The plate number to validate.
 * @return True if the plate number is valid, false otherwise.
 */
bool validate_plate(const char *plate) {
	if (strlen(plate) != 7) return false;
	// Check LLL
	for (int i = 0; i < 3; i++) if (!util_is_letter(plate[i])) return false;
	// Check N
	if (!util_is_digit(plate[3])) return false;
	// Check L
	if (!util_is_letter(plate[4])) return false;
	// Check NN
	for (int i = 5; i < 7; i++) if (!util_is_digit(plate[i])) return false;
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
