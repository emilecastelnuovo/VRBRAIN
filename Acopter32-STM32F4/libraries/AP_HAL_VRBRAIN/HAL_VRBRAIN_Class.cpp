
#include <AP_HAL.h>
#if CONFIG_HAL_BOARD == HAL_BOARD_VRBRAIN

#include <AP_HAL_VRBRAIN.h>
#include "AP_HAL_VRBRAIN_Namespace.h"
#include "HAL_VRBRAIN_Class.h"
#include "AP_HAL_VRBRAIN_Private.h"
#include "Util.h"
#include <AP_HAL_Empty.h>
#include <AP_HAL_Empty_Private.h>
#include <pwm_in.h>
#include <usart.h>
#include <i2c.h>
#include <AP_Compass.h>

using namespace VRBRAIN;

//_USART1 PIN 3 AND 4 OF THE INPUT RAIL
//_USART2 INTERNAL SERIAL PORT
//_USART3 PIN 1 AND 2 OF THE INPUT RAIL
//_USART6 PIN 5 AND 6 on the INPUT RAIL


// XXX make sure these are assigned correctly
static VRBRAINUARTDriver uartADriver(_USART1,1);
static VRBRAINUARTDriver uartBDriver(_USART2,0);
static VRBRAINUARTDriver uartCDriver(_USART3,0);
static VRBRAINUARTDriver uartDDriver(_USART6,0);
static VRBRAINSemaphore  i2cSemaphore;
static VRBRAINSemaphore  i2c2Semaphore;
static VRBRAINI2CDriver  i2cDriver(_I2C2,&i2cSemaphore); //internal I2c
static VRBRAINI2CDriver  i2c2Driver(_I2C1,&i2c2Semaphore); //external I2C
static VRBRAINSPIDeviceManager spiDeviceManager;
static VRBRAINAnalogIn analogIn;
static VRBRAINStorage storageDriver(_I2C2,&i2cSemaphore);
static VRBRAINGPIO gpioDriver;
static VRBRAINRCInput rcinDriver;
static VRBRAINRCOutput rcoutDriver;
static VRBRAINScheduler schedulerInstance;
static VRBRAINUtil utilInstance;

uint8_t g_ext_mag_detect;

HAL_VRBRAIN::HAL_VRBRAIN() :
    AP_HAL::HAL(
      &uartADriver,
      &uartBDriver,
      &uartCDriver,
      &uartDDriver,
	  NULL,
      &i2cDriver,
      &i2c2Driver,
      &spiDeviceManager,
      &analogIn,
      &storageDriver,
      &uartADriver,
      &gpioDriver,
      &rcinDriver,
      &rcoutDriver,
      &schedulerInstance,
      &utilInstance
	  )
{}

extern const AP_HAL::HAL& hal;

/*Returns true if an external mag on I2C2 port has been detected*/
static void detect_compass(void)
    {

    hal.scheduler->delay(1000);

    AP_HAL::Semaphore* _i2c_sem;
    uint8_t _base_config;
    //Try external.

    _i2c_sem = hal.i2c2->get_semaphore();
    if (!_i2c_sem->take(HAL_SEMAPHORE_BLOCK_FOREVER))
	{
	hal.console->println("Failed to get HMC5843 semaphore");
	}

    // determine if we are using 5843 or 5883L
    _base_config = 0;
    if (hal.i2c2->writeRegister((uint8_t) 0x3C, 0x00,
	    0x03 << 5 | 0x06 << 2 | 0x10)
	    || hal.i2c2->readRegister((uint8_t) 0x3C, 0x00, &_base_config))
	{
	g_ext_mag_detect = 0;
	_i2c_sem->give();
	}
    if (_base_config != 0)
	{
	g_ext_mag_detect = 1;
	hal.console->println("Compass TYPE EXT");
	_i2c_sem->give();
	return;
	}


    _i2c_sem = hal.i2c->get_semaphore();
    if (!_i2c_sem->take(HAL_SEMAPHORE_BLOCK_FOREVER))
	{
	hal.console->println("Failed to get HMC5843 semaphore");
	}

    // determine if we are using 5843 or 5883L
    _base_config = 0;
    if (hal.i2c->writeRegister((uint8_t) 0x3C, 0x00,
	    0x03 << 5 | 0x06 << 2 | 0x10)
	    || hal.i2c->readRegister((uint8_t) 0x3C, 0x00, &_base_config))
	{
	g_ext_mag_detect = 0;
	_i2c_sem->give();
	return;
	}
    if (_base_config != 0)
	{
	g_ext_mag_detect = 0;
	hal.console->println("Compass TYPE INT");
	_i2c_sem->give();
	return;
	}

    //No compass found
    g_ext_mag_detect = 3;
}

void HAL_VRBRAIN::init(int argc,char* const argv[]) const
{
  /* initialize all drivers and private members here.
   * up to the programmer to do this in the correct order.
   * Scheduler should likely come first. */
  //delay_us(2000000);
  scheduler->init(NULL);

  uartA->begin(57600);
  //uartC->begin(57600);

  //uartC->set_blocking_writes(true);

  g_ext_mag_detect = 0;

  //_member->init();
  i2c->begin();
  i2c2->begin();

  spi->init(NULL);

  detect_compass();

  analogin->init(NULL);
  storage->init(NULL);
  rcin->init(NULL);
  rcout->init(NULL);

}

const HAL_VRBRAIN AP_HAL_VRBRAIN;

#endif
