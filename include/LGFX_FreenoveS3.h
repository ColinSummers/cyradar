#pragma once

#include <LovyanGFX.hpp>

// Freenove FNK0104B — ESP32-S3, ILI9341 320×240, FT6336U capacitive touch
class LGFX : public lgfx::LGFX_Device
{
    lgfx::Panel_ILI9341 _panel;
    lgfx::Bus_SPI _bus;
    lgfx::Light_PWM _light;
    lgfx::Touch_FT5x06 _touch;

public:
    LGFX(void)
    {
        {
            auto cfg = _bus.config();
            cfg.spi_host = SPI2_HOST;
            cfg.freq_write = 40000000;
            cfg.freq_read  = 16000000;
            cfg.pin_mosi = 11;
            cfg.pin_miso = 13;
            cfg.pin_sclk = 12;
            cfg.pin_dc   = 46;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs   = 10;
            cfg.pin_rst  = -1;
            cfg.pin_busy = -1;
            cfg.panel_width  = 240;
            cfg.panel_height = 320;
            cfg.offset_x     = 0;
            cfg.offset_y     = 0;
            cfg.offset_rotation = 0;
            cfg.invert   = true;
            cfg.rgb_order = false;
            _panel.config(cfg);
        }
        {
            auto cfg = _light.config();
            cfg.pin_bl = 45;
            cfg.invert = false;
            cfg.freq   = 44100;
            cfg.pwm_channel = 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        {
            auto cfg = _touch.config();
            cfg.i2c_port = 0;
            cfg.i2c_addr = 0x38;
            cfg.pin_sda  = 16;
            cfg.pin_scl  = 15;
            cfg.pin_int  = 17;
            cfg.pin_rst  = 18;
            cfg.freq     = 400000;
            cfg.x_min    = 0;
            cfg.x_max    = 239;
            cfg.y_min    = 0;
            cfg.y_max    = 319;
            cfg.bus_shared = false;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
        setPanel(&_panel);
    }
};
