/*
 *  AP_Notify Library. 
 * based upon a prototype library by David "Buzz" Bussenschutt.
 */

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

#ifndef __VRBRAIN_LED_H__
#define __VRBRAIN_LED_H__

#include "RGBLed.h"
#include "AP_BoardLED.h"


class VRBRAIN_LED: public RGBLed {
public:
    VRBRAIN_LED();

    bool hw_init(void);
    bool hw_set_rgb(uint8_t r, uint8_t g, uint8_t b);
};

#endif
