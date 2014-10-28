
#include <string.h>
#include "Storage.h"
#include <AP_HAL.h>
#include <i2c.h>

extern const AP_HAL::HAL& hal;

using namespace VRBRAIN;
__IO uint32_t  timeout = I2C_TIMEOUT;

#define countof(a) (sizeof(a) / sizeof(*(a)))

void VRBRAINStorage::init(void*)
{
}

void VRBRAINStorage::read_block(void* dst, uint16_t src, size_t n) {

	uint8_t * buff = (uint8_t*)dst;
	uint16_t numbytes = (uint16_t)n;


	if (!_sem->take(1)) {
	   return;
	}


	uint32_t ret = sEE_ReadBuffer(_dev, buff, src, &numbytes);

	_sem->give();

	if(ret == 1){
	    hal.gpio->write(20, 1);
	    i2c_bus_reset(_dev);
	    return;
	}

	uint32_t time =  ((uint32_t)(10 * 0x1000));
	while(numbytes > 0){
	    if ((time--) == 0)
		{
		hal.gpio->write(20, 1);
		return;
		}
	}

}

void VRBRAINStorage::write_block(uint16_t dst,const void* src, size_t n)
{
	uint8_t * buff = (uint8_t *)src;


	if (!_sem->take(1)) {
	   return;
	}


	uint32_t ret = sEE_WriteBuffer(_dev, buff,dst,(uint16_t)n);

	_sem->give();

	if(ret == 1){
	    hal.gpio->write(20, 1);
	    i2c_bus_reset(_dev);
	    return;
	}
}



