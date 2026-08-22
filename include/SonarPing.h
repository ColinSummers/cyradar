#pragma once
#ifdef BOARD_FREENOVE_S3

#include <driver/i2c.h>
#include <driver/i2s.h>
#include <math.h>

namespace sonar {

static esp_err_t writeReg(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    return i2c_master_write_to_device(I2C_NUM_0, 0x18, buf, 2, pdMS_TO_TICKS(100));
}

static bool codecReady = false;

static void init() {
    pinMode(1, OUTPUT);

    // ES8311 codec registers — I2C only, no DMA allocation
    esp_err_t err = writeReg(0x00, 0x1F);
    Serial.printf("[AUDIO] ES8311 I2C: %s\n", esp_err_to_name(err));
    if (err != ESP_OK) return;

    vTaskDelay(pdMS_TO_TICKS(20));
    writeReg(0x00, 0x00);
    writeReg(0x00, 0x80);

    writeReg(0x01, 0x3F);
    writeReg(0x02, 0x48);
    writeReg(0x03, 0x10);
    writeReg(0x04, 0x10);
    writeReg(0x05, 0x00);
    writeReg(0x06, 0x03);
    writeReg(0x07, 0x00);
    writeReg(0x08, 0xFF);

    writeReg(0x09, 0x0C);
    writeReg(0x0A, 0x0C);

    writeReg(0x0D, 0x01);
    writeReg(0x0E, 0x02);
    writeReg(0x12, 0x00);
    writeReg(0x13, 0x10);

    writeReg(0x1C, 0x6A);
    writeReg(0x37, 0x08);

    writeReg(0x32, 0xFF);

    codecReady = true;
    Serial.println("[AUDIO] ES8311 codec configured");
}

static void ping() {
    if (!codecReady) return;
    Serial.printf("[AUDIO] ping (heap=%u, maxBlock=%u)\n",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());

    digitalWrite(1, HIGH);

    i2s_config_t i2s_cfg = {};
    i2s_cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    i2s_cfg.sample_rate = 16000;
    i2s_cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    i2s_cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    i2s_cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    i2s_cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    i2s_cfg.dma_buf_count = 4;
    i2s_cfg.dma_buf_len = 256;
    i2s_cfg.use_apll = false;
    i2s_cfg.tx_desc_auto_clear = true;
    i2s_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_384;

    i2s_pin_config_t pin_cfg = {};
    pin_cfg.mck_io_num = 4;
    pin_cfg.bck_io_num = 5;
    pin_cfg.ws_io_num = 7;
    pin_cfg.data_out_num = 8;
    pin_cfg.data_in_num = -1;

    if (i2s_driver_install(I2S_NUM_0, &i2s_cfg, 0, NULL) != ESP_OK ||
        i2s_set_pin(I2S_NUM_0, &pin_cfg) != ESP_OK) {
        Serial.println("[AUDIO] I2S install failed");
        return;
    }

    constexpr int SAMPLE_RATE = 16000;
    constexpr int DURATION_MS = 300;
    constexpr int SAMPLES = SAMPLE_RATE * DURATION_MS / 1000;

    int16_t buf[128];
    int idx = 0;

    for (int i = 0; i < SAMPLES; i++) {
        float t = (float)i / SAMPLE_RATE;
        float progress = (float)i / SAMPLES;
        float amplitude = expf(-progress * 4.0f);
        int16_t sample = (int16_t)(sinf(2.0f * M_PI * 800.0f * t) * 30000.0f * amplitude);
        buf[idx++] = sample;
        buf[idx++] = sample;
        if (idx >= 128) {
            size_t written;
            i2s_write(I2S_NUM_0, buf, idx * sizeof(int16_t), &written, portMAX_DELAY);
            idx = 0;
        }
    }
    while (idx < 128) buf[idx++] = 0;
    size_t written;
    i2s_write(I2S_NUM_0, buf, idx * sizeof(int16_t), &written, portMAX_DELAY);

    vTaskDelay(pdMS_TO_TICKS(50));
    i2s_driver_uninstall(I2S_NUM_0);
    digitalWrite(1, LOW);
    Serial.println("[AUDIO] ping done, I2S freed");
}

} // namespace sonar
#endif
