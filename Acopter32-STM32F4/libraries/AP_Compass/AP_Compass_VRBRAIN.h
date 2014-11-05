/// -*- tab-width: 4; Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*-
#ifndef AP_Compass_VRBRAIN_H
#define AP_Compass_VRBRAIN_H

#include <AP_HAL.h>
#include "../AP_Common/AP_Common.h"
#include "../AP_Math/AP_Math.h"

#include "Compass.h"

class AP_Compass_VRBRAIN : public Compass
{
private:
    float               calibration[3];
    bool                _initialised;
    virtual bool        read_raw(void);
    uint8_t             _base_config;
    virtual bool        re_initialise(void);
    bool                read_register(uint8_t address, uint8_t *value);
    bool                write_register(uint8_t address, uint8_t value);
    bool		_init(void);
    uint32_t            _retry_time; // when unhealthy the millis() value to retry at
    AP_HAL::Semaphore*  _i2c_sem;
    AP_HAL::I2CDriver* 	_i2c;

    int16_t		_mag_x;
    int16_t		_mag_y;
    int16_t		_mag_z;
    int16_t             _mag_x_accum;
    int16_t             _mag_y_accum;
    int16_t             _mag_z_accum;
    uint8_t		_accum_count;
    uint32_t            _last_accum_time;

public:
    AP_Compass_VRBRAIN() : Compass() {
    _initialised=0;
    _base_config=0;
    _retry_time=0;
    _i2c_sem=0;
    _i2c = 0;
    _mag_x=0;
    _mag_y=0;
    _mag_z=0;
    _mag_x_accum=0;
    _mag_y_accum=0;
    _mag_z_accum=0;
    _accum_count=0;
    _last_accum_time=0;
    }

    bool        init(void);
    bool        read(void);
    void        accumulate(void);

};
#endif
