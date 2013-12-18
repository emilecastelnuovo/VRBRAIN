
#ifndef __AP_HAL_VRBRAIN_RCOUTPUT_H__
#define __AP_HAL_VRBRAIN_RCOUTPUT_H__

#include <AP_HAL_VRBRAIN.h>
#include <AP_HAL.h>
#include <timer.h>

#define VRBRAIN_MAX_OUTPUT_CHANNELS 12

class VRBRAIN::VRBRAINRCOutput : public AP_HAL::RCOutput {
    void     init(void* implspecific);
    void     set_freq(uint32_t chmask, uint16_t freq_hz);
    uint16_t get_freq(uint8_t ch);
    void     enable_ch(uint8_t ch);
    void     enable_mask(uint32_t chmask);
    void     disable_ch(uint8_t ch);
    void     disable_mask(uint32_t chmask);
    void     write(uint8_t ch, uint16_t period_us);
    void     write(uint8_t ch, uint16_t* period_us, uint8_t len);
    uint16_t read(uint8_t ch);
    void     read(uint16_t* period_us, uint8_t len);
private:
    uint32_t _timer_period(uint16_t speed_hz);
    uint8_t _num_motors;

    uint8_t out_ch1;
    uint8_t out_ch2;
    uint8_t out_ch3;
    uint8_t out_ch4;
    uint8_t out_ch5;
    uint8_t out_ch6;
    uint8_t out_ch7;
    uint8_t out_ch8;
    uint8_t out_ch9;
    uint8_t out_ch10;
    uint8_t out_ch11;
    uint8_t out_ch12;
    uint16_t output_channel_raw[VRBRAIN_MAX_OUTPUT_CHANNELS];

};

#endif // __AP_HAL_VRBRAIN_RCOUTPUT_H__
