#include <zephyr/kernel.h>
#include <string.h>
#include "common.h"

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

