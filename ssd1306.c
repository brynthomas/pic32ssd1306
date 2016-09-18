#include "ssd1306.h"
#define _DISABLE_OPENADC10_CONFIGPORT_WARNING
#define _SUPPRESS_PLIB_WARNING

#include <stdint.h>
#include <plib.h>

// This lot of defines is present to allow me to specify a single instance of
// "SSD1306_I2C_CHANNEL" and then dynamically generate all the combinations
// of BRG/CON/IRQ etc that are needed.

#define DEF_MERGER_INDIRECTOR(x,y,z) x ## y ## z
#define DEF_MERGER(x,y,z) DEF_MERGER_INDIRECTOR(x,y,z)
#define CHAN_FUNC(suffix) DEF_MERGER(SSD1306_I2C_CHANNEL, suffix, )
#define CHAN_FUNC_PREFIXED(prefix, suffix) DEF_MERGER(prefix, SSD1306_I2C_CHANNEL, suffix)

// A set of definitions defining the font and screen information
// It's only been tested with this one particular font at this one font size
// It definitely won't work with fonts that could vertically straddle more than
// two pages (i.e. height > 16), nor will it work with fonts that are not 4 or
// possible 8 pixels wide. It's not been built with flexible fonts in mind.

#define FONT_WIDTH 4
#define FONT_HEIGHT 7
#define FONT_FIRST_CHAR 32
#define FONT_LAST_CHAR 126
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCREEN_PAGES 8
#define TEXT_ROWS 9
#define TEXT_COLUMNS 32

// The array that stores the full screen data. Is this really necessary?
// Could it be done to work directly off the memory in the SSD1306 and avoid
// a local buffer? Yes, but for expedience and simplicity it's done like this.
// Not particularly RAM constrained on the PIC32 either.

// The screen is set to "Horizontal Addressing Mode" in this mode
// it is accessed in 8 pixel vertical pages, then 128 pixel columns,
// and then those 128 byte columns are organised into 8 rows of 8 pixels.
// Top to bottom (1 byte), then left to right (128 bytes),
// then more top to bottom. (1024 bytes)

static uint8_t screen_data[1024];

// This font is the converted version of the Tom Thumb font available at
// http://robey.lag.net/2010/01/23/tiny-monospace-font.html

// Font data goes from LSB to MSB in four vertical columns of 8-bits.
// Since the font is only 7 pixels high the 8th bit is ignored.
// 4 columns * 8-bit fits each symbol into a single 32-bit value.

static const uint32_t const font_data[] = {0x0, 0x1700, 0x30003, 0x1f0a1f, 0x51f0a, 0x120409, 0x1c170f, 0x300, 0x110e00, 0xe11, 0x50205, 0x40e04, 0x810, 0x40404, 0x1000, 0x30418, 0xf111e, 0x1f02, 0x121519, 0xa1511, 0x1f0407, 0x91517, 0x1d151e, 0x30519, 0x1f151f, 0xf1517, 0xa00, 0xa10, 0x110a04, 0xa0a0a, 0x40a11, 0x31501, 0x16150e, 0x1e051e, 0xa151f, 0x11110e, 0xe111f, 0x15151f, 0x5051f, 0x1d150e, 0x1f041f, 0x111f11, 0xf1008, 0x1b041f, 0x10101f, 0x1f061f, 0x1f0e1f, 0xe110e, 0x2051f, 0x1e190e, 0x160d1f, 0x91512, 0x11f01, 0x1f100f, 0x71807, 0x1f0c1f, 0x1b041b, 0x31c03, 0x131519, 0x11111f, 0x80402, 0x1f1111, 0x20102, 0x101010, 0x201, 0x1c161a, 0xc121f, 0x12120c, 0x1f120c, 0x161a0c, 0x51e04, 0x1e2a0c, 0x1c021f, 0x1d00, 0x1d2010, 0x120c1f, 0x101f11, 0x1e0e1e, 0x1c021e, 0xc120c, 0xc123e, 0x3e120c, 0x2021c, 0xa1e14, 0x121f02, 0x1e100e, 0xe180e, 0x1e1c1e, 0x120c12, 0x1e2806, 0x161e1a, 0x111b04, 0x1b00, 0x41b11, 0x10302};

// Tracks the state of the I2C transmission for the interrupt handler.

static int i2c_irq_phase;
static int i2c_data_count;

// Controls the position of the virtual cursor.

static int cursor_x_pos = 0;
static int cursor_y_pos = 0;

// Used to signal the state of transmission.

static uint8_t i2c_refresh_queued = 0;
static uint8_t i2c_in_transfer = 0;

// Begins a refresh of the on-chip pixel data to the SSD1306
// If a transfer is already in progress, another one gets queued up.
// If a transfer is not happening, it directly starts up the I2C system
// knowing that the interrupt handler will see it through to completion.

static void queue_refresh(void){
    i2c_refresh_queued = 1;
    if (i2c_in_transfer == 0){
        INTClearFlag(INT_SOURCE_I2C_MASTER(CHAN_FUNC()));
        i2c_in_transfer = 1;
        i2c_irq_phase = 0;
        CHAN_FUNC(CONbits).SEN = 1;        
    }
}

// Scrolls the data in the virtual buffer up by one line.

static void scroll_up(){
    uint8_t page;
    uint16_t tempdata;
    uint8_t column;
    
    for (column = 0 ; column < SCREEN_WIDTH; column++){
        for (page = 0; page < SCREEN_PAGES; page++){
            tempdata = (screen_data[SCREEN_WIDTH * page + column]);
            if (page < (SCREEN_PAGES - 1)) tempdata += (screen_data[SCREEN_WIDTH * (page + 1) + column]) << 8;
            tempdata >>= FONT_HEIGHT;
            screen_data[SCREEN_WIDTH * page + column] = tempdata & 0xFF;
        }
    }
    queue_refresh();
}

// Writes a single character to the virtual buffer at the current cursor
// position and advances the cursor.

void output_char(char a){
    uint16_t first_row = (cursor_y_pos * 7) >> 3;
    uint8_t bit_in = (cursor_y_pos * 7) & 7;
    uint16_t temp_merged_lines;
    uint32_t pixel_data;
    uint8_t pixel_offset;
    
    // Check for CR/LF and instead of writing anything just use it to move
    // the cursor.
    
    if (a == 10) {
        cursor_x_pos = 0; 
        return;
    }
    if (a == 13) {
        cursor_x_pos = 0;
        cursor_y_pos++;
        if (cursor_y_pos >= TEXT_ROWS) {
            cursor_y_pos = TEXT_ROWS - 1;
            scroll_up();
        } 
        return;
    }
    
    if ((a < FONT_FIRST_CHAR) | (a > FONT_LAST_CHAR)) {
        return;
    }
    
    // Load in the font data for this particular character

    pixel_data = font_data[a - FONT_FIRST_CHAR];
    
    // Step through each column of font data...
    
    for (pixel_offset = 0; pixel_offset < FONT_WIDTH; pixel_offset++) {
        // ... read what is currently in the virtual buffer at the cursor ...
        temp_merged_lines = screen_data[SCREEN_WIDTH * first_row + cursor_x_pos * FONT_WIDTH + pixel_offset];
        if (first_row < SCREEN_HEIGHT)
            temp_merged_lines += screen_data[SCREEN_WIDTH * (first_row+1) + cursor_x_pos * FONT_WIDTH + pixel_offset] << 8;
        
        // ... clear out any pixels in the spot where the new symbol needs to go ...
        temp_merged_lines &= ~((uint16_t)(((0b1 << FONT_HEIGHT) - 1) << bit_in));
        
        // ... and put a single column of pixels from our font in its place ...
        temp_merged_lines |= ((uint16_t)(pixel_data & 0xFF)) << (bit_in);
        
        // ... then put that combined data back into the virtual buffer ...
        screen_data[SCREEN_WIDTH * first_row + cursor_x_pos * FONT_WIDTH + pixel_offset] = (uint8_t)temp_merged_lines;
        if (first_row < SCREEN_HEIGHT) 
            screen_data[SCREEN_WIDTH * (first_row+1) + cursor_x_pos * FONT_WIDTH + pixel_offset] = (uint8_t) (temp_merged_lines >> 8);
        
        // ... and advance the font pixel data one column to the right.
        pixel_data >>= 8;
    }
    cursor_x_pos++;
    
    // Do any wrapping if we've reached the end of a line or the end of the
    // screen.
    if (cursor_x_pos >= TEXT_COLUMNS) {
        cursor_x_pos = 0;
        cursor_y_pos++;
    };
    if (cursor_y_pos >= TEXT_ROWS) {
        cursor_y_pos = TEXT_ROWS - 1;
        scroll_up();
    }
    queue_refresh();
}

// This overrides the default printf writer. Lets you use printf and
// have the contents show up on the screen.

void _mon_putc(char a){
    output_char(a);
}

// What if you don't want to use printf and all the tons of library baggage
// it and all its friends bring along? Then you can use this much simpler
// screen printing routine.

void output_str(char* str){
    int len;
    int count;
    
    len = strlen(str);
    for (count = 0; count < len; count++)
        output_char(str[count]);
}

// Clears the virtual buffer and updates the screen.

void clear_screen(void){
    memset(screen_data, 0, (SCREEN_WIDTH * SCREEN_HEIGHT) >> 3);
    cursor_x_pos = 0;
    cursor_y_pos = 0;
    queue_refresh();
}

// Moves the virtual cursor.

void goto_xy(uint8_t x, uint8_t y){
    cursor_x_pos = x;
    cursor_y_pos = y;
    if (cursor_x_pos >= TEXT_COLUMNS)
        cursor_x_pos = 0;
    if (cursor_y_pos >= TEXT_ROWS)
        cursor_y_pos = TEXT_ROWS - 1;
}

// Sets a pixel at a particular location on the screen.
// 0 = black, 1 = white for colour.

void set_pixel(uint8_t x, uint8_t y, uint8_t colour){
    uint8_t bit_offset = y & 7;
    uint8_t row_offset = y >> 3;
    uint8_t column_offset = x;
    if (x >= SCREEN_WIDTH)
        return;
    if (y >= SCREEN_HEIGHT)
        return;
    
    if (colour == 0){
        screen_data[SCREEN_WIDTH * row_offset + column_offset] &= ~(1 << bit_offset); 
    } else {
        screen_data[SCREEN_WIDTH * row_offset + column_offset] |= (1 << bit_offset); 
    }
    
    queue_refresh();
}

// Our interrupt handler to drive updating of the screen

void __ISR(CHAN_FUNC_PREFIXED(_, _VECTOR), IPL1SOFT) i2c_irq_handler(void){
    INTClearFlag(INT_SOURCE_I2C_MASTER(CHAN_FUNC()));
    
    // Every I2C transaction follows the pattern of
    // SEN -> Address -> Command/Data Selection -> Command/Data* -> PEN
    // We assign a number for each of those and track which state we're in.
    
    switch (i2c_irq_phase) {
      // SSD1306 Data
        // Blast out the 1024 bytes of screen data

        case 0: CHAN_FUNC(TRN) = SSD1306_I2C_ADDR << 1; i2c_irq_phase++; break;
        case 1: CHAN_FUNC(TRN) = 0b01000000; i2c_data_count = 0; i2c_irq_phase++; i2c_refresh_queued = 0; break;
        case 2: CHAN_FUNC(TRN) = screen_data[i2c_data_count]; i2c_data_count++; if (i2c_data_count >= ((SCREEN_WIDTH * SCREEN_HEIGHT) >> 3)) i2c_irq_phase++; break;
        case 3: CHAN_FUNC(CONbits).PEN = 1; i2c_irq_phase++; break;
        
        // If we have another transfer queued, begin that.
        // Otherwise we're done with I2C for now. 
        case 4: if (i2c_refresh_queued) {i2c_irq_phase = 0; CHAN_FUNC(CONbits).SEN = 1; } else {i2c_irq_phase++; i2c_in_transfer = 0; }; break;
    }
}

static void i2c_send_command(uint8_t data){
    // Every I2C transaction follows the pattern of
    // SEN -> Address -> Command/Data Selection -> (Command/Data)* -> PEN    
    CHAN_FUNC(CONbits).SEN = 1;
    while (CHAN_FUNC(CONbits).SEN);
    // Send the address
    CHAN_FUNC(TRN) = SSD1306_I2C_ADDR << 1;
    while (CHAN_FUNC(STATbits).TRSTAT);
    // Tell it a command is coming
    CHAN_FUNC(TRN) = 0b00000000;
    while (CHAN_FUNC(STATbits).TRSTAT);
    // Send the command
    CHAN_FUNC(TRN) = data;
    while (CHAN_FUNC(STATbits).TRSTAT);
    // End the transaction
    CHAN_FUNC(CONbits).PEN = 1;
    while (CHAN_FUNC(CONbits).PEN);
}

void ssd1306_initialize(void){
    
    // Setup the interrupts and interrupt handlers
    INTSetVectorPriority(INT_VECTOR_I2C(CHAN_FUNC()), INT_PRIORITY_LEVEL_1);
    INTSetVectorSubPriority(INT_VECTOR_I2C(CHAN_FUNC()), INT_SUB_PRIORITY_LEVEL_3);
    INTEnable(INT_SOURCE_I2C_MASTER(CHAN_FUNC()), INT_DISABLED);
    
    // Set the I2C baud rate
    CHAN_FUNC(BRG) = SSD1306_I2C_BAUD_RATE_DIVIDER;
    
    // Enable the I2C bus
    CHAN_FUNC(CONbits).ON = 1;
    
    i2c_irq_phase = 0;
    
    // This is the initialisation code needed for the SSD1306. Some of these
    // can vary from module to module depending on the physical setup!
    
    // This is pretty much a direct copy of the flowchart found in the
    // SSD1306 datasheet near the end titled:
    // "Software initialization flow chart"
    
    // Set MUX Ratio
    i2c_send_command(0xA8);
    i2c_send_command(0x3F);

    // Set Display Offset
    i2c_send_command(0xD3);
    i2c_send_command(0x00);
    
    // Set Display Start Line
    i2c_send_command(0x40);
    
    // Set Segment re-map (if things are reversed you can try change this to 0xA0)
    i2c_send_command(0xA1);
    
    // Set COM Output Scan Direction
    // (if things are reversed you can try change this to 0xC0)
    i2c_send_command(0xC8);
    
    // Set COM Pins hardware configuration
    // (if rows are missing or duplicated you can try 0xDA/0x02)
    i2c_send_command(0xDA);
    i2c_send_command(0x12);
    
    // Set Contrast Control
    i2c_send_command(0x81);
    i2c_send_command(0x7F);
    
    // Disable Entire Display On
    // (if you just are trying to get ANYTHING to show on the screen
    // it can help when testing to set 0xA5 to get the whole screen white)
    i2c_send_command(0xA4);
    
    // Set Normal Display (instead of reversed)
    i2c_send_command(0xA6);

    // Set Osc Frequency
    i2c_send_command(0xD5);
    i2c_send_command(0x80);
    
    // Enable charge pump regulator
    i2c_send_command(0x8D);
    i2c_send_command(0x14);
    
    // Display On
    i2c_send_command(0xAF);
    
    // Disable auto scrolling
    i2c_send_command(0x2E);

    // Set horizontal addressing mode
    i2c_send_command(0x20);
    i2c_send_command(0x00);

    // Set the starting and ending column for horizontal addressing mode
    i2c_send_command(0x21);
    i2c_send_command(0);
    i2c_send_command(127);
    
    // Set the page start and end for horizontal addressing
    i2c_send_command(0x22);
    i2c_send_command(0);
    i2c_send_command(7);
    
    // Enable the interrupt handler
    INTClearFlag(INT_SOURCE_I2C_MASTER(CHAN_FUNC()));
    INTEnable(INT_SOURCE_I2C_MASTER(CHAN_FUNC()), INT_ENABLED);

    queue_refresh();    
}