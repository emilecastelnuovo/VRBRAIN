// -*- tab-width: 4; Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*-
/*
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
  Compass driver backend class. Each supported compass sensor type
  needs to have an object derived from this class.
 */
#ifndef __AP_COMPASS_BACKEND_H__
#define __AP_COMPASS_BACKEND_H__

class AP_Compass_Backend
{
public:
    AP_Compass_Backend(Compass &compass);

    // we declare a virtual destructor so that drivers can
    // override with a custom destructor if need be.
    virtual ~AP_Compass_Backend(void) {}

    // initialize the magnetometers
    virtual bool init(void);

    // read sensor data
    virtual bool read(void);

    // accumulate a reading from the magnetometer
    virtual void accumulate(void);

    void _update_compass(uint8_t instance, const Vector3f field, bool healthy);

protected:
    virtual bool        read_raw(void);
    virtual bool        re_initialise(void);
    virtual bool        read_register(uint8_t address, uint8_t *value);
    virtual bool        write_register(uint8_t address, uint8_t value);
    
    Compass             &_compass; ///< access to frontend

    float               calibration[3];
    bool                _initialised;
    uint8_t             _base_config;
    uint32_t            _retry_time; // when unhealthy the millis() value to retry at
    AP_HAL::Semaphore*  _i2c_sem;

    int16_t             _mag_x;
    int16_t             _mag_y;
    int16_t             _mag_z;
    int16_t             _mag_x_accum;
    int16_t             _mag_y_accum;
    int16_t             _mag_z_accum;
    uint8_t             _accum_count;
    uint32_t            _last_accum_time;

    bool                _healthy;
    Vector3f            _field;     ///< magnetic field strength

};

#endif // __AP_COMPASS_BACKEND_H__
