#ifndef SPIN_DATA_OBJECTS_H
#define SPIN_DATA_OBJECTS_H

#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#include <thingset.h>
#include <thingset/sdk.h>

#include "SpinAPI.h"
#include "hrtim_enum.h"

/*
 * ThingSet exposure for Spin API (GPIO, PWM, DAC, ADC data, etc.).
 * Each group uses writable command fields plus a small callback that
 * invokes the matching Spin API entry points when changed remotely.
 */

#ifndef SPIN_TS_NO_SUBSET
#define SPIN_TS_NO_SUBSET 0
#endif

/* Top-level Spin group */
#define ID_SPIN              0x70

/* Sub-groups */
#define ID_SPIN_LED          0x701
#define ID_SPIN_GPIO         0x702
#define ID_SPIN_DATA         0x703
#define ID_SPIN_PWM          0x704
#define ID_SPIN_DAC          0x705
#define ID_SPIN_COMP         0x706
#define ID_SPIN_TIMER        0x707
#define ID_SPIN_UART         0x708
#define ID_SPIN_NGND         0x709

THINGSET_ADD_GROUP(TS_ID_ROOT, ID_SPIN, "Spin", THINGSET_NO_CALLBACK);

/* =========================================================================
 * LED
 * ========================================================================= */

static bool spin_led_on = false;
static bool spin_led_toggle = false;

static int spin_led_cb(enum thingset_callback_reason reason,
                       const thingset_data_object *obj)
{
    (void)obj;
    if (reason != THINGSET_CALLBACK_POST_WRITE) {
        return 0;
    }

    if (spin_led_on) {
        spin.led.turnOn();
    } else {
        spin.led.turnOff();
    }

    if (spin_led_toggle) {
        spin.led.toggle();
        spin_led_toggle = false;
    }
    return 0;
}

THINGSET_ADD_GROUP(ID_SPIN, ID_SPIN_LED, "Led", &spin_led_cb);
THINGSET_ADD_ITEM_BOOL(ID_SPIN_LED, 0x7011, "wOn", &spin_led_on, THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_BOOL(ID_SPIN_LED, 0x7012, "xToggle", &spin_led_toggle, THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);

#ifdef CONFIG_OWNTECH_GPIO_API
/* =========================================================================
 * GPIO
 * ========================================================================= */

typedef struct {
    uint8_t  pin;
    uint32_t flags;
    uint8_t  write_value;
    uint8_t  read_value;
    bool     cmd_configure;
    bool     cmd_set;
    bool     cmd_reset;
    bool     cmd_toggle;
    bool     cmd_write;
    bool     cmd_read;
} spin_gpio_cmd_t;

static spin_gpio_cmd_t spin_gpio_cmd = {
    .pin          = 0,
    .flags        = 0,
    .write_value  = 0,
    .read_value   = 0,
    .cmd_configure= false,
    .cmd_set      = false,
    .cmd_reset    = false,
    .cmd_toggle   = false,
    .cmd_write    = false,
    .cmd_read     = false
};

static int spin_gpio_cb(enum thingset_callback_reason reason,
                        const thingset_data_object *obj)
{
    (void)obj;
    if (reason != THINGSET_CALLBACK_POST_WRITE) {
        return 0;
    }

    if (spin_gpio_cmd.cmd_configure) {
        spin.gpio.configurePin(spin_gpio_cmd.pin, (gpio_flags_t)spin_gpio_cmd.flags);
        spin_gpio_cmd.cmd_configure = false;
    }
    if (spin_gpio_cmd.cmd_set) {
        spin.gpio.setPin(spin_gpio_cmd.pin);
        spin_gpio_cmd.cmd_set = false;
    }
    if (spin_gpio_cmd.cmd_reset) {
        spin.gpio.resetPin(spin_gpio_cmd.pin);
        spin_gpio_cmd.cmd_reset = false;
    }
    if (spin_gpio_cmd.cmd_toggle) {
        spin.gpio.togglePin(spin_gpio_cmd.pin);
        spin_gpio_cmd.cmd_toggle = false;
    }
    if (spin_gpio_cmd.cmd_write) {
        spin.gpio.writePin(spin_gpio_cmd.pin, spin_gpio_cmd.write_value);
        spin_gpio_cmd.cmd_write = false;
    }
    if (spin_gpio_cmd.cmd_read) {
        spin_gpio_cmd.read_value = spin.gpio.readPin(spin_gpio_cmd.pin);
        spin_gpio_cmd.cmd_read = false;
    }
    return 0;
}

THINGSET_ADD_GROUP(ID_SPIN, ID_SPIN_GPIO, "Gpio", &spin_gpio_cb);
THINGSET_ADD_ITEM_UINT8 (ID_SPIN_GPIO, 0x7021, "wPin",        &spin_gpio_cmd.pin,         THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT32(ID_SPIN_GPIO, 0x7022, "wModeFlags",  &spin_gpio_cmd.flags,       THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_BOOL  (ID_SPIN_GPIO, 0x7023, "xConfigure",  &spin_gpio_cmd.cmd_configure, THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_BOOL  (ID_SPIN_GPIO, 0x7024, "xSet",        &spin_gpio_cmd.cmd_set,     THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_BOOL  (ID_SPIN_GPIO, 0x7025, "xReset",      &spin_gpio_cmd.cmd_reset,   THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_BOOL  (ID_SPIN_GPIO, 0x7026, "xToggle",     &spin_gpio_cmd.cmd_toggle,  THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT8 (ID_SPIN_GPIO, 0x7027, "wValue",      &spin_gpio_cmd.write_value, THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_BOOL  (ID_SPIN_GPIO, 0x7028, "xWrite",      &spin_gpio_cmd.cmd_write,   THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_BOOL  (ID_SPIN_GPIO, 0x7029, "xRead",       &spin_gpio_cmd.cmd_read,    THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT8 (ID_SPIN_GPIO, 0x702A, "rValue",      &spin_gpio_cmd.read_value,  THINGSET_ANY_R,  SPIN_TS_NO_SUBSET);
#endif

/* =========================================================================
 * Data API (ADC)
 * ========================================================================= */

typedef enum : uint8_t {
    DATA_ACT_ENABLE = 0,
    DATA_ACT_START,
    DATA_ACT_STOP,
    DATA_ACT_TRIGGER,
    DATA_ACT_SET_LINEAR,
    DATA_ACT_SET_NTC,
    DATA_ACT_STORE,
    DATA_ACT_RETRIEVE,
    DATA_ACT_PEEK,
    DATA_ACT_GET_LATEST,
    DATA_ACT_CONVERT,
    DATA_ACT_GET_PARAM,
    DATA_ACT_GET_TYPE,
    DATA_ACT_SET_DISCONT,
    DATA_ACT_SET_TRIG_SRC,
    DATA_ACT_REFRESH_STATE
} spin_data_action_t;

typedef struct {
    uint8_t  pin;
    int8_t   adc;
    uint8_t  trigger_source;
    uint32_t discontinuous_count;
    float32_t gain;
    float32_t offset;
    float32_t r0;
    float32_t b;
    float32_t rdiv;
    float32_t t0;
    uint16_t raw_value;
    uint8_t  conv_param_name;
    float32_t latest_value;
    float32_t converted_value;
    float32_t conv_param_value;
    int32_t  conv_param_type;
    uint8_t  data_valid;
    int8_t   status;
    bool     started;
    spin_data_action_t action;
    bool     exec;
} spin_data_cmd_t;

static spin_data_cmd_t spin_data_cmd = {
    .pin                 = 0,
    .adc                 = DEFAULT_ADC,
    .trigger_source      = TRIG_SOFTWARE,
    .discontinuous_count = 0,
    .gain                = 1.0f,
    .offset              = 0.0f,
    .r0                  = 0.0f,
    .b                   = 0.0f,
    .rdiv                = 0.0f,
    .t0                  = 25.0f,
    .raw_value           = 0,
    .conv_param_name     = 0,
    .latest_value        = NO_VALUE,
    .converted_value     = NO_VALUE,
    .conv_param_value    = NO_VALUE,
    .conv_param_type     = -5,
    .data_valid          = DATA_IS_MISSING,
    .status              = 0,
    .started             = false,
    .action              = DATA_ACT_ENABLE,
    .exec                = false
};

static int spin_data_cb(enum thingset_callback_reason reason,
                        const thingset_data_object *obj)
{
    (void)obj;
    if (reason != THINGSET_CALLBACK_POST_WRITE || !spin_data_cmd.exec) {
        return 0;
    }

    spin_data_cmd.exec = false;

    switch (spin_data_cmd.action) {
    case DATA_ACT_ENABLE:
        spin_data_cmd.status = spin.data.enableAcquisition(
            spin_data_cmd.pin, (adc_t)spin_data_cmd.adc);
        break;
    case DATA_ACT_START:
        spin_data_cmd.status = spin.data.start();
        break;
    case DATA_ACT_STOP:
        spin_data_cmd.status = spin.data.stop();
        break;
    case DATA_ACT_TRIGGER:
        spin.data.triggerAcquisition((adc_t)spin_data_cmd.adc);
        break;
    case DATA_ACT_SET_LINEAR:
        spin.data.setConversionParametersLinear(spin_data_cmd.pin,
                                                spin_data_cmd.gain,
                                                spin_data_cmd.offset);
        break;
    case DATA_ACT_SET_NTC:
        spin.data.setConversionParametersNtcThermistor(spin_data_cmd.pin,
                                                       spin_data_cmd.r0,
                                                       spin_data_cmd.b,
                                                       spin_data_cmd.rdiv,
                                                       spin_data_cmd.t0);
        break;
    case DATA_ACT_STORE:
        spin_data_cmd.status = spin.data.storeConversionParametersInMemory(spin_data_cmd.pin);
        break;
    case DATA_ACT_RETRIEVE:
        spin_data_cmd.status = spin.data.retrieveConversionParametersFromMemory(spin_data_cmd.pin);
        break;
    case DATA_ACT_PEEK:
        spin_data_cmd.latest_value = spin.data.peekLatestValue(spin_data_cmd.pin);
        break;
    case DATA_ACT_GET_LATEST:
        spin_data_cmd.latest_value = spin.data.getLatestValue(spin_data_cmd.pin, &spin_data_cmd.data_valid);
        break;
    case DATA_ACT_CONVERT:
        spin_data_cmd.converted_value = spin.data.convertValue(spin_data_cmd.pin, spin_data_cmd.raw_value);
        break;
    case DATA_ACT_GET_PARAM:
        spin_data_cmd.conv_param_value = spin.data.getConversionParameterValue(
            spin_data_cmd.pin, (parameter_t)spin_data_cmd.conv_param_name);
        break;
    case DATA_ACT_GET_TYPE:
        spin_data_cmd.conv_param_type = spin.data.getConversionParameterType(spin_data_cmd.pin);
        break;
    case DATA_ACT_SET_DISCONT:
        spin.data.configureDiscontinuousMode((adc_t)spin_data_cmd.adc,
                                             spin_data_cmd.discontinuous_count);
        break;
    case DATA_ACT_SET_TRIG_SRC:
        spin.data.configureTriggerSource((adc_t)spin_data_cmd.adc,
                                         (trigger_source_t)spin_data_cmd.trigger_source);
        break;
    case DATA_ACT_REFRESH_STATE:
        spin_data_cmd.started = spin.data.started();
        break;
    default:
        break;
    }
    return 0;
}

THINGSET_ADD_GROUP(ID_SPIN, ID_SPIN_DATA, "Data", &spin_data_cb);
THINGSET_ADD_ITEM_UINT8 (ID_SPIN_DATA, 0x7101, "wPin",        &spin_data_cmd.pin,         THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_INT8  (ID_SPIN_DATA, 0x7102, "wAdc",        &spin_data_cmd.adc,         THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT8 (ID_SPIN_DATA, 0x7103, "wAction",     (uint8_t*)&spin_data_cmd.action, THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_BOOL  (ID_SPIN_DATA, 0x7104, "xExec",       &spin_data_cmd.exec,        THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT8 (ID_SPIN_DATA, 0x7105, "wTrigSrc",    &spin_data_cmd.trigger_source, THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT32(ID_SPIN_DATA, 0x7106, "wDiscontCnt", &spin_data_cmd.discontinuous_count, THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_FLOAT (ID_SPIN_DATA, 0x7107, "wGain",       &spin_data_cmd.gain,        4, THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_FLOAT (ID_SPIN_DATA, 0x7108, "wOffset",     &spin_data_cmd.offset,      4, THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_FLOAT (ID_SPIN_DATA, 0x7109, "wR0",         &spin_data_cmd.r0,          4, THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_FLOAT (ID_SPIN_DATA, 0x710A, "wB",          &spin_data_cmd.b,           4, THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_FLOAT (ID_SPIN_DATA, 0x710B, "wRdiv",       &spin_data_cmd.rdiv,        4, THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_FLOAT (ID_SPIN_DATA, 0x710C, "wT0_C",       &spin_data_cmd.t0,          2, THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT16(ID_SPIN_DATA, 0x710D, "wRaw",        &spin_data_cmd.raw_value,   THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT8 (ID_SPIN_DATA, 0x710E, "wConvParam",  &spin_data_cmd.conv_param_name, THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_INT8  (ID_SPIN_DATA, 0x710F, "rStatus",     &spin_data_cmd.status,      THINGSET_ANY_R,  SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_FLOAT (ID_SPIN_DATA, 0x7110, "rLatest",     &spin_data_cmd.latest_value,3, THINGSET_ANY_R, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT8 (ID_SPIN_DATA, 0x7111, "rValid",      &spin_data_cmd.data_valid,  THINGSET_ANY_R,  SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_FLOAT (ID_SPIN_DATA, 0x7112, "rConverted",  &spin_data_cmd.converted_value, 3, THINGSET_ANY_R, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_FLOAT (ID_SPIN_DATA, 0x7113, "rConvParamVal", &spin_data_cmd.conv_param_value, 4, THINGSET_ANY_R, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_INT32 (ID_SPIN_DATA, 0x7114, "rConvType",   &spin_data_cmd.conv_param_type, THINGSET_ANY_R, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_BOOL  (ID_SPIN_DATA, 0x7115, "rStarted",    &spin_data_cmd.started,   THINGSET_ANY_R, SPIN_TS_NO_SUBSET);

/* =========================================================================
 * PWM / HRTIM
 * ========================================================================= */

typedef enum : uint8_t {
    PWM_ACT_INIT_UNIT = 0,
    PWM_ACT_START_DUAL,
    PWM_ACT_STOP_DUAL,
    PWM_ACT_START_SINGLE,
    PWM_ACT_STOP_SINGLE,
    PWM_ACT_SET_MODULATION,
    PWM_ACT_SET_SWITCH,
    PWM_ACT_INIT_FIXED_FREQ,
    PWM_ACT_INIT_VARIABLE_FREQ,
    PWM_ACT_SET_DEADTIME,
    PWM_ACT_SET_DUTY,
    PWM_ACT_SET_DUTY_RAW,
    PWM_ACT_SET_PHASE,
    PWM_ACT_SET_MODE,
    PWM_ACT_SET_EEV,
    PWM_ACT_SET_ADC_TRIGGER,
    PWM_ACT_ENABLE_ADC_TRIG,
    PWM_ACT_DISABLE_ADC_TRIG,
    PWM_ACT_SET_ADC_INSTANT,
    PWM_ACT_SET_ADC_EDGE,
    PWM_ACT_SET_ADC_DECIM,
    PWM_ACT_SET_ADC_PS,
    PWM_ACT_DISABLE_PERIOD_EVT,
    PWM_ACT_SET_PERIOD_EVT_REP,
    PWM_ACT_CONFIG_PERIOD_EVT,
    PWM_ACT_ENABLE_PERIOD_EVT,
    PWM_ACT_SET_FREQUENCY,
    PWM_ACT_INIT_BURST,
    PWM_ACT_SET_BURST,
    PWM_ACT_START_BURST,
    PWM_ACT_STOP_BURST,
    PWM_ACT_DEINIT_BURST,
    PWM_ACT_REFRESH_READBACK
} spin_pwm_action_t;

typedef struct {
    uint8_t  tu;
    uint8_t  output;
    uint8_t  modulation;
    uint8_t  switch_conv;
    uint16_t dead_rise_ns;
    uint16_t dead_fall_ns;
    float32_t duty;
    uint16_t duty_raw;
    int16_t  phase_deg;
    uint8_t  pwm_mode;
    uint8_t  eev;
    uint32_t freq_hz;
    uint32_t init_freq;
    uint32_t min_freq;
    uint32_t adc_ps;
    uint8_t  adc_target;
    float32_t adc_inst;
    uint8_t  adc_edge;
    uint32_t adc_decim;
    uint32_t repetition;
    uint32_t burst_cmp;
    uint32_t burst_per;
    uint32_t period_ticks;
    uint32_t period_max;
    uint32_t period_min;
    uint32_t period_us;
    uint32_t freq_min_read;
    uint32_t freq_max_read;
    uint32_t resolution_ps;
    uint32_t period_evt_rep;
    uint8_t  mode_readback;
    uint8_t  mod_readback;
    uint8_t  switch_readback;
    uint8_t  eev_readback;
    uint8_t  adc_trig_readback;
    uint8_t  adc_edge_readback;
    spin_pwm_action_t action;
    bool     exec;
} spin_pwm_cmd_t;

static void spin_pwm_period_callback(void)
{
    /* placeholder callback for configurePeriodEvnt */
}

static spin_pwm_cmd_t spin_pwm_cmd = {
    .tu                 = 0,
    .output             = 0,
    .modulation         = 0,
    .switch_conv        = 0,
    .dead_rise_ns       = 0,
    .dead_fall_ns       = 0,
    .duty               = 0.0f,
    .duty_raw           = 0,
    .phase_deg          = 0,
    .pwm_mode           = 0,
    .eev                = 0,
    .freq_hz            = 0,
    .init_freq          = 0,
    .min_freq           = 0,
    .adc_ps             = 0,
    .adc_target         = 0,
    .adc_inst           = 0.0f,
    .adc_edge           = 0,
    .adc_decim          = 1,
    .repetition         = 0,
    .burst_cmp          = 0,
    .burst_per          = 0,
    .period_ticks       = 0,
    .period_max         = 0,
    .period_min         = 0,
    .period_us          = 0,
    .freq_min_read      = 0,
    .freq_max_read      = 0,
    .resolution_ps      = 0,
    .period_evt_rep     = 0,
    .mode_readback      = 0,
    .mod_readback       = 0,
    .switch_readback    = 0,
    .eev_readback       = 0,
    .adc_trig_readback  = 0,
    .adc_edge_readback  = 0,
    .action             = PWM_ACT_INIT_UNIT,
    .exec               = false
};

static int spin_pwm_cb(enum thingset_callback_reason reason,
                       const thingset_data_object *obj)
{
    (void)obj;
    if (reason != THINGSET_CALLBACK_POST_WRITE || !spin_pwm_cmd.exec) {
        return 0;
    }

    spin_pwm_cmd.exec = false;

    /* Basic guards to avoid invalid TU index or out-of-range params causing faults */
    if (spin_pwm_cmd.tu >= HRTIM_CHANNELS) {
        return -EINVAL;
    }
    if (spin_pwm_cmd.duty < 0.0f) {
        spin_pwm_cmd.duty = 0.0f;
    } else if (spin_pwm_cmd.duty > 1.0f) {
        spin_pwm_cmd.duty = 1.0f;
    }
    if (spin_pwm_cmd.dead_rise_ns > 10000) {
        spin_pwm_cmd.dead_rise_ns = 10000;
    }
    if (spin_pwm_cmd.dead_fall_ns > 10000) {
        spin_pwm_cmd.dead_fall_ns = 10000;
    }
    if (spin_pwm_cmd.phase_deg > 180) {
        spin_pwm_cmd.phase_deg = 180;
    } else if (spin_pwm_cmd.phase_deg < -180) {
        spin_pwm_cmd.phase_deg = -180;
    }

    switch (spin_pwm_cmd.action) {
    case PWM_ACT_INIT_UNIT:
        spin.pwm.initUnit((hrtim_tu_number_t)spin_pwm_cmd.tu);
        break;
    case PWM_ACT_START_DUAL:
        spin.pwm.startDualOutput((hrtim_tu_number_t)spin_pwm_cmd.tu);
        break;
    case PWM_ACT_STOP_DUAL:
        spin.pwm.stopDualOutput((hrtim_tu_number_t)spin_pwm_cmd.tu);
        break;
    case PWM_ACT_START_SINGLE:
        spin.pwm.startSingleOutput((hrtim_tu_number_t)spin_pwm_cmd.tu,
                                   (hrtim_output_number_t)spin_pwm_cmd.output);
        break;
    case PWM_ACT_STOP_SINGLE:
        spin.pwm.stopSingleOutput((hrtim_tu_number_t)spin_pwm_cmd.tu,
                                  (hrtim_output_number_t)spin_pwm_cmd.output);
        break;
    case PWM_ACT_SET_MODULATION:
        if (spin_pwm_cmd.modulation > 1) {
            spin_pwm_cmd.modulation = 1;
        }
        spin.pwm.setModulation((hrtim_tu_number_t)spin_pwm_cmd.tu,
                               (hrtim_cnt_t)spin_pwm_cmd.modulation);
        break;
    case PWM_ACT_SET_SWITCH:
        if (spin_pwm_cmd.switch_conv > 1) {
            spin_pwm_cmd.switch_conv = 1;
        }
        spin.pwm.setSwitchConvention((hrtim_tu_number_t)spin_pwm_cmd.tu,
                                     (hrtim_switch_convention_t)spin_pwm_cmd.switch_conv);
        break;
    case PWM_ACT_INIT_FIXED_FREQ:
        spin.pwm.initFixedFrequency(spin_pwm_cmd.freq_hz);
        break;
    case PWM_ACT_INIT_VARIABLE_FREQ:
        spin.pwm.initVariableFrequency(spin_pwm_cmd.init_freq, spin_pwm_cmd.min_freq);
        break;
    case PWM_ACT_SET_DEADTIME:
        spin.pwm.setDeadTime((hrtim_tu_number_t)spin_pwm_cmd.tu,
                             spin_pwm_cmd.dead_rise_ns,
                             spin_pwm_cmd.dead_fall_ns);
        break;
    case PWM_ACT_SET_DUTY:
        spin.pwm.setDutyCycle((hrtim_tu_number_t)spin_pwm_cmd.tu, spin_pwm_cmd.duty);
        break;
    case PWM_ACT_SET_DUTY_RAW:
        spin.pwm.setDutyCycleRaw((hrtim_tu_number_t)spin_pwm_cmd.tu, spin_pwm_cmd.duty_raw);
        break;
    case PWM_ACT_SET_PHASE:
        spin.pwm.setPhaseShift((hrtim_tu_number_t)spin_pwm_cmd.tu, spin_pwm_cmd.phase_deg);
        break;
    case PWM_ACT_SET_MODE:
        if (spin_pwm_cmd.pwm_mode > 1) {
            spin_pwm_cmd.pwm_mode = 1;
        }
        spin.pwm.setMode((hrtim_tu_number_t)spin_pwm_cmd.tu, (hrtim_pwm_mode_t)spin_pwm_cmd.pwm_mode);
        break;
    case PWM_ACT_SET_EEV:
        spin.pwm.setEev((hrtim_tu_number_t)spin_pwm_cmd.tu,
                        (hrtim_external_trigger_t)spin_pwm_cmd.eev);
        break;
    case PWM_ACT_SET_ADC_TRIGGER:
        spin.pwm.setAdcTrigger((hrtim_tu_number_t)spin_pwm_cmd.tu, (adc_t)spin_pwm_cmd.adc_target);
        break;
    case PWM_ACT_ENABLE_ADC_TRIG:
        spin.pwm.enableAdcTrigger((hrtim_tu_number_t)spin_pwm_cmd.tu);
        break;
    case PWM_ACT_DISABLE_ADC_TRIG:
        spin.pwm.disableAdcTrigger((hrtim_tu_number_t)spin_pwm_cmd.tu);
        break;
    case PWM_ACT_SET_ADC_INSTANT:
        spin.pwm.setAdcTriggerInstant((hrtim_tu_number_t)spin_pwm_cmd.tu, spin_pwm_cmd.adc_inst);
        break;
    case PWM_ACT_SET_ADC_EDGE:
        spin.pwm.setAdcEdgeTrigger((hrtim_tu_number_t)spin_pwm_cmd.tu,
                                   (hrtim_adc_edgetrigger_t)spin_pwm_cmd.adc_edge);
        break;
    case PWM_ACT_SET_ADC_DECIM:
        spin.pwm.setAdcDecimation((hrtim_tu_number_t)spin_pwm_cmd.tu, spin_pwm_cmd.adc_decim);
        break;
    case PWM_ACT_SET_ADC_PS:
        spin.pwm.setAdcTriggerPostScaler((hrtim_tu_number_t)spin_pwm_cmd.tu, spin_pwm_cmd.adc_ps);
        break;
    case PWM_ACT_DISABLE_PERIOD_EVT:
        spin.pwm.disablePeriodEvnt((hrtim_tu_t)spin_pwm_cmd.tu);
        break;
    case PWM_ACT_SET_PERIOD_EVT_REP:
        spin.pwm.setPeriodEvntRep((hrtim_tu_t)spin_pwm_cmd.tu, spin_pwm_cmd.repetition);
        break;
    case PWM_ACT_CONFIG_PERIOD_EVT:
        spin.pwm.configurePeriodEvnt((hrtim_tu_t)spin_pwm_cmd.tu,
                                     spin_pwm_cmd.repetition,
                                     &spin_pwm_period_callback);
        break;
    case PWM_ACT_ENABLE_PERIOD_EVT:
        spin.pwm.enablePeriodEvnt((hrtim_tu_t)spin_pwm_cmd.tu);
        break;
    case PWM_ACT_SET_FREQUENCY:
        spin.pwm.setFrequency(spin_pwm_cmd.freq_hz);
        break;
    case PWM_ACT_INIT_BURST:
        spin.pwm.initBurstMode();
        break;
    case PWM_ACT_SET_BURST:
        spin.pwm.setBurstMode((int)spin_pwm_cmd.burst_cmp, (int)spin_pwm_cmd.burst_per);
        break;
    case PWM_ACT_START_BURST:
        spin.pwm.startBurstMode();
        break;
    case PWM_ACT_STOP_BURST:
        spin.pwm.stopBurstMode();
        break;
    case PWM_ACT_DEINIT_BURST:
        spin.pwm.deInitBurstMode();
        break;
    case PWM_ACT_REFRESH_READBACK:
        spin_pwm_cmd.period_ticks    = spin.pwm.getPeriod((hrtim_tu_number_t)spin_pwm_cmd.tu);
        spin_pwm_cmd.period_max      = spin.pwm.getPeriodMax((hrtim_tu_number_t)spin_pwm_cmd.tu);
        spin_pwm_cmd.period_min      = spin.pwm.getPeriodMin((hrtim_tu_number_t)spin_pwm_cmd.tu);
        spin_pwm_cmd.period_us       = spin.pwm.getPeriodUs((hrtim_tu_number_t)spin_pwm_cmd.tu);
        spin_pwm_cmd.freq_min_read   = spin.pwm.getFrequencyMin((hrtim_tu_number_t)spin_pwm_cmd.tu);
        spin_pwm_cmd.freq_max_read   = spin.pwm.getFrequencyMax((hrtim_tu_number_t)spin_pwm_cmd.tu);
        spin_pwm_cmd.resolution_ps   = spin.pwm.getResolutionPs((hrtim_tu_number_t)spin_pwm_cmd.tu);
        spin_pwm_cmd.period_evt_rep  = spin.pwm.getPeriodEvntRep((hrtim_tu_t)spin_pwm_cmd.tu);
        spin_pwm_cmd.mode_readback   = spin.pwm.getMode((hrtim_tu_number_t)spin_pwm_cmd.tu);
        spin_pwm_cmd.mod_readback    = spin.pwm.getModulation((hrtim_tu_number_t)spin_pwm_cmd.tu);
        spin_pwm_cmd.switch_readback = spin.pwm.getSwitchConvention((hrtim_tu_number_t)spin_pwm_cmd.tu);
        spin_pwm_cmd.eev_readback    = spin.pwm.getEev((hrtim_tu_number_t)spin_pwm_cmd.tu);
        spin_pwm_cmd.adc_trig_readback = spin.pwm.getAdcTrigger((hrtim_tu_number_t)spin_pwm_cmd.tu);
        spin_pwm_cmd.adc_edge_readback = spin.pwm.getAdcEdgeTrigger((hrtim_tu_number_t)spin_pwm_cmd.tu);
        break;
    default:
        break;
    }
    return 0;
}

THINGSET_ADD_GROUP(ID_SPIN, ID_SPIN_PWM, "Pwm", &spin_pwm_cb);
THINGSET_ADD_ITEM_UINT8 (ID_SPIN_PWM, 0x7201, "wTU",        &spin_pwm_cmd.tu,        THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT8 (ID_SPIN_PWM, 0x7202, "wOutput",    &spin_pwm_cmd.output,    THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT8 (ID_SPIN_PWM, 0x7203, "wAction",    (uint8_t*)&spin_pwm_cmd.action, THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_BOOL  (ID_SPIN_PWM, 0x7204, "xExec",      &spin_pwm_cmd.exec,      THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT8 (ID_SPIN_PWM, 0x7205, "wModulation",&spin_pwm_cmd.modulation,THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT8 (ID_SPIN_PWM, 0x7206, "wSwitchConv",&spin_pwm_cmd.switch_conv,THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_FLOAT (ID_SPIN_PWM, 0x7207, "wDuty",      &spin_pwm_cmd.duty,      4, THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT16(ID_SPIN_PWM, 0x7208, "wDutyRaw",   &spin_pwm_cmd.duty_raw,  THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_INT16 (ID_SPIN_PWM, 0x7209, "wPhase_deg", &spin_pwm_cmd.phase_deg, THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT16(ID_SPIN_PWM, 0x720A, "wDeadRise",  &spin_pwm_cmd.dead_rise_ns, THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT16(ID_SPIN_PWM, 0x720B, "wDeadFall",  &spin_pwm_cmd.dead_fall_ns, THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT32(ID_SPIN_PWM, 0x720C, "wFreq_Hz",   &spin_pwm_cmd.freq_hz,   THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT32(ID_SPIN_PWM, 0x720D, "wInitFreq_Hz",&spin_pwm_cmd.init_freq, THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT32(ID_SPIN_PWM, 0x720E, "wMinFreq_Hz",&spin_pwm_cmd.min_freq,  THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT8 (ID_SPIN_PWM, 0x720F, "wMode",      &spin_pwm_cmd.pwm_mode,  THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT8 (ID_SPIN_PWM, 0x7210, "wEEV",       &spin_pwm_cmd.eev,       THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT32(ID_SPIN_PWM, 0x7211, "wAdcPS",     &spin_pwm_cmd.adc_ps,    THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT8 (ID_SPIN_PWM, 0x7212, "wAdc",       &spin_pwm_cmd.adc_target,THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_FLOAT (ID_SPIN_PWM, 0x7213, "wAdcInstant",&spin_pwm_cmd.adc_inst,  4, THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT8 (ID_SPIN_PWM, 0x7214, "wAdcEdge",   &spin_pwm_cmd.adc_edge,  THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT32(ID_SPIN_PWM, 0x7215, "wAdcDecim",  &spin_pwm_cmd.adc_decim, THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT32(ID_SPIN_PWM, 0x7216, "wRepCnt",    &spin_pwm_cmd.repetition,THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT32(ID_SPIN_PWM, 0x7217, "wBurstCmp",  &spin_pwm_cmd.burst_cmp, THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT32(ID_SPIN_PWM, 0x7218, "wBurstPer",  &spin_pwm_cmd.burst_per, THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);

THINGSET_ADD_ITEM_UINT32(ID_SPIN_PWM, 0x7219, "rPeriod",    &spin_pwm_cmd.period_ticks, THINGSET_ANY_R, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT32(ID_SPIN_PWM, 0x721A, "rPeriodMax", &spin_pwm_cmd.period_max,   THINGSET_ANY_R, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT32(ID_SPIN_PWM, 0x721B, "rPeriodMin", &spin_pwm_cmd.period_min,   THINGSET_ANY_R, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT32(ID_SPIN_PWM, 0x721C, "rPeriod_us", &spin_pwm_cmd.period_us,    THINGSET_ANY_R, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT32(ID_SPIN_PWM, 0x721D, "rFreqMin_Hz",&spin_pwm_cmd.freq_min_read, THINGSET_ANY_R, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT32(ID_SPIN_PWM, 0x721E, "rFreqMax_Hz",&spin_pwm_cmd.freq_max_read, THINGSET_ANY_R, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT32(ID_SPIN_PWM, 0x721F, "rResol_ps",  &spin_pwm_cmd.resolution_ps, THINGSET_ANY_R, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT32(ID_SPIN_PWM, 0x7220, "rRepCnt",    &spin_pwm_cmd.period_evt_rep, THINGSET_ANY_R, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT8 (ID_SPIN_PWM, 0x7221, "rMode",      &spin_pwm_cmd.mode_readback, THINGSET_ANY_R, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT8 (ID_SPIN_PWM, 0x7222, "rModulation",&spin_pwm_cmd.mod_readback,  THINGSET_ANY_R, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT8 (ID_SPIN_PWM, 0x7223, "rSwitchConv",&spin_pwm_cmd.switch_readback, THINGSET_ANY_R, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT8 (ID_SPIN_PWM, 0x7224, "rEEV",       &spin_pwm_cmd.eev_readback, THINGSET_ANY_R, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT8 (ID_SPIN_PWM, 0x7225, "rAdc",       &spin_pwm_cmd.adc_trig_readback, THINGSET_ANY_R, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT8 (ID_SPIN_PWM, 0x7226, "rAdcEdge",   &spin_pwm_cmd.adc_edge_readback, THINGSET_ANY_R, SPIN_TS_NO_SUBSET);

/* =========================================================================
 * DAC
 * ========================================================================= */

typedef enum : uint8_t {
    DAC_ACT_INIT_CONST = 0,
    DAC_ACT_SET_CONST,
    DAC_ACT_SLOPE_COMP,
    DAC_ACT_CURRENT_MODE
} spin_dac_action_t;

typedef struct {
    uint8_t  dac_number;
    uint8_t  channel;
    uint32_t const_value;
    float32_t peak_voltage;
    float32_t low_voltage;
    uint8_t  tu_src;
    spin_dac_action_t action;
    bool     exec;
} spin_dac_cmd_t;

static spin_dac_cmd_t spin_dac_cmd = {
    .dac_number  = 1,
    .channel     = 1,
    .const_value = 0,
    .peak_voltage= 0.0f,
    .low_voltage = 0.0f,
    .tu_src      = 0,
    .action      = DAC_ACT_INIT_CONST,
    .exec        = false
};

static int spin_dac_cb(enum thingset_callback_reason reason,
                       const thingset_data_object *obj)
{
    (void)obj;
    if (reason != THINGSET_CALLBACK_POST_WRITE || !spin_dac_cmd.exec) {
        return 0;
    }

    spin_dac_cmd.exec = false;

    switch (spin_dac_cmd.action) {
    case DAC_ACT_INIT_CONST:
        spin.dac.initConstValue(spin_dac_cmd.dac_number);
        break;
    case DAC_ACT_SET_CONST:
        spin.dac.setConstValue(spin_dac_cmd.dac_number,
                               spin_dac_cmd.channel,
                               spin_dac_cmd.const_value);
        break;
    case DAC_ACT_SLOPE_COMP:
        spin.dac.slopeCompensation(spin_dac_cmd.dac_number,
                                   spin_dac_cmd.peak_voltage,
                                   spin_dac_cmd.low_voltage);
        break;
    case DAC_ACT_CURRENT_MODE:
        spin.dac.currentModeInit(spin_dac_cmd.dac_number,
                                 (hrtim_tu_t)spin_dac_cmd.tu_src);
        break;
    default:
        break;
    }
    return 0;
}

THINGSET_ADD_GROUP(ID_SPIN, ID_SPIN_DAC, "Dac", &spin_dac_cb);
THINGSET_ADD_ITEM_UINT8 (ID_SPIN_DAC, 0x7301, "wDac",    &spin_dac_cmd.dac_number, THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT8 (ID_SPIN_DAC, 0x7302, "wChannel",&spin_dac_cmd.channel,    THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT32(ID_SPIN_DAC, 0x7303, "wValue",  &spin_dac_cmd.const_value,THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_FLOAT (ID_SPIN_DAC, 0x7304, "wPeak_V", &spin_dac_cmd.peak_voltage, 3, THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_FLOAT (ID_SPIN_DAC, 0x7305, "wLow_V",  &spin_dac_cmd.low_voltage,  3, THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT8 (ID_SPIN_DAC, 0x7306, "wTuSrc",  &spin_dac_cmd.tu_src,     THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT8 (ID_SPIN_DAC, 0x7307, "wAction", (uint8_t*)&spin_dac_cmd.action, THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_BOOL  (ID_SPIN_DAC, 0x7308, "xExec",   &spin_dac_cmd.exec,       THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);

/* =========================================================================
 * Comparator
 * ========================================================================= */

static uint8_t spin_comp_number = 1;
static bool spin_comp_exec = false;

static int spin_comp_cb(enum thingset_callback_reason reason,
                        const thingset_data_object *obj)
{
    (void)obj;
    if (reason != THINGSET_CALLBACK_POST_WRITE || !spin_comp_exec) {
        return 0;
    }

    spin_comp_exec = false;
    spin.comp.initialize(spin_comp_number);
    return 0;
}

THINGSET_ADD_GROUP(ID_SPIN, ID_SPIN_COMP, "Comp", &spin_comp_cb);
THINGSET_ADD_ITEM_UINT8(ID_SPIN_COMP, 0x7401, "wComp", &spin_comp_number, THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_BOOL (ID_SPIN_COMP, 0x7402, "xInit", &spin_comp_exec,   THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);

/* =========================================================================
 * Timer
 * ========================================================================= */

static uint8_t  spin_timer_number = TIMER4;
static uint32_t spin_timer_encoder = 0;
static bool     spin_timer_exec_start = false;
static bool     spin_timer_exec_read  = false;

static int spin_timer_cb(enum thingset_callback_reason reason,
                         const thingset_data_object *obj)
{
    (void)obj;
    if (reason != THINGSET_CALLBACK_POST_WRITE) {
        return 0;
    }

    if (spin_timer_exec_start) {
        spin.timer.startLogIncrementalEncoder((timernumber_t)spin_timer_number);
        spin_timer_exec_start = false;
    }
    if (spin_timer_exec_read) {
        spin_timer_encoder = spin.timer.getIncrementalEncoderValue((timernumber_t)spin_timer_number);
        spin_timer_exec_read = false;
    }
    return 0;
}

THINGSET_ADD_GROUP(ID_SPIN, ID_SPIN_TIMER, "Timer", &spin_timer_cb);
THINGSET_ADD_ITEM_UINT8 (ID_SPIN_TIMER, 0x7501, "wTimer",  &spin_timer_number,    THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_BOOL  (ID_SPIN_TIMER, 0x7502, "xStart",  &spin_timer_exec_start, THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_BOOL  (ID_SPIN_TIMER, 0x7503, "xRead",   &spin_timer_exec_read,  THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT32(ID_SPIN_TIMER, 0x7504, "rEncoder",&spin_timer_encoder,    THINGSET_ANY_R,  SPIN_TS_NO_SUBSET);

/* =========================================================================
 * UART (optional)
 * ========================================================================= */

#ifdef CONFIG_OWNTECH_UART_API
static uint8_t spin_uart_action = 0; /* 0=init,1=read,2=write,3=swap */
static uint8_t spin_uart_char = 0;
static bool    spin_uart_exec = false;

static int spin_uart_cb(enum thingset_callback_reason reason,
                        const thingset_data_object *obj)
{
    (void)obj;
    if (reason != THINGSET_CALLBACK_POST_WRITE || !spin_uart_exec) {
        return 0;
    }

    spin_uart_exec = false;

    switch (spin_uart_action) {
    case 0:
        spin.uart.usart1Init();
        break;
    case 1:
        spin_uart_char = (uint8_t)spin.uart.usart1ReadChar();
        break;
    case 2:
        spin.uart.usart1WriteChar((char)spin_uart_char);
        break;
    case 3:
        spin.uart.usart1SwapRxTx();
        break;
    default:
        break;
    }
    return 0;
}

THINGSET_ADD_GROUP(ID_SPIN, ID_SPIN_UART, "Uart", &spin_uart_cb);
THINGSET_ADD_ITEM_UINT8(ID_SPIN_UART, 0x7601, "wAction", &spin_uart_action, THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_BOOL (ID_SPIN_UART, 0x7602, "xExec",   &spin_uart_exec,   THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT8(ID_SPIN_UART, 0x7603, "wChar",   &spin_uart_char,   THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_UINT8(ID_SPIN_UART, 0x7604, "rChar",   &spin_uart_char,   THINGSET_ANY_R,  SPIN_TS_NO_SUBSET);
#endif

/* =========================================================================
 * NGND driver (optional)
 * ========================================================================= */

#ifdef CONFIG_OWNTECH_NGND_DRIVER
static bool spin_ngnd_on = false;
static bool spin_ngnd_exec = false;

static int spin_ngnd_cb(enum thingset_callback_reason reason,
                        const thingset_data_object *obj)
{
    (void)obj;
    if (reason != THINGSET_CALLBACK_POST_WRITE || !spin_ngnd_exec) {
        return 0;
    }

    spin_ngnd_exec = false;

    if (spin_ngnd_on) {
        spin.ngnd.turnOn();
    } else {
        spin.ngnd.turnOff();
    }
    return 0;
}

THINGSET_ADD_GROUP(ID_SPIN, ID_SPIN_NGND, "Ngnd", &spin_ngnd_cb);
THINGSET_ADD_ITEM_BOOL(ID_SPIN_NGND, 0x7701, "wOn",   &spin_ngnd_on,   THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
THINGSET_ADD_ITEM_BOOL(ID_SPIN_NGND, 0x7702, "xApply",&spin_ngnd_exec, THINGSET_ANY_RW, SPIN_TS_NO_SUBSET);
#endif

#endif /* SPIN_DATA_OBJECTS_H */
