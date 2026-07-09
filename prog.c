#include "defs.h"

int8_t prog_packet_exchange(PACKET *p) {
    uint8_t read_data = 0;
    uint8_t pointer = 0;
    uint8_t data_len = p->packet_size - 2; /* bytes available in p->data */
    if (p->cmd & CMDFLAG_NEW_PACKET) {
        if (p->cmd & CMDFLAG_DATASIZE_256) {
            bytes_left = 256;
        } else if (p->cmd & CMDFLAG_DATASIZE){
            bytes_left = p->data[0];
        } else {
            bytes_left = 0;
        }
        if (options.options & OPTION_USE_SS) {
            spi_set_ss();
        }
        uint8_t instr_size = (p->cmd & CMDFLAG_INSTR_SIZE_MASK) + 1;
        pointer = 1; /* skip data size byte */
        for (uint8_t kk = 0; kk < instr_size; kk++) {
            spi_exchange(p->data[pointer++]);
        }
    }

    /* Data phase: remaining bytes of this packet, up to bytes_left in total.
       Continuation packets (CMDFLAG_NEW_PACKET clear) carry raw data from data[0]. */
    while (pointer < data_len && bytes_left) {
        read_data = spi_exchange(p->data[pointer]);
        if (!(p->cmd & CMDFLAG_WRITE_DIRECTION)) {
            p->data[pointer] = read_data;
        }
        pointer++;
        bytes_left--;
    }

    if (options.options & OPTION_USE_SS && p->cmd & CMDFLAG_LAST_PACKET) {
        spi_reset_ss();
    }
    return ERROR_OK;
}
