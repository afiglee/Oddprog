#ifndef __DEFS_H__
#define __DEFS_H__

#ifdef __SDCC_mcs51
#ifdef STC89
#include <mcs51/stc89.h>
#else
#include <mcs51/at89x52.h>
#endif
#endif
#include <stdint.h>
#include <string.h>
#ifndef __SDCC_mcs51
#include <stdio.h>
#endif

#ifdef __SDCC_mcs51
#define __xdata __xdata
#define __code __code
#define __at(A) __at(A)
#define __data __data
#define __interrupt(A) __interrupt(A)
#else
#define __xdata
#define __code
#define __at(A)
#define __data
#define __interrupt(A)
#endif


#define ERROR_OK                0x00
#define ERROR_PACKET_CORRUPTED  0xC0
#define ERROR_PACKET_CHECKSUM   0xCE
#define ERROR_PACKET_ESC_ERROR  0xEC /*SLIP_ESC is not following by SLIP_ESC_END or SLIP_ESC_ESC*/

/* SLIP PROTOCOL */
#define SLIP_END     0xC0
#define SLIP_ESC     0xDB
#define SLIP_ESC_END 0xDC
#define SLIP_ESC_ESC 0xDD


#define FLAG_MILLISEC           0x01
#define FLAG_RECEIVING_STATE    0x02
#define FLAG_PROCESSING_STATE   0x04
#define FLAG_ERROR_STATE        0x08  // Error ERROR_PACKET_ESC_ERROR detected in interrupt handling

#define READ_SERIAL(A) A = SBUF
#define WRITE_SERIAL(A) SBUF = A

/* ++ 8051 specific ++ */
#ifdef __SDCC_mcs51
#define SPI_MOSI    P1_5
#define SPI_MISO    P1_6
#define SPI_SCK     P1_7
#define SPI_SS      P1_4

#define PIN_MILLISEC 	P1_3
#define PIN_SERIAL_TICK    P1_2
#define PIN_OPER          P1_4
#define PIN_INT0         P3_2
#define PIN_FREQ         P1_5

#define RECEIVE_INTERRUPT_FLAG_SET RI == 1
#define RESET_RECEIVE_INTERRUPT_FLAG RI = 0
#define TRANSMIT_INTERRUPT_FLAG_SET TI == 1
#define RESET_TRANSMIT_INTERRUPT_FLAG TI = 0
#define ENABLE_GLOBAL_INTERRUPTS EA = 1
#define DISABLE_GLOBAL_INTERRUPTS EA = 0
/* -- 8051 specific -- */
#else
/* Test environment*/
extern uint8_t interrupt_flags;
extern uint8_t SBUF; 
extern uint8_t SPI_SS;
#define RECEIVE_INTERRUPT_FLAG_SET (interrupt_flags & 1)
#define SET_RECEIVE_INTERRUPT_FLAG (interrupt_flags |= 1)
#define RESET_RECEIVE_INTERRUPT_FLAG (interrupt_flags &= 0xFE)
#define ENABLE_GLOBAL_INTERRUPTS
#define DISABLE_GLOBAL_INTERRUPTS
#endif

#define SET_FLAG(A) flags |= A;
#define CLEAR_FLAG(A) flags &= ~A;
#define IS_FLAG_SET(A) (flags & A)

#define BUFFER_SIZE 0x40

#define CMDFLAG_NEW_PACKET          0x80
#define CMDFLAG_LAST_PACKET         0x40
#define CMDFLAG_WRITE_DIRECTION     0x20 /*means data part after instruction is to be written, otherwise read*/
#define CMDFLAG_DATASIZE_256        0x10 //Means data size is 256 bytes after instruction
#define CMDFLAG_DATASIZE            0x08 //Means next byte after cmd is size of the data unless CMDFLAG_DATASIZE_256 is set then size of the data is 256 byte
#define CMDFLAG_OPTIONS             0x04 // not actual programming packet, options packet
#define CMDFLAG_INSTR_SIZE_MASK     0x03 //00 - 1 byte, 01 - 2bytes, 10 - 3 bytes, 11 - 4 bytes

typedef struct s_PACKET {
    uint8_t packet_size;
    uint8_t packet_checksum; // sum (cmd + loop(data[packet_size - 1])) == 0
    uint8_t cmd;             // contains cmd flag
    uint8_t data[];         // First byte is always a size of data packet, 0 in case of 256 bytes (have CMDFLAG_DATASIZE_256 set)
} PACKET;

#define OPTION_USE_SS           01
#define OPTION_USE_POLARITY     02

typedef struct s_OPTIONS {
    uint8_t options;
} OPTIONS;

extern uint8_t serial_in;
extern uint8_t serial_seek;
extern uint8_t serial_last;
extern uint8_t flags;
extern uint8_t timer_counter;
extern __xdata uint8_t work_buffer[BUFFER_SIZE];
extern __xdata uint8_t serial_buffer[BUFFER_SIZE];
extern uint16_t bytes_left;
extern OPTIONS options;

#ifdef __cplusplus
extern "C" {
#endif
void device_init(void);
int8_t on_packet_received(uint8_t);
uint8_t spi_exchange(uint8_t data);
void spi_set_ss(void);
void spi_reset_ss(void);
int8_t prog_packet_exchange(PACKET *p);
#ifdef __cplusplus
}
#endif


#endif // __DEFS_H__