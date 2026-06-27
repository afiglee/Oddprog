#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <iostream>
#include <vector>
#include <map>
#include <array>
#include <optional>

#include "testdefs.h"

extern "C" {
    void uart(void);
}

uint8_t interrupt_flags;
uint8_t SBUF;
uint8_t SPI_SS;

extern std::vector<uint8_t> vcReceived;
extern std::vector<uint8_t> vcSentOut;
 
void test_env_reset() {
    flags = 0;
    options.options = 0;
    SPI_SS = 1;
    vcReceived.clear();
    vcSentOut.clear();
}

TEST(SlipProtocolStub, SlipPacketReceiveValid)
{
    uint8_t packet[] = {0xFE, 0xC0, 0x0A, 0xFF, 0xEE, 0xC0};
    test_env_reset();
    serial_seek = 8;
    for (uint8_t kk = 0; kk < sizeof(packet); kk++) {
        SBUF = packet[kk];
        SET_RECEIVE_INTERRUPT_FLAG;
        uart();
        if (kk == 0) {
            EXPECT_EQ(flags, 0);
        } else if (kk != (sizeof(packet) - 1)) {
            printf("kk=%d SBUF=0x%02X\n", kk, SBUF);
            EXPECT_EQ(flags, FLAG_RECEIVING_STATE);
        }
    }
    EXPECT_EQ(flags, FLAG_PROCESSING_STATE);
    EXPECT_EQ(serial_buffer[0], packet[2]);
    EXPECT_EQ(serial_buffer[1], packet[3]);
    EXPECT_EQ(serial_buffer[2], packet[4]);
    EXPECT_EQ(serial_seek, 3);
}

TEST(SlipProtocolStub, SlipBrokenPacketReceive)
{
    uint8_t packet[] = {0xFE, 0xC0, SLIP_ESC, 0xFF, 0xEE};
    test_env_reset();
    serial_seek = 8;
    for (uint8_t kk = 0; kk < sizeof(packet); kk++) {
        SBUF = packet[kk];
        SET_RECEIVE_INTERRUPT_FLAG;
        uart();
    }
    EXPECT_EQ(flags, FLAG_PROCESSING_STATE | FLAG_ERROR_STATE);
}

TEST(SlipProtocolStub, SlipPacketReceivedWithEscape)
{
    uint8_t packet[] = {0xFE, 0xC0, SLIP_ESC, SLIP_ESC_ESC, 0x0A, SLIP_ESC, SLIP_ESC_END, 0xFF, 0xEE, 0xC0};
    test_env_reset();
    serial_seek = 8;
    for (uint8_t kk = 0; kk < sizeof(packet); kk++) {
        SBUF = packet[kk];
        SET_RECEIVE_INTERRUPT_FLAG;
        uart();
        if (kk == 0) {
            EXPECT_EQ(flags, 0);
        } else if (kk != (sizeof(packet) - 1)) {
            printf("kk=%d SBUF=0x%02X\n", kk, SBUF);
            EXPECT_EQ(flags, FLAG_RECEIVING_STATE);
        }
    }
    EXPECT_EQ(flags, FLAG_PROCESSING_STATE);
    EXPECT_EQ(serial_buffer[0], SLIP_ESC);
    EXPECT_EQ(serial_buffer[1], packet[4]);
    EXPECT_EQ(serial_buffer[2], SLIP_END);
    EXPECT_EQ(serial_buffer[3], packet[7]);
    EXPECT_EQ(serial_buffer[4], packet[8]);
    EXPECT_EQ(serial_seek, 5);
}

// TODO:
// Test on timeout
// Test on packet too large
