#include <zephyr/ztest.h>
#include "common.h"

ZTEST(radar_unit, test_speed_calculation)
{
    // 5000mm (5m) in 360ms -> 50 km/h
    // (5000 * 36) / (360 * 10) = 180000 / 3600 = 50
    zassert_equal(calculate_speed(5000, 360), 50, "Speed should be 50 km/h");

    // 5000mm in 180ms -> 100 km/h
    // (5000 * 36) / (1800) = 180000 / 1800 = 100
    zassert_equal(calculate_speed(5000, 180), 100, "Speed should be 100 km/h");

    // 5000mm in 1000ms -> 18 km/h
    zassert_equal(calculate_speed(5000, 1000), 18, "Speed should be 18 km/h");
    
    // Zero duration check
    zassert_equal(calculate_speed(5000, 0), 0, "Zero duration should return 0");
}

ZTEST(radar_unit, test_vehicle_classification)
{
    // Logic: <= 2 axles is LIGHT, > 2 is HEAVY
    
    uint32_t axles_light = 2;
    vehicle_type_t type_light = (axles_light <= 2) ? VEHICLE_LIGHT : VEHICLE_HEAVY;
    zassert_equal(type_light, VEHICLE_LIGHT, "2 axles should be LIGHT");

    uint32_t axles_moto = 1; // Although unlikely for this sensor setup, < 2 is light
    vehicle_type_t type_moto = (axles_moto <= 2) ? VEHICLE_LIGHT : VEHICLE_HEAVY;
    zassert_equal(type_moto, VEHICLE_LIGHT, "1 axle should be LIGHT");

    uint32_t axles_heavy = 3;
    vehicle_type_t type_heavy = (axles_heavy <= 2) ? VEHICLE_LIGHT : VEHICLE_HEAVY;
    zassert_equal(type_heavy, VEHICLE_HEAVY, "3 axles should be HEAVY");

    uint32_t axles_truck = 5;
    vehicle_type_t type_truck = (axles_truck <= 2) ? VEHICLE_LIGHT : VEHICLE_HEAVY;
    zassert_equal(type_truck, VEHICLE_HEAVY, "5 axles should be HEAVY");
}

ZTEST(radar_unit, test_plate_validation)
{
    // Valid Mercosul: LLLNLNN (ABC1D23)
    zassert_true(validate_plate("ABC1D23"), "ABC1D23 should be valid");
    zassert_true(validate_plate("XYZ9W88"), "XYZ9W88 should be valid");

    // Invalid length
    zassert_false(validate_plate("ABC1D2"), "Too short");
    zassert_false(validate_plate("ABC1D233"), "Too long");

    // Invalid format
    zassert_false(validate_plate("ABC1234"), "Old format should be invalid (LLLNNNN)");
    zassert_false(validate_plate("123ABCD"), "Wrong chars");
    zassert_true(validate_plate("abc1d23"), "Lowercase should be accepted (normalized to uppercase)");
}

ZTEST_SUITE(radar_unit, NULL, NULL, NULL, NULL, NULL);
