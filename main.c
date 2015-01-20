/*
 * Serial High BootLoader for PIC16F1783 Buck Click
 *
 * File:   main.c
 * Author: Lucio Di Jasio
 *
 * Compiler: XC8, v.1.33b
 *
 * Created on November 21, 2014
 */
#include "mcc_generated_files/mcc.h"
#include "Flash.h"

#include <string.h>

// program memory organization for PIC16F1783
#define BOOT_START    0x0E00       // row aligned high start of bootloader
#define APP_START     BOOT_START-2 // ljmp to application 

inline void bootLoad( void) @BOOT_START
{ // ensure a jump to bootloader init is placed at BOOT_START
#asm
        PAGESEL     (start_initialization)
        goto        (start_initialization)&0x7ff
#endasm
}

inline void runApp( void)
{ // run the application
#asm
                PAGESEL     APP_START
                goto        APP_START&0x7FF
#endasm

}
/**************************************************************************
Protocol Description.

    USB protocol is a typical master-slave communication protocol, where
    master (PC) sends commands and slave (bootloader equipped device) executes
    them and acknowledges execution.

    * Command format.

    <STX[0]><CMD_CODE[0]><ADDRESS[0..3]><COUNT[0..1]> <DATA[0..COUNT-1]>
    |-- 1 --|---- 1 -----|------ 4 -----|----- 2 ----|------ COUNT -----|

    STX      - Command start delimiter (for future upgrades).
               Length: 1 byte. Mandatory.
    CMD_CODE - Command index (TCmd).
               Length: 1 byte. Mandatory.
    ADDRESS  - Address field. Flash start address for
               CMD_CODE command operation.
               Length: 4 bytes. Optional (command specific).
    COUNT    - Count field. Amount of data/blocks for
               CMD_CODE command operation.
               Length: 2 bytes. Optional (command specific).
    DATA     - Data array.
               Length: COUNT bytes. Optional (command specific).

    Some commands do not utilize all of these fields.
    See 'Command Table' below for details on specific command's format.

    * Command Table.
     --------------------------+---------------------------------------------------
    |       Description        |                      Format                       |
    | Synchronize with PC tool |                  <STX><cmdSYNC>                   |
    | Send bootloader info     |                  <STX><cmdINFO>                   |
    | Go to bootloader mode    |                  <STX><cmdBOOT>                   |
    | Restart MCU              |                  <STX><cmdREBOOT>                 |
    | Write to MCU flash       | <STX><cmdWRITE><START_ADDR><DATA_LEN><DATA_ARRAY> |
    | Erase MCU flash.         |  <STX><cmdERASE><START_ADDR><ERASE_BLOCK_COUNT>   |
     ------------------------------------------------------------------------------

     * Acknowledge format.

    <STX[0]><CMD_CODE[0]>
    |-- 1 --|---- 1 -----|

    STX      - Response start delimiter (for future upgrades).
               Length: 1 byte. Mandatory.
    CMD_CODE - Index of command (TCmd) we want to acknowledge.
               Length: 1 byte. Mandatory.

    See 'Acknowledgement Table' below for details on specific command's
    acknowledgement process.

    * Acknowledgement Table.
     --------------------------+---------------------------------------------------
    |       Description        |                   Acknowledgement                 |
    |--------------------------+---------------------------------------------------|
    | Synchronize with PC tool |                  upon reception                   |
    | Send bootloader info     |          no acknowledge, just send info           |
    | Go to bootloader mode    |                  upon reception                   |
    | Restart MCU              |                  no acknowledge                   |
    | Write to MCU flash       | upon each write of internal buffer data to flash  |
    | Erase MCU flash.         |                  upon execution                   |

*******************************************************************************/

#define STX             '['//0x0F
#define cmdSYNC         'S'//1
#define cmdINFO         'I'//2
#define cmdBOOT         'B'//3
#define cmdREBOOT       'R'//4
#define cmdWRITE        'W'//11
#define cmdERASE        'E'//21

// Supported MCU families/types.
//enum { PC16 = 1, PIC18 = 2, PIC18FJ = 3, PIC24 = 4,  dsPIC = 10, PIC32' = 20;)  dMcuType ;
#define mcuPIC16    1

uint16_t data[FLASH_ROWSIZE];       // data buffer

#define putch   EUSART_Write
#define getch   EUSART_Read

/**
 * Send a word (lsb first)
 * @param w
 */
void putw( uint16_t w)
{
    putch( w);          // lsb
    putch( w>>8);       // msb
}

/**
 *  Receive a word (lsb First)
 *  @return unsigned 16-bit value
 */
uint16_t getw( void)
{
    union  {
        uint8_t     byte[2];
        uint16_t    word;
    } r;

    r.byte[0] = getch();
    r.byte[1] = getch();
    return  r.word;
} // getw


/**
 *  Send the info block describing the device and bootloader address
 */
void info( void)
{
    putch( 23+20);                            // 1, info block size
    putch( 1);    putw( mcuPIC16);            // 3, mcuType
    putch( 8);    putw( FLASH_SIZE); putw(0); // 5, total amount of flash available
//    putch( 2);    putw( 0x1783);              // mcuID unused
    putch( 3);    putw( FLASH_ROWSIZE );      // 3, erase page size
    putch( 4);    putw( FLASH_ROWSIZE );      // 3, write row size
    putch( 5);    putw( 0x0100);              // 3, bootloader revision 0.1
    putch( 6);    putw( BOOT_START); putw(0); // 5, bootloader start address
    putch( 7);                                // 21, 20-byte padded text
    putch( 'B'); putch( 'u'); putch( 'c'); putch( 'k');
    putch( 'C'); putch( 'l');putch( 'i'); putch( 'c'); putch( 'k'); putch( '\0');
    putch( '\0'); putch( '\0'); putch( '\0'); putch( '\0'); putch( '\0');
    putch( '\0'); putch( '\0'); putch( '\0'); putch( '\0'); putch( '\0');
} // info

/**
 * Send an acknowledge
 * @param r     command to be acknowledged
 */
void ack( uint8_t r)
{
    putch( STX);
    putch( r);
} // ack


/**
 * Receive a block of data (words)
 * @param pcount    pointer to counter (words)
 * @param pdata     array of words (16-bit unsigned)
 */
void get_data( uint16_t* pcount, uint16_t* pdata)
{
    uint16_t count = getw();       // get the word count
    *pcount = count;

    while ( count-- > 0)  // read each word
    {
        *pdata++ = getw();
    }
} // get_data


/**
 * Write a block of data to flash
 * @param add       address (16-bit unsigned)
 * @param count     number of words
 * @param data      arrray of words
 */
void write( uint16_t add, uint16_t count, uint16_t* data)
{
    // write latches
    while( count-- > 1)
    {
        FLASH_write( add++, *data++, 1);  // latch
    }
    // write last word and entire row
    FLASH_write( add, *data++, 0);          // write
}

void main(void)
{
    uint16_t count;
    uint16_t add;

    SYSTEM_Initialize();
    while( !TMR0_HasOverflowOccured());     // wait for 1ms

    // check CS if not active (high) -> run the app
    if ( P_CS_GetValue())  
    {
        P_LED_SetLow();
        runApp();
    }

    // if CS is active (low) -> boot
    while( 1)
    {
        // wait for a start command
        while ( STX != getch()){
        };
        P_LED_Toggle();
        // receive the command and dispatch
        switch( getch()){
            case cmdSYNC:           // synchronize
                ack( cmdSYNC);      // acknowledge immediately
                break;
            case cmdBOOT:           // stay in bootloader mode
                ack( cmdBOOT);
                break;
            case cmdINFO:           // return info record
                info();
                break;
            case cmdREBOOT:         // run application
                runApp();
                break;
            case cmdERASE:          // erase block
                add = getw();       // get address (word)
                getch(); getch();   // discard two high bytes
                FLASH_erase( add);
                ack( cmdERASE);
                break;
            case cmdWRITE:          // write block
                add = getw();       // get address (word)
                getch(); getch();   // discard two high bytes
                get_data( &count, &data);
//                add = 0x20;
//                for( count=0; count<32; count++)
//                    data[count]=count;
                write( add, count, data);
                ack( cmdWRITE);
                break;
            default:
                bootLoad();         // restart bootloader (avoid/keep from optimizer)
                break;
        } // swtich
    } // main loop
} // main


