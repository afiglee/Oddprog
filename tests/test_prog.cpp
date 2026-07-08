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
    uint8_t packet_checksum; // two's complement: packet_checksum + cmd + sum(data[0..packet_size-3]) == 0 (mod 256)
    uint8_t cmd;             // contains cmd flag
    uint8_t data[];         // First byte is always a size of data packet, 0 in case of 256 bytes (have CMDFLAG_DATASIZE_256 set)
} PACKET;
 */

// Computes packet[1] (packet_checksum) so that
// packet_checksum + cmd + sum(data) == 0 (mod 256)
static void fill_packet_checksum(uint8_t *packet, size_t total_size) {
    uint8_t sum = 0;
    for (size_t kk = 2; kk < total_size; kk++) {
        sum += packet[kk];
    }
    packet[1] = (uint8_t)(0 - sum);
}

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
    fill_packet_checksum(received_packet, sizeof(received_packet));

    PACKET *p = (PACKET*) received_packet;
    int8_t ret = prog_packet_exchange(p);

    EXPECT_EQ(ret, 0);
    EXPECT_EQ(vcReceived.size(), 2);
    EXPECT_EQ(vcReceived[0], received_packet[4]);
    EXPECT_EQ(vcReceived[1], received_packet[5]);
}

TEST(OddProgStub, PacketChecksumValid)
{
    test_env_reset();
    uint8_t received_packet[] = {0x05/*packet_size*/, 0x00/*packet_checksum*/,
        CMDFLAG_NEW_PACKET | CMDFLAG_LAST_PACKET | INSTRUCTION_SIZE_2BYTES/*cmd*/,
        /*data*/ 0x00, 0xAC, 0x53};
    fill_packet_checksum(received_packet, sizeof(received_packet));

    memcpy(work_buffer, received_packet, sizeof(received_packet));
    int8_t ret = on_packet_received(sizeof(received_packet));

    EXPECT_EQ(ret, (int8_t)ERROR_OK);
}

TEST(OddProgStub, PacketChecksumInvalid)
{
    test_env_reset();
    uint8_t received_packet[] = {0x05/*packet_size*/, 0x00/*packet_checksum*/,
        CMDFLAG_NEW_PACKET | CMDFLAG_LAST_PACKET | INSTRUCTION_SIZE_2BYTES/*cmd*/,
        /*data*/ 0x00, 0xAC, 0x53};
    fill_packet_checksum(received_packet, sizeof(received_packet));
    received_packet[4] += 1; // corrupt one data byte

    memcpy(work_buffer, received_packet, sizeof(received_packet));
    int8_t ret = on_packet_received(sizeof(received_packet));

    EXPECT_EQ(ret, (int8_t)ERROR_PACKET_CHECKSUM);
}

TEST(OddProgStub, PacketChecksumOptionsPacket)
{
    test_env_reset();
    uint8_t received_packet[] = {0x04/*packet_size*/, 0x00/*packet_checksum*/,
        CMDFLAG_OPTIONS/*cmd*/,
        /*data*/ 0x00, OPTION_USE_SS};
    fill_packet_checksum(received_packet, sizeof(received_packet));

    memcpy(work_buffer, received_packet, sizeof(received_packet));
    int8_t ret = on_packet_received(sizeof(received_packet));

    EXPECT_EQ(ret, (int8_t)ERROR_OK);
    EXPECT_EQ(options.options & OPTION_USE_SS, OPTION_USE_SS);
}

