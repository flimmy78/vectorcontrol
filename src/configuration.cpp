/*
Copyright (C) 2014-2015 Thiemar Pty Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <cstdlib>
#include <cstring>

#include "configuration.h"
#include "hal.h"


volatile float *flash_param_values = (volatile float*)FLASH_PARAM_ADDRESS;


__attribute__((section(".app_descriptor"),used))
volatile struct bootloader_app_descriptor
flash_app_descriptor = {
    .signature = 0x3030637365445041L,
    .image_crc = 0L,
    .image_size = 0L,
    .vcs_commit = 0L,
    .major_version = 0u,
    .minor_version = 1u,
    .reserved = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu}
};


struct param_t flash_params[NUM_PARAMS] = {
    /*
    Index, type, name,
        default value, min value, max value
    */

    /*
    Number of motor poles. Used to convert mechanical speeds to electrical
    speeds.
    */
    {PARAM_MOTOR_NUM_POLES, PARAM_TYPE_INT, "motor_num_poles",
        14.0f, 2.0f, 40.0f},

    /*
    Motor current limit in amps. This determines the maximum current
    controller setpoint, as well as the maximum allowable current setpoint
    slew rate.
    */
    {PARAM_MOTOR_CURRENT_LIMIT, PARAM_TYPE_FLOAT, "motor_current_limit",
        2.0f, 1.0f, 40.0f},

    /*
    Motor voltage limit in volts. The current controller's commanded voltage
    will never exceed this value.

    Note that this may safely be above the nominal voltage of the motor; to
    determine the actual motor voltage limit, divide the motor's rated maximum
    power by the motor current limit.
    */
    {PARAM_MOTOR_VOLTAGE_LIMIT, PARAM_TYPE_FLOAT, "motor_voltage_limit",
        7.4f, 0.5f, 27.0f},

    /*
    Motor maximum rated RPM. This limits the upper end of the PWM setpoint
    range if it's lower than KV multiplied by Vbus.
    */
    {PARAM_MOTOR_RPM_MAX, PARAM_TYPE_INT, "motor_rpm_max",
        20000.0f, 500.0f, 40000.0f},

    /* Motor resistance in ohms. This is estimated on start-up. */
    {PARAM_MOTOR_RS, PARAM_TYPE_FLOAT, "motor_rs",
        60e-3f, 1e-3f, 1000e-3f},

    /* Motor inductance in henries. This is estimated on start-up. */
    {PARAM_MOTOR_LS, PARAM_TYPE_FLOAT, "motor_ls",
        20e-6f, 1e-6f, 1000e-6f},

    /*
    Motor KV in RPM per volt. This can be taken from the motor's spec sheet;
    accuracy will help control performance but a 20% error is fine.
    */
    {PARAM_MOTOR_KV, PARAM_TYPE_FLOAT, "motor_kv",
        850.0f, 100.0f, 5000.0f},

    /*
    Acceleration torque limit in amps. Determines the maximum difference
    between the torque setpoint and the load torque, and therefore the amount
    of torque available for acceleration.

    This is a critical factor in smooth start-up into high-inertia systems. If
    start-up is rough, lower this parameter and/or control_accel_gain. If
    controller response is too slow, increase this parameter and/or
    control_accel_gain.
    */
    {PARAM_CONTROL_ACCEL_TORQUE_MAX, PARAM_TYPE_FLOAT, "control_accel_torque_max",
        2.0f, 0.1f, 40.0f},

    /*
    Load torque in amps. This is a target value for torque at full throttle,
    rather than a limit, and in conjunction with control_accel_time it
    determines the torque output from the speed controller.
    */
    {PARAM_CONTROL_LOAD_TORQUE, PARAM_TYPE_FLOAT, "control_load_torque",
        10.0f, 1.0f, 40.0f},

    /*
    Speed controller acceleration gain. A gain of 0.0 results in no torque
    output proportional to the required acceleration, whiel a gain of 1.0
    results in a full-scale acceleration torque output for an error of
    100 rad/s electrical.
    */
    {PARAM_CONTROL_ACCEL_GAIN, PARAM_TYPE_FLOAT, "control_accel_gain",
        0.1f, 0.0f, 1.0f},

    /*
    Rise time of the speed controller's torque output; this determines the
    target time to accelerate from near zero to full throttle, subject to
    the overall current limits and load inertia.
    */
    {PARAM_CONTROL_ACCEL_TIME, PARAM_TYPE_FLOAT, "control_accel_time",
        0.1f, 0.01f, 1.0f},

    /*
    Interval in microseconds at which custom ESC status messages should be
    sent. Zero disables publication.
    */
    {PARAM_CUSTOM_ESCSTATUS_INTERVAL, PARAM_TYPE_INT,
        "uavcan.pubp-thiemar.equipment.esc.Status",
        20e3, 0, 1e6f},

    /* Data type ID of the custom ESC status message. */
    {PARAM_CUSTOM_ESCSTATUS_ID, PARAM_TYPE_INT,
        "uavcan.dtid-thiemar.equipment.esc.Status",
        768, 1, 1023},

    /*
    Interval in microseconds at which UAVCAN standard ESC status messages
    should be sent. Zero disables publication.
    */
    {PARAM_UAVCAN_ESCSTATUS_INTERVAL, PARAM_TYPE_INT,
        "uavcan.pubp-uavcan.equipment.esc.Status",
        1e5f, 0, 1e6f},

    /* Index of this ESC in throttle command messages. */
    {PARAM_UAVCAN_ESC_INDEX, PARAM_TYPE_INT,
        "uavcan.id-uavcan.equipment.esc-esc_index",
        0.0f, 0.0f, 15.0f},
};


inline static float _rad_per_s_from_rpm(float rpm, uint32_t num_poles) {
    return rpm / 60.0f * (2.0f * (float)M_PI * (float)(num_poles >> 1u));
}


Configuration::Configuration(void) {
    size_t i;
    for (i = 0; i < NUM_PARAMS; i++) {
        params_[i] = flash_param_values[i];
    }
}


void Configuration::read_motor_params(struct motor_params_t& params) {
    params.num_poles = (uint32_t)params_[PARAM_MOTOR_NUM_POLES];

    params.max_current_a = params_[PARAM_MOTOR_CURRENT_LIMIT];
    params.max_voltage_v = params_[PARAM_MOTOR_VOLTAGE_LIMIT];
    params.max_speed_rad_per_s =
        _rad_per_s_from_rpm(params_[PARAM_MOTOR_RPM_MAX], params.num_poles);

    params.rs_r = params_[PARAM_MOTOR_RS];
    params.ls_h = params_[PARAM_MOTOR_LS];
    params.phi_v_s_per_rad = 1.0f /
        _rad_per_s_from_rpm(params_[PARAM_MOTOR_KV], params.num_poles);
}


void Configuration::read_control_params(
    struct control_params_t& params
) {
    params.bandwidth_hz = 50.0f;
    params.max_accel_torque_a = params_[PARAM_CONTROL_ACCEL_TORQUE_MAX];
    params.load_torque_a = params_[PARAM_CONTROL_LOAD_TORQUE];
    params.accel_gain = params_[PARAM_CONTROL_ACCEL_GAIN];
    params.accel_time_s = params_[PARAM_CONTROL_ACCEL_TIME];
}


static size_t _get_param_name_len(const char* name) {
    size_t i;

    for (i = 0; i < 27; i++) {
        if (name[i] == 0) {
            return i + 1u;
        }
    }

    return 0;
}


static size_t _find_param_index_by_name(
    const char* name,
    struct param_t params[],
    size_t num_params
) {
    size_t i, name_len;

    name_len = _get_param_name_len(name);
    if (name_len <= 1) {
        return num_params;
    }

    for (i = 0; i < num_params; i++) {
        if (memcmp(params[i].name, name, name_len) == 0) {
            return i;
        }
    }

    return num_params;
}


bool Configuration::get_param_by_name(
    struct param_t& out_param,
    const char* name
) {
    size_t idx;

    idx = _find_param_index_by_name(name, flash_params, NUM_PARAMS);
    return get_param_by_index(out_param, (uint8_t)idx);
}


bool Configuration::get_param_by_index(
    struct param_t& out_param,
    uint8_t index
) {
    if (index >= NUM_PARAMS) {
        return false;
    }

    memcpy(&out_param, &flash_params[index], sizeof(struct param_t));
    return true;
}


bool Configuration::set_param_value_by_name(const char* name, float value) {
    size_t idx;

    idx = _find_param_index_by_name(name, flash_params, NUM_PARAMS);
    return set_param_value_by_index((uint8_t)idx, value);
}


bool Configuration::set_param_value_by_index(uint8_t index, float value) {
    if (index >= NUM_PARAMS) {
        return false;
    }

    if (flash_params[index].min_value <= value &&
            value <= flash_params[index].max_value) {
        params_[index] = value;
        return true;
    } else {
        return false;
    }
}


void Configuration::write_params(void) {
    static_assert(!(sizeof(params_) & 0x3u),
                  "Size of params_ must be a multiple of 4");

    hal_flash_protect(false);
    hal_flash_erase((uint8_t*)flash_param_values, 2048u);
    hal_flash_write((uint8_t*)flash_param_values, sizeof(params_),
                    (uint8_t*)params_);
    hal_flash_protect(true);
}
