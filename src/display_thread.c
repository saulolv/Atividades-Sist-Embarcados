#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/display.h>
#include "common.h"

LOG_MODULE_REGISTER(display_thread, LOG_LEVEL_INF);

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_RESET   "\x1b[0m"

/**
 * Main entry point for the display thread.
 * @param p1 Pointer to the display thread data.
 * @param p2 Pointer to the display thread data.
 * @param p3 Pointer to the display thread data.
 */
void display_thread_entry(void *p1, void *p2, void *p3) {
    const struct device *display_dev = DEVICE_DT_GET(DT_NODELABEL(dummy_display));

    // Check if the display device is ready
    if (!device_is_ready(display_dev)) {
        LOG_WRN("Dummy Display not ready, proceeding with console only");
    } else {
        LOG_INF("Dummy Display Initialized");
        display_blanking_off(display_dev);
    }

    // Initialize the display data
    display_data_t data;

    while (1) {
        // Wait for a message from the display queue
        if (k_msgq_get(&display_msgq, &data, K_FOREVER) == 0) {
            const char *color = ANSI_COLOR_RESET;
            const char *status_str = "UNKNOWN";

            // Determine the color and status string based on the status
            switch (data.status) {
                case STATUS_NORMAL:
                    color = ANSI_COLOR_GREEN;
                    status_str = "NORMAL";
                    break;
                case STATUS_WARNING:
                    color = ANSI_COLOR_YELLOW;
                    status_str = "WARNING";
                    break;
                case STATUS_INFRACTION:
                    color = ANSI_COLOR_RED;
                    status_str = "INFRACTION";
                    break;
            }

            // Print the display data to the console
            printk("\n%s========================================%s\n", color, ANSI_COLOR_RESET);
            printk("%s RADAR STATUS: %s %s\n", color, status_str, ANSI_COLOR_RESET);
            if (data.limit_kmh > 0) {
                printk(" Velocidade: %d km/h\n", data.speed_kmh);
                printk(" Limite: %d km/h (Alerta \xE2\x89\xA5 %d km/h)\n", data.limit_kmh, data.warning_kmh);
            } else {
                printk(" Velocidade: %d km/h\n", data.speed_kmh);
                printk(" Limite: %d km/h\n", data.limit_kmh);
            }
            {
                const char *tipo = "Desconhecido";
                switch (data.type) {
                    case VEHICLE_LIGHT: tipo = "Leve"; break;
                    case VEHICLE_HEAVY: tipo = "Pesado"; break;
                    case VEHICLE_UNKNOWN: default: tipo = "Desconhecido"; break;
                }
                printk(" Veiculo: %s", tipo);
            }
            if (data.axle_count > 0) {
                printk(" (Eixos: %d)", data.axle_count);
            }
            printk("\n");
            
            // If the plate is not empty, print the plate
            if (data.plate[0] != '\0') {
                printk(" Placa: %s\n", data.plate);
            }
            // Print the end of the display data
            printk("%s========================================%s\n\n", color, ANSI_COLOR_RESET);
        }
    }
}

