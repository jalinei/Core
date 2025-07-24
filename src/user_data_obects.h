

#include <stdint.h>
#include <string.h>

#include <thingset.h>
#include <thingset/sdk.h>

/*
 * Groups / first layer data object IDs
 */
#define ID_ROOT        0x00

/* Config */
#define ID_CONF             0x4
#define ID_CONF_POW         0x41
#define ID_CONF_IDL         0x42


/* Measurements */
#define ID_MEAS        0x5
#define ID_MEAS_V1_LOW 0x50
#define ID_MEAS_V2_LOW 0x51
#define ID_MEAS_V_HIGH 0x52
#define ID_MEAS_I1_LOW 0x53
#define ID_MEAS_I2_LOW 0x54
#define ID_MEAS_I_HIGH 0x55
#define ID_MEAS_TEMP1  0x56
#define ID_MEAS_TEMP2  0x57

/* TEST */
#define ID_TEST                    0x6
#define ID_TEST_RS485              0x60
#define ID_TEST_SYNC               0x61
#define ID_TEST_CAN                0x62
#define ID_TEST_BOOL_CAN           0x63
#define ID_TEST_ANALOG_VALUE       0x64
#define ID_TEST_ID_STATUS          0x65

/*
 * Subset definitions for statements and publish/subscribe
 */

/* UART serial */
#define SUBSET_SER  (1U << 0)
/* CAN bus */
#define SUBSET_CAN  (1U << 1)
/* Control data sent and received via CAN */
#define SUBSET_CTRL (1U << 3)

/* Measure variables */
static float32_t V1_low_value;
static float32_t V2_low_value;
static float32_t I1_low_value;
static float32_t I2_low_value;
static float32_t I_high_value;
static float32_t V_high_value;

static float32_t temp_1_value;
static float32_t temp_2_value;

void powering_power(void);
void idling_power(void);

extern uint8_t mode;

/* Temporary storage fore measured value (ctrl task) */
static float32_t meas_data;

// static THINGSET_DEFINE_FLOAT_ARRAY(ocv_points_arr, 3, ocv_points, ARRAY_SIZE(ocv_points));

/* ThingSet object definitions */

/* CONFIG object definitions */

THINGSET_ADD_GROUP(TS_ID_ROOT, ID_CONF, "Config", THINGSET_NO_CALLBACK);

THINGSET_ADD_FN_VOID(ID_CONF, ID_CONF_POW, "xPower", &powering_power,
                     THINGSET_ANY_RW);

THINGSET_ADD_FN_VOID(ID_CONF, ID_CONF_IDL, "xIdle", &idling_power,
                     THINGSET_ANY_RW);

/* Measurements object definitions */

THINGSET_ADD_GROUP(ID_ROOT, ID_MEAS, "Measurements", THINGSET_NO_CALLBACK);

THINGSET_ADD_ITEM_FLOAT(ID_MEAS, ID_MEAS_V1_LOW, "rV1Low_V", &V1_low_value, 2,
                        THINGSET_ANY_R, TS_SUBSET_LIVE);

THINGSET_ADD_ITEM_FLOAT(ID_MEAS, ID_MEAS_V2_LOW, "rV2Low_V", &V2_low_value, 2,
                        THINGSET_ANY_R, SUBSET_SER);

THINGSET_ADD_ITEM_FLOAT(ID_MEAS, ID_MEAS_V_HIGH, "rVHigh_V", &V_high_value, 2,
                        THINGSET_ANY_R, SUBSET_SER);

THINGSET_ADD_ITEM_FLOAT(ID_MEAS, ID_MEAS_I1_LOW, "rI1Low_A", &I1_low_value, 2,
                        THINGSET_ANY_R, SUBSET_SER);

THINGSET_ADD_ITEM_FLOAT(ID_MEAS, ID_MEAS_I2_LOW, "rI2Low_A", &I2_low_value, 2,
                        THINGSET_ANY_R, SUBSET_SER);

THINGSET_ADD_ITEM_FLOAT(ID_MEAS, ID_MEAS_I_HIGH, "rIHigh_A", &I_high_value, 2,
                        THINGSET_ANY_R, SUBSET_SER);

THINGSET_ADD_ITEM_FLOAT(ID_MEAS, ID_MEAS_TEMP1, "rTemp_degC", &temp_1_value, 2,
                        THINGSET_ANY_R, SUBSET_SER);

THINGSET_ADD_ITEM_FLOAT(ID_MEAS, ID_MEAS_TEMP2, "rTemp2_degC", &temp_2_value, 2,
                        THINGSET_ANY_R, SUBSET_SER);

/* TEST object definitions */

// THINGSET_ADD_GROUP(TS_ID_ROOT, ID_TEST, "Config", THINGSET_NO_CALLBACK);

// THINGSET_ADD_FN_VOID(ID_TEST, ID_TEST_RS485, "xRS485", &test_RS485,
//                      THINGSET_ANY_RW);

// THINGSET_ADD_FN_VOID(ID_TEST, ID_TEST_SYNC, "xSync", &test_SYNC,
//                      THINGSET_ANY_RW);

// THINGSET_ADD_FN_VOID(ID_TEST, ID_TEST_CAN, "xCAN", &test_CAN,
//                      THINGSET_ANY_RW);

// THINGSET_ADD_ITEM_BOOL(ID_TEST, ID_TEST_BOOL_CAN, "rTestCAN", &test_bool_can,
//                         THINGSET_ANY_R, SUBSET_SER);

// THINGSET_ADD_ITEM_FLOAT(ID_TEST, ID_TEST_ANALOG_VALUE, "rAnalog_test_V", &analog_value_measure, 2,
//                         THINGSET_ANY_R, SUBSET_SER);

// THINGSET_ADD_ITEM_INT8(ID_TEST, ID_TEST_ID_STATUS, "rID", &id_and_status,
//                         THINGSET_ANY_R, SUBSET_SER);



