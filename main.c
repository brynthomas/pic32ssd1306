#define _DISABLE_OPENADC10_CONFIGPORT_WARNING
#define _SUPPRESS_PLIB_WARNING
#define GetSystemClock() (40000000ul)

#include <p32xxxx.h>

#include <plib.h>
#include <stdint.h>
#include <stdlib.h>
#include "ssd1306.h"

#pragma config   JTAGEN    = OFF    // JTAG Enable OFF
#pragma config   FNOSC     = FRCPLL // Fast RC w PLL 8mHz internal rc Osc
#pragma config   FPLLIDIV  = DIV_2  // PLL in 8mHz/2 = 4mHz
#pragma config   FPLLMUL   = MUL_20 // PLL mul 4mHz * 20 = 80mHz 24??
#pragma config   FPLLODIV  = DIV_2  // PLL Out 8mHz/2= 40 mHz system frequency osc
#pragma config   FPBDIV    = DIV_2  // Peripheral Bus Divisor
#pragma config   FCKSM     = CSECME // Clock Switch Enable, FSCM Enabled
#pragma config   POSCMOD   = OFF    // Primary osc disabled
#pragma config   IESO      = OFF    // Internal/external switch over
#pragma config   OSCIOFNC  = OFF    // CLKO Output Signal Active on the OSCO Pin
#pragma config   FWDTEN    = OFF    // Watchdog Timer Enable:
#pragma config   FSOSCEN   = OFF

int counter;

int main (void)
{
    TRISB = 0b100000000;
    ANSELB = 0;
    ANSELA = 0;
    mOSCSetPBDIV(OSC_PB_DIV_1);	
    INTConfigureSystem(INT_SYSTEM_CONFIG_MULT_VECTOR);
    INTEnableSystemMultiVectoredInt();
    SYSTEMConfigPerformance(GetSystemClock());
    INTEnableInterrupts();
    // Used on my board to power the OLED
    LATBbits.LATB5 = 1;    

    ssd1306_initialize();
   
    for (counter = 0; counter < 10000;counter++){
        printf("%i\n", counter);
    }

    clear_screen();

    output_str("the quick brown fox jumps over the lazy dog\n");
    output_str("THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG\n");

    while (1);
}