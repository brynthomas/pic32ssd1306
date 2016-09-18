# pic32ssd1306
A PIC32 library to support SSD1306-based OLED screens over I2C/TWI

These SSD1306 OLED screens are a nice small, cheap, low power, and low pin count option for getting information out of the PIC32. They're available in plentiful supply from AliExpress, Banggood, eBay, whatever you use.

The library uses a 4x7 font to fit as much text as possible in the 128x64 pixels available, and it implements the _mon_putc function allowing it to be used directly from printf.
