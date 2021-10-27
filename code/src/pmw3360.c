// derived from https://github.com/mrjohnk/PMW3360DM-T2QU

#include "hardware/gpio.h"
#include "hardware/spi.h"

#include "srom.h"

#define SPI_PORT spi0

#define PIN_MISO 4
#define PIN_CS   9
#define PIN_SCK  2
#define PIN_MOSI 3

// Registers
#define Product_ID  0x00
#define Revision_ID 0x01
#define Motion  0x02
#define Delta_X_L 0x03
#define Delta_X_H 0x04
#define Delta_Y_L 0x05
#define Delta_Y_H 0x06
#define SQUAL 0x07
#define Raw_Data_Sum  0x08
#define Maximum_Raw_data  0x09
#define Minimum_Raw_data  0x0A
#define Shutter_Lower 0x0B
#define Shutter_Upper 0x0C
#define Control 0x0D
#define Config1 0x0F
#define Config2 0x10
#define Angle_Tune  0x11
#define Frame_Capture 0x12
#define SROM_Enable 0x13
#define Run_Downshift 0x14
#define Rest1_Rate_Lower  0x15
#define Rest1_Rate_Upper  0x16
#define Rest1_Downshift 0x17
#define Rest2_Rate_Lower  0x18
#define Rest2_Rate_Upper  0x19
#define Rest2_Downshift 0x1A
#define Rest3_Rate_Lower  0x1B
#define Rest3_Rate_Upper  0x1C
#define Observation 0x24
#define Data_Out_Lower  0x25
#define Data_Out_Upper  0x26
#define Raw_Data_Dump 0x29
#define SROM_ID 0x2A
#define Min_SQ_Run  0x2B
#define Raw_Data_Threshold  0x2C
#define Config5 0x2F
#define Power_Up_Reset  0x3A
#define Shutdown  0x3B
#define Inverse_Product_ID  0x3F
#define LiftCutoff_Tune3  0x41
#define Angle_Snap  0x42
#define LiftCutoff_Tune1  0x4A
#define Motion_Burst  0x50
#define LiftCutoff_Tune_Timeout 0x58
#define LiftCutoff_Tune_Min_Length  0x5A
#define SROM_Load_Burst 0x62
#define Lift_Config 0x63
#define Raw_Data_Burst  0x64
#define LiftCutoff_Tune2  0x65

void cs_select() {
    asm volatile ("nop \n nop \n nop");
    gpio_put(PIN_CS, 0);        // Active low
    asm volatile ("nop \n nop \n nop");
}

void cs_deselect() {
    asm volatile ("nop \n nop \n nop");
    gpio_put(PIN_CS, 1);
    asm volatile ("nop \n nop \n nop");
}

uint8_t read_register(uint8_t reg_addr) {
    cs_select();

    // send adress of the register, with MSBit = 0 to indicate it's a read
    uint8_t x = reg_addr & 0x7f;
    spi_write_blocking(SPI_PORT, &x, 1);
    sleep_us(100);              // tSRAD
    // read data
    uint8_t data;
    spi_read_blocking(SPI_PORT, 0, &data, 1);

    sleep_us(1);                // tSCLK-NCS for read operation is 120ns
    cs_deselect();
    sleep_us(19);               // tSRW/tSRR (=20us) minus tSCLK-NCS

    return data;
}

void write_register(uint8_t reg_addr, uint8_t data) {
    cs_select();

    // send adress of the register, with MSBit = 1 to indicate it's a write
    uint8_t x = reg_addr | 0x80;
    spi_write_blocking(SPI_PORT, &x, 1);
    // send data
    spi_write_blocking(SPI_PORT, &data, 1);

    sleep_us(20);               // tSCLK-NCS for write operation
    cs_deselect();
    sleep_us(100);              // tSWW/tSWR (=120us) minus tSCLK-NCS. Could be shortened, but is looks like a safe lower bound 
}

void upload_firmware() {
    // send the firmware to the chip, cf p.18 of the datasheet

    // Write 0 to Rest_En bit of Config2 register to disable Rest mode.
    write_register(Config2, 0x20);

    // write 0x1d in SROM_enable reg for initializing
    write_register(SROM_Enable, 0x1d);

    // wait for more than one frame period
    sleep_ms(10);               // assume that the frame rate is as low as 100fps... even if it should never be that low

    // write 0x18 to SROM_enable to start SROM download
    write_register(SROM_Enable, 0x18);

    // write the SROM file (=firmware data) 
    cs_select();
    uint8_t data = SROM_Load_Burst | 0x80; // write burst destination adress
    spi_write_blocking(SPI_PORT, &data, 1);
    sleep_us(15);

    // send all bytes of the firmware
    for (int i = 0; i < firmware_length; i++) {
        spi_write_blocking(SPI_PORT, &(firmware_data[i]), 1);
        sleep_us(15);
    }

    // Read the SROM_ID register to verify the ID before any other register reads or writes.
    read_register(SROM_ID);

    // Write 0x00 to Config2 register for wired mouse or 0x20 for wireless mouse design.
    write_register(Config2, 0x00);

    // set initial CPI resolution
    write_register(Config1, 0x15);

    cs_deselect();
}

void perform_startup(void) {
    cs_deselect();              // ensure that the serial port is reset
    cs_select();                // ensure that the serial port is reset
    cs_deselect();              // ensure that the serial port is reset
    write_register(Power_Up_Reset, 0x5a);       // force reset
    sleep_ms(50);               // wait for it to reboot
    // read registers 0x02 to 0x06 (and discard the data)
    read_register(Motion);
    read_register(Delta_X_L);
    read_register(Delta_X_H);
    read_register(Delta_Y_L);
    read_register(Delta_Y_H);
    // upload the firmware
    upload_firmware();
    sleep_ms(10);
}

void pmw3360_set_cpi(unsigned int cpi) {
    int cpival = (cpi / 100) - 1;
    cs_select();
    write_register(Config1, cpival);
    cs_deselect();
}

void pmw3360_get_deltas(int16_t * dx, int16_t * dy) {
    // write 0x01 to Motion register and read from it to freeze the motion values and make them available
    write_register(Motion, 0x01);
    read_register(Motion);

    *dx = (int16_t) (read_register(Delta_X_H) << 8 | read_register(Delta_X_L));
    *dy = (int16_t) (read_register(Delta_Y_H) << 8 | read_register(Delta_Y_L));
}

void pmw3360_init() {
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);

    spi_init(SPI_PORT, 500000);
    spi_set_format(SPI_PORT, 8, 1, 1, SPI_MSB_FIRST);

    perform_startup();
}
