/// -*- tab-width: 4; Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*-

#include <AP_HAL.h>
#include "Compass.h"
#include "AP_Compass_Backend.h"

AP_Compass_Backend::AP_Compass_Backend(AP_Compass &compass) :
    _compass(compass),
    _product_id(AP_PRODUCT_ID_NONE)
{}


// Update frontend values _sum, _count and _last_timestamp
//
void AP_InertialSensor_Backend::_update_compass(uint8_t instance, const Vector3f field, bool healthy)
{
	// Update frontend _field and _healthy
    _compass._field[instance] = field;
   	_compass._healthy[instance] = healthy;    
}
