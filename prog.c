#include "defs.h"

int8_t prog_packet_exchange(PACKET *p) {
    uint8_t read_data = 0;
    if (p->cmd & CMDFLAG_NEW_PACKET) {
        if (p->cmd & CMDFLAG_DATASIZE_256) {
            bytes_left = 256;
        } else if (p->cmd & CMDFLAG_DATASIZE){
            bytes_left = p->data[0];
        }
        if (options.options & OPTION_USE_SS) {
            spi_set_ss();
        }
        uint8_t instr_size = (p->cmd & CMDFLAG_INSTR_SIZE_MASK) + 1;        
        for (uint8_t kk = 0; kk < instr_size; kk++) {
            read_data = spi_exchange(p->data[1+kk]);
        }
    }

    if (options.options & OPTION_USE_SS && p->cmd & CMDFLAG_LAST_PACKET) {
        spi_reset_ss();
    }
    return ERROR_OK;
}