#include "defs.h"

__code const char ident[] = "https://github.com/afiglee/OddProg";
__code const char speed[] = "57600 baud";
__code const char * const header = "OddProg v1.0 "__DATE__" "__TIME__"\n";
#if 0
void print(const char *str)
{
    while (*str) {
        while (!TI);
        TI = 0;
        SBUF = *str++;     
    }
}
#endif

int main(void)
{

    device_init();
  
    while (1) {
		if (IS_FLAG_SET(FLAG_PROCESSING_STATE)) {
            if (IS_FLAG_SET(FLAG_ERROR_STATE)) {
                //TODO replay with error
                ENABLE_GLOBAL_INTERRUPTS;
		    } else {
                uint8_t data_size = serial_seek;
                for (uint8_t kk = 0; kk < data_size; kk++){
                    work_buffer[kk] = serial_buffer[kk];
                }
                ENABLE_GLOBAL_INTERRUPTS;
                on_packet_received(data_size);
            }
            CLEAR_FLAG(FLAG_PROCESSING_STATE);
        } 
        if (flags & FLAG_MILLISEC) {
            flags &= ~FLAG_MILLISEC;
            // Toggle pin
        }
    }
}
