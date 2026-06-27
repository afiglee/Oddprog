#include "defs.h"

int8_t on_options_packet(PACKET *p) {
    if (p->data[1] & OPTION_USE_SS) {
        options.options |= OPTION_USE_SS;
    } else {
        options.options &= ~OPTION_USE_SS;
    }
    //TODO: Implement polarity
    if (p->data[1] & OPTION_USE_POLARITY) {
        options.options |= OPTION_USE_POLARITY;
    } else {
        options.options &= ~OPTION_USE_POLARITY;
    }
    return 0;
}

int8_t on_packet_received(uint8_t data_size) {

    int8_t ret = 0;
    uint8_t checksum = 0;
    PACKET *p = (PACKET*) work_buffer;
    
    // Verify integrity
    if (data_size < sizeof(PACKET)) {
        return ERROR_PACKET_CORRUPTED;
    }
    if (p->packet_size > (BUFFER_SIZE - 1) || p->packet_size < 2) {
        return ERROR_PACKET_CORRUPTED;
    }
    for (uint8_t kk = 0; kk < p->packet_size - 2; kk++) {
        checksum += p->data[kk];
    }
    checksum += p->cmd;
    if (checksum) {
        return ERROR_PACKET_CHECKSUM;
    }
    if (p->cmd & CMDFLAG_OPTIONS) {
        return on_options_packet(p);
    }
    return ERROR_OK;
}