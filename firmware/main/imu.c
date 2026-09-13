#include "imu.h"
#include "board.h"

#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static spi_device_handle_t s_dev;

// Register map from ST's lsm6ds3tr-c-pid driver.
#define REG_INT1_CTRL   0x0D
#define REG_INT2_CTRL   0x0E
#define REG_WHO_AM_I    0x0F
#define REG_CTRL1_XL    0x10
#define REG_WAKE_UP_SRC 0x1B   // followed by TAP_SRC
#define REG_OUTX_L_XL   0x28
#define REG_TAP_CFG     0x58
#define REG_TAP_THS_6D  0x59
#define REG_INT_DUR2    0x5A
#define REG_WAKE_UP_THS 0x5B
#define REG_WAKE_UP_DUR 0x5C
#define REG_MD1_CFG     0x5E
#define REG_MD2_CFG     0x5F
#define READ            0x80   // address bit 7 selects a read; multi-byte reads auto-increment

static esp_err_t read_regs(uint8_t reg, uint8_t *out, size_t n) {
    uint8_t tx[8] = { READ | reg }, rx[8] = { 0 };
    if (n > sizeof tx - 1) return ESP_ERR_INVALID_SIZE;
    spi_transaction_t t = { .length = 8 * (n + 1), .tx_buffer = tx, .rx_buffer = rx };
    esp_err_t err = spi_device_polling_transmit(s_dev, &t);
    if (err == ESP_OK) memcpy(out, rx + 1, n);
    return err;
}

static esp_err_t write_reg(uint8_t reg, uint8_t val) {
    uint8_t tx[2] = { reg, val };
    spi_transaction_t t = { .length = 16, .tx_buffer = tx };
    return spi_device_polling_transmit(s_dev, &t);
}

esp_err_t imu_begin(uint8_t *who_am_i) {
    esp_err_t err = gpio_config(&(gpio_config_t){ .pin_bit_mask = 1ULL << PIN_IMU_INT, .mode = GPIO_MODE_INPUT });
    if (err != ESP_OK) return err;
    err = spi_bus_add_device(SPI2_HOST, &(spi_device_interface_config_t){
        .spics_io_num = PIN_IMU_CS, .clock_speed_hz = 1000000, .mode = 0, .queue_size = 1 }, &s_dev);
    if (err != ESP_OK) return err;
    if ((err = read_regs(REG_WHO_AM_I, who_am_i, 1)) != ESP_OK) return err;
    return *who_am_i == IMU_WHO_AM_I_EXPECTED ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t imu_accel_mg(int *x, int *y, int *z) {
    uint8_t d[6];
    esp_err_t err = read_regs(REG_OUTX_L_XL, d, sizeof d);
    if (err != ESP_OK) return err;
    // 0.061 mg per LSB at +/-2 g.
    *x = (int16_t)(d[0] | d[1] << 8) * 61 / 1000;
    *y = (int16_t)(d[2] | d[3] << 8) * 61 / 1000;
    *z = (int16_t)(d[4] | d[5] << 8) * 61 / 1000;
    return ESP_OK;
}

esp_err_t imu_sample_mg(int *x, int *y, int *z) {
    esp_err_t err = write_reg(REG_CTRL1_XL, 0x40);   // 104 Hz, +/-2 g
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(60));                  // the first samples after power-up are not settled
    err = imu_accel_mg(x, y, z);
    write_reg(REG_CTRL1_XL, 0);
    return err;
}

// Route accelerometer data-ready to one output at a time. Data-ready stays
// high until the sample is read, so GPIO5 rises only for the wired output.
int imu_find_int_pin(void) {
    int found = 0, x, y, z;
    write_reg(REG_CTRL1_XL, 0x60);   // 416 Hz, +/-2 g
    for (int pin = 1; pin <= 2; pin++) {
        write_reg(REG_INT1_CTRL, pin == 1 ? 0x01 : 0x00);
        write_reg(REG_INT2_CTRL, pin == 2 ? 0x01 : 0x00);
        vTaskDelay(pdMS_TO_TICKS(50));
        int high = gpio_get_level(PIN_IMU_INT);
        write_reg(REG_INT1_CTRL, 0x00);
        write_reg(REG_INT2_CTRL, 0x00);
        imu_accel_mg(&x, &y, &z);
        vTaskDelay(pdMS_TO_TICKS(20));
        if (high && gpio_get_level(PIN_IMU_INT) == 0) found |= pin;
    }
    return found;
}

esp_err_t imu_arm_wake(int int_pin) {
    // Tap settings follow ST's application note for this part.
    static const uint8_t cfg[][2] = {
        { REG_CTRL1_XL,    0x60 },   // 416 Hz, +/-2 g; tap detection needs 416 Hz or more
        { REG_TAP_CFG,     0x8F },   // interrupts on, taps on X/Y/Z, latched until read
        { REG_TAP_THS_6D,  0x0C },   // tap threshold 12 x 62.5 mg = 750 mg
        { REG_INT_DUR2,    0x7F },   // up to ~540 ms between taps; quiet 29 ms; shock 58 ms
        { REG_WAKE_UP_THS, 0x83 },   // single and double tap on; motion threshold 3 x 31.25 mg
        { REG_WAKE_UP_DUR, 0x00 },
    };
    for (size_t i = 0; i < sizeof cfg / sizeof cfg[0]; i++) {
        esp_err_t err = write_reg(cfg[i][0], cfg[i][1]);
        if (err != ESP_OK) return err;
    }
    const uint8_t route = 0x28;   // wake-up (motion) and double tap
    esp_err_t err = write_reg(REG_MD1_CFG, int_pin == 1 ? route : 0);
    return err != ESP_OK ? err : write_reg(REG_MD2_CFG, int_pin == 2 ? route : 0);
}

esp_err_t imu_power_down(void) {
    static const uint8_t cfg[][2] = {
        { REG_MD1_CFG, 0 }, { REG_MD2_CFG, 0 }, { REG_INT1_CTRL, 0 }, { REG_INT2_CTRL, 0 },
        { REG_TAP_CFG, 0 }, { REG_CTRL1_XL, 0 }, { 0x11 /* CTRL2_G */, 0 },
    };
    for (size_t i = 0; i < sizeof cfg / sizeof cfg[0]; i++) {
        esp_err_t err = write_reg(cfg[i][0], cfg[i][1]);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

esp_err_t imu_read_sources(uint8_t *wake_up_src, uint8_t *tap_src) {
    uint8_t d[2];
    esp_err_t err = read_regs(REG_WAKE_UP_SRC, d, sizeof d);
    if (err != ESP_OK) return err;
    *wake_up_src = d[0];
    *tap_src = d[1];
    return ESP_OK;
}
