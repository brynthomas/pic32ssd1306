
#ifndef _SSD1306_H 
#define _SSD1306_H

#include <stdint.h>

// The channel being used for I2C

#define SSD1306_I2C_CHANNEL I2C1

// The I2C address of the OLED screen
// If your OLED screen says something like 0x78 it's probably actually 0x3C
// If it says 0x7A it's probably 0x3D
// Those addresses have already been shifted left by one, we need the
// unshifted value.

#define SSD1306_I2C_ADDR 0x3C

// This is the speed the I2C bus runs it and is a function of the PBCLK
// Check Section 24: Inter-Integrated Circuit of the PIC32 Family Reference
// Manual, and look under the Enabling I2C Operation section for a table
// that has different example Baud Rate Divisions for a selection of PBCLKs
// and the formula to calculate your own.

// 0x0C2 is for 40MHz PBCLK to run the I2C bus at 100KHz
// 0x02C is for 40MHz PBCLK to run the I2C bus at 400KHz

#define SSD1306_I2C_BAUD_RATE_DIVIDER 0x02C

// Overrides the default printf handler to use the screen instead

void _mon_putc(char a);

// Outputs a single character and advances the cursor

void output_char(char a);

// A simple string output function

void output_str(char* str);

// Clears the virtual buffer and updates the screen

void clear_screen(void);

// Moves the virtual cursor

void goto_xy(uint8_t x, uint8_t y);

// Sets a pixel at a particular location on the screen
// 0 = black, 1 = white for colour.

void set_pixel(uint8_t x, uint8_t y, uint8_t colour);

// Required to setup the screen and interrupt handling

void ssd1306_initialize(void);

#endif
