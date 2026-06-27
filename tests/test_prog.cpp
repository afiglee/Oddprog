#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <iostream>
#include <vector>
#include <map>
#include <array>
#include <optional>

#include "testdefs.h"

/*
typedef struct s_PACKET {
    uint8_t packet_size;
    uint8_t packet_checksum; // sum (cmd + loop(data[packet_size - 1])) == 0
    uint8_t cmd;             // contains cmd flag
    uint8_t data[];         // First byte is always a size of data packet, 0 in case of 256 bytes (have CMDFLAG_DATASIZE_256 set)
} PACKET;
 */

 #define INSTRUCTION_SIZE_1BYTE     0x00
 #define INSTRUCTION_SIZE_2BYTES    0x01
 #define INSTRUCTION_SIZE_3BYTES    0x02
 #define INSTRUCTION_SIZE_4BYTES    0x03

 std::vector<uint8_t> vcReceived;
 std::vector<uint8_t> vcSentOut;

 uint8_t spi_exchange(uint8_t data) {
    if (options.options & OPTION_USE_SS) {
        EXPECT_EQ(SPI_SS, 0);
    } else {
        EXPECT_EQ(SPI_SS, 1);
    }
    vcReceived.emplace_back(data);
    EXPECT_LE(vcReceived.size(), vcSentOut.size());
    return vcSentOut[vcReceived.size() - 1]; //data;
}


TEST(OddProgStub, ProgLP51ProgramEnable)
{
    test_env_reset();
    options.options |= OPTION_USE_SS;
    vcSentOut.push_back(0x00);
    vcSentOut.push_back(0x00);
    
    uint8_t received_packet[] = {0x05/*packet_size*/, 0x00/*packet_checksum*/, 
        CMDFLAG_NEW_PACKET | CMDFLAG_LAST_PACKET | INSTRUCTION_SIZE_2BYTES/*cmd*/, 
        /*data*/ 0x00, 0xAC, 0x53};
    for (uint8_t kk = 2; kk < sizeof(received_packet); kk++) {
        received_packet[1] += received_packet[kk]; 
    }
    printf("sum=%d\n", received_packet[1]);
    received_packet[1] = ~received_packet[1];

    PACKET *p = (PACKET*) received_packet;
    int8_t ret = prog_packet_exchange(p);

    EXPECT_EQ(ret, 0);
    EXPECT_EQ(vcReceived.size(), 2);
    EXPECT_EQ(vcReceived[0], received_packet[4]);
    EXPECT_EQ(vcReceived[1], received_packet[5]);
}

