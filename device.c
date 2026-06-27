#include "defs.h"

uint8_t serial_in;
uint8_t serial_seek;
uint8_t serial_last;
uint8_t flags;
uint8_t timer_counter;
uint8_t __xdata work_buffer[BUFFER_SIZE];
uint8_t __xdata serial_buffer[BUFFER_SIZE];
uint16_t bytes_left;
OPTIONS options;


#ifdef __SDCC_mcs51
void int_oper(void) __interrupt(0)
{
}


void timer0_isr(void) __interrupt(1)
{
    ++timer_counter;
	if (!(timer_counter & 0x07)) {
		PIN_MILLISEC ^= 1;
		SET_FLAG(FLAG_MILLISEC);
	}
}
#endif

void uart(void) __interrupt(4)
{
    if (RECEIVE_INTERRUPT_FLAG_SET) {
        
        RESET_RECEIVE_INTERRUPT_FLAG;
        READ_SERIAL(serial_in);
#ifndef __SDCC_mcs51
        printf("serial_in=0x%02X\n", serial_in);
#endif
        if (serial_in == SLIP_END){
            if (IS_FLAG_SET(FLAG_RECEIVING_STATE)) {
                CLEAR_FLAG(FLAG_RECEIVING_STATE);
                SET_FLAG(FLAG_PROCESSING_STATE);
                DISABLE_GLOBAL_INTERRUPTS;
            } else {
                serial_seek = 0;
                SET_FLAG(FLAG_RECEIVING_STATE);
            }
#ifndef __SDCC_mcs51
        printf("flags=0x%02X\n", flags);
#endif
        } else if (IS_FLAG_SET(FLAG_RECEIVING_STATE)) {
            switch (serial_in) {
                case SLIP_ESC:
                    serial_last = SLIP_ESC;
                    return;
                    //break;
                case SLIP_ESC_END:
                    if (serial_last == SLIP_ESC){
                        serial_in = SLIP_END;
                    }
                    break;
                case SLIP_ESC_ESC:
                    if (serial_last == SLIP_ESC){
                        serial_in = SLIP_ESC;
                    }
                break;
                default:
                if (serial_last == SLIP_ESC){
                    // ERROR - break communication                    
                    CLEAR_FLAG(FLAG_RECEIVING_STATE);
                    SET_FLAG(FLAG_PROCESSING_STATE);
                    SET_FLAG(FLAG_ERROR_STATE);
                    return;
                }
                break;
            }
            serial_last = 0;
            if (serial_seek < BUFFER_SIZE) {
                serial_buffer[serial_seek++] = serial_in;
            }
        }     
    }
}

void uart_init(unsigned char baud)
{
#ifdef __SDCC    
    TMOD |= 0x20;   // Timer 1 mode 2
    SCON = 0x50;    // 8-bit variable UART mode 1
    PCON = 0x80;    // Set SMOD bit for double baud rate
    TH1 = baud;   // Load TH1 with the desired baud rate
    TL1 = baud;  // Load TL1 with the desired baud rate
    REN = 1;  // Enable UART receiver
    ES = 1;  // Enable UART interrupt
    TR1 = 1; // Start Timer 1
#endif    
}

// 1 millisec interrupt
void timer_init(void) {
#ifdef __SDCC    
    TMOD |= 0x02; // Timer 0 mode 2
    TH0 = 26;   // Load TH0 with the desired value for 1ms
    TL0 = 26;   // Load TL0 with the desired value for 1ms
    ET0 = 1;    // Enable Timer 0 interrupt
    TR0 = 1;    // Start Timer 0
#ifdef CLOCK_OUT
    RCAP2H = 0xFF; // Load TH2 with the desired value for 11.0592MHz/8
    //0xFFFF will not work
    RCAP2L = 0xFE; // Load TL2 with the desired value for 11.0592MHz/8
    // Set to frequency 1.3824MHz on P1_0
    C_T2 = 0; // Timer 2 in timer mode
    T2MOD |= T2OE; // Enable Timer 2 output
    TR2 = 1; // Start Timer 2
#endif
#endif
}

void device_init(void) {
    uart_init(0xFF); //57600 baud
    timer_init(); 
    ENABLE_GLOBAL_INTERRUPTS;  // Enable global interrupts
}

#ifdef __SDCC_mcs51
uint8_t spi_exchange(uint8_t data) {
    uint8_t ret = 0;
    for (uint8_t kk = 0; kk <8; kk++) {
        SPI_MOSI = (data & 0x80) ? 1 : 0;
        data <<= 1;
        SPI_SCK = 1;          // rising edge: slave drives MISO, master samples here
        ret <<= 1;
        ret |= SPI_MISO;      // ← correct pin, correct timing
        SPI_SCK = 0;          // falling edge: master shifts out next MOSI bit
    }
    return ret;
}
#endif
void spi_set_ss(void) {        
    SPI_SS = 0;
}

void spi_reset_ss(void) {
    SPI_SS = 1;
}