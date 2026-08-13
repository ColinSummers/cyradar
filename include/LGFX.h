#pragma once

#include <LovyanGFX.hpp>

// CYD ESP32-2432S028R — ILI9341 320×240, HSPI bus
class LGFX : public lgfx::LGFX_Device
{
    lgfx::Panel_ILI9341 _panel;
    lgfx::Bus_SPI _bus;
    lgfx::Light_PWM _light;
    lgfx::Touch_XPT2046 _touch;

public:
    LGFX(void)
    {
        {
            auto cfg = _bus.config();
            cfg.spi_host = HSPI_HOST;
            cfg.freq_write = 40000000;
            cfg.freq_read  = 16000000;
            cfg.pin_mosi = 13;
            cfg.pin_miso = 12;
            cfg.pin_sclk = 14;
            cfg.pin_dc   = 2;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs   = 15;
            cfg.pin_rst  = -1;
            cfg.pin_busy = -1;
            cfg.panel_width  = 240;
            cfg.panel_height = 320;
            cfg.offset_x     = 0;
            cfg.offset_y     = 0;
            cfg.offset_rotation = 0;
            cfg.invert   = false;
            cfg.rgb_order = false;
            _panel.config(cfg);
        }
        {
            auto cfg = _light.config();
            cfg.pin_bl = 21;
            cfg.invert = false;
            cfg.freq   = 44100;
            cfg.pwm_channel = 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        {
            auto cfg = _touch.config();
            cfg.freq = 1000000;
            cfg.x_min =   300;
            cfg.x_max =  3900;
            cfg.y_min =   200;
            cfg.y_max =  3700;
            cfg.pin_int  = 36;
            cfg.bus_shared = false;
            cfg.spi_host = VSPI_HOST;
            cfg.pin_mosi = 32;
            cfg.pin_miso = 39;
            cfg.pin_sclk = 25;
            cfg.pin_cs   = 33;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
        setPanel(&_panel);
    }
};
