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

static void slip_send_byte(uint8_t data) {
    if (data == SLIP_END) {
        SERIAL_SEND_BYTE(SLIP_ESC);
        SERIAL_SEND_BYTE(SLIP_ESC_END);
    } else if (data == SLIP_ESC) {
        SERIAL_SEND_BYTE(SLIP_ESC);
        SERIAL_SEND_BYTE(SLIP_ESC_ESC);
    } else {
        SERIAL_SEND_BYTE(data);
    }
}

/* Response packet, SLIP framed: [packet_size, packet_checksum, status, data...]
   The request's data area (with read bytes filled in by prog_packet_exchange)
   is echoed back only for a successful non-options packet without
   CMDFLAG_WRITE_DIRECTION; otherwise the response carries the status alone. */
void send_response(int8_t status) {
    PACKET *p = (PACKET*) work_buffer;
    uint8_t data_size = 0;
    uint8_t checksum = (uint8_t)status;
    if (status == ERROR_OK && !(p->cmd & CMDFLAG_OPTIONS)
            && !(p->cmd & CMDFLAG_WRITE_DIRECTION)) {
        data_size = p->packet_size - 2;
    }
    for (uint8_t kk = 0; kk < data_size; kk++) {
        checksum += p->data[kk];
    }
    SERIAL_SEND_BYTE(SLIP_END);
    slip_send_byte(data_size + 2);
    slip_send_byte((uint8_t)(0 - checksum));
    slip_send_byte((uint8_t)status);
    for (uint8_t kk = 0; kk < data_size; kk++) {
        slip_send_byte(p->data[kk]);
    }
    SERIAL_SEND_BYTE(SLIP_END);
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
    checksum += p->packet_checksum;
    checksum += p->cmd;
    if (checksum) {
        return ERROR_PACKET_CHECKSUM;
    }
    if (p->cmd & CMDFLAG_OPTIONS) {
        return on_options_packet(p);
    }
    return prog_packet_exchange(p);
}