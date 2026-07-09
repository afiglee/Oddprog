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

// AT89LP51RD2/ED2/ID2 datasheet, Table 23-21 "Programming Command Summary" (page 221).
// Every command starts with the two preamble bytes AA 55 followed by the opcode.
#define ISP_PREAMBLE_1              0xAA
#define ISP_PREAMBLE_2              0x55
#define ISP_PROGRAM_ENABLE          0xAC /* + addr high 0x53 */
#define ISP_PROGRAM_ENABLE_ADDR     0x53
#define ISP_PARALLEL_ENABLE         0xAC /* + addr high 0x35 */
#define ISP_PARALLEL_ENABLE_ADDR    0x35
#define ISP_CHIP_ERASE              0x8A
#define ISP_READ_STATUS             0x60
#define ISP_LOAD_PAGE_BUFFER        0x51
#define ISP_WRITE_CODE_PAGE         0x50
#define ISP_WRITE_CODE_PAGE_AE      0x70
#define ISP_READ_CODE_PAGE          0x30
#define ISP_WRITE_DATA_PAGE         0xD0
#define ISP_WRITE_DATA_PAGE_AE      0xD2
#define ISP_READ_DATA_PAGE          0xB0
#define ISP_WRITE_USER_FUSES        0xE1
#define ISP_WRITE_USER_FUSES_AE     0xF1
#define ISP_READ_USER_FUSES         0x61
#define ISP_WRITE_LOCK_BITS         0xE4
#define ISP_READ_LOCK_BITS          0x64
#define ISP_WRITE_USER_SIG          0x52
#define ISP_WRITE_USER_SIG_AE       0x72
#define ISP_READ_USER_SIG           0x32
#define ISP_READ_ATMEL_SIG          0x38

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

// Builds a single NEW|LAST packet for one ISP command, feeds it through
// on_packet_received() and checks the MOSI byte stream.
//   instr:          instruction bytes (1..4), i.e. preambles + opcode (+ addr high)
//   payload:        data bytes exchanged after the instruction (addr low, data/dummy bytes)
//   write_direction:true = payload is written to the target,
//                   false = MISO bytes replace payload in work_buffer (read command)
//   miso_response:  bytes the target returns during the payload phase (read commands)
// Returns the payload area of work_buffer after the exchange.
static std::vector<uint8_t> run_isp_command(const std::vector<uint8_t> &instr,
                                            const std::vector<uint8_t> &payload,
                                            bool write_direction,
                                            const std::vector<uint8_t> &miso_response = {}) {
    std::vector<uint8_t> packet;
    packet.push_back(0); // packet_size, filled below
    packet.push_back(0); // packet_checksum, filled below
    uint8_t cmd = CMDFLAG_NEW_PACKET | CMDFLAG_LAST_PACKET | (uint8_t)(instr.size() - 1);
    if (!payload.empty()) {
        cmd |= CMDFLAG_DATASIZE;
        if (write_direction) {
            cmd |= CMDFLAG_WRITE_DIRECTION;
        }
    }
    packet.push_back(cmd);
    packet.push_back((uint8_t)payload.size()); // data[0]: total transfer size
    packet.insert(packet.end(), instr.begin(), instr.end());
    packet.insert(packet.end(), payload.begin(), payload.end());
    packet[0] = (uint8_t)(packet.size() - 1);
    fill_packet_checksum(packet.data(), packet.size());

    // MISO bytes: don't care during the instruction, then the target's response
    vcSentOut.insert(vcSentOut.end(), instr.size(), 0x00);
    vcSentOut.insert(vcSentOut.end(), miso_response.begin(), miso_response.end());
    while (vcSentOut.size() < instr.size() + payload.size()) {
        vcSentOut.push_back(0x00);
    }

    memcpy(work_buffer, packet.data(), packet.size());
    int8_t ret = on_packet_received((uint8_t)packet.size());
    EXPECT_EQ(ret, (int8_t)ERROR_OK);

    // The programmer clocks out the instruction followed by the payload as-is
    std::vector<uint8_t> expected_mosi = instr;
    expected_mosi.insert(expected_mosi.end(), payload.begin(), payload.end());
    EXPECT_EQ(vcReceived, expected_mosi);
    EXPECT_EQ(bytes_left, 0);
    if (options.options & OPTION_USE_SS) {
        EXPECT_EQ(SPI_SS, 1); // released after CMDFLAG_LAST_PACKET
    }

    PACKET *p = (PACKET *)work_buffer;
    const uint8_t *payload_start = p->data + 1 + instr.size();
    return std::vector<uint8_t>(payload_start, payload_start + payload.size());
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
    vcSentOut.push_back(0x00);
    vcSentOut.push_back(0x00);
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

// --- Table 23-21 command suite -------------------------------------------
// The full SPI byte stream of a command is AA 55 opcode addrH addrL [data...].
// The packet instruction field holds at most 4 bytes (AA 55 opcode addrH);
// addrL travels as the first payload byte. On read commands the MISO byte
// returned while addrL is shifted out is garbage and discarded by the host.

TEST(OddProgStub, ProgLP51ProgramEnableFullSequence)
{
    test_env_reset();
    options.options |= OPTION_USE_SS;
    run_isp_command({ISP_PREAMBLE_1, ISP_PREAMBLE_2, ISP_PROGRAM_ENABLE, ISP_PROGRAM_ENABLE_ADDR},
                    {}, true);
}

TEST(OddProgStub, ProgLP51ParallelEnable)
{
    test_env_reset();
    options.options |= OPTION_USE_SS;
    run_isp_command({ISP_PREAMBLE_1, ISP_PREAMBLE_2, ISP_PARALLEL_ENABLE, ISP_PARALLEL_ENABLE_ADDR},
                    {}, true);
}

TEST(OddProgStub, ProgLP51ChipErase)
{
    test_env_reset();
    options.options |= OPTION_USE_SS;
    // Chip Erase has no address or data; don't care bytes at the end may be omitted
    run_isp_command({ISP_PREAMBLE_1, ISP_PREAMBLE_2, ISP_CHIP_ERASE}, {}, true);
}

TEST(OddProgStub, ProgLP51ReadStatus)
{
    test_env_reset();
    options.options |= OPTION_USE_SS;
    // AA 55 60 xx xx Status-Out: one dummy payload byte for addrL, one for the status
    uint8_t status = 0x0E; // LOAD | SUCCESS | WRTINH, not BUSY
    std::vector<uint8_t> read_back =
        run_isp_command({ISP_PREAMBLE_1, ISP_PREAMBLE_2, ISP_READ_STATUS, 0x00},
                        {0x00, 0x00}, false, {0xFF, status});
    EXPECT_EQ(read_back[1], status);
}

TEST(OddProgStub, ProgLP51ReadStatusNoSlaveSelect)
{
    test_env_reset(); // OPTION_USE_SS not set: SS stays high, driven externally
    uint8_t status = 0x06; // SUCCESS | WRTINH
    std::vector<uint8_t> read_back =
        run_isp_command({ISP_PREAMBLE_1, ISP_PREAMBLE_2, ISP_READ_STATUS, 0x00},
                        {0x00, 0x00}, false, {0xFF, status});
    EXPECT_EQ(read_back[1], status);
}

TEST(OddProgStub, ProgLP51LoadPageBuffer)
{
    test_env_reset();
    options.options |= OPTION_USE_SS;
    // AA 55 51 xx addrL data... (addr high is don't care)
    run_isp_command({ISP_PREAMBLE_1, ISP_PREAMBLE_2, ISP_LOAD_PAGE_BUFFER, 0x00},
                    {/*addrL*/ 0x00, 0x11, 0x22, 0x33, 0x44}, true);
}

TEST(OddProgStub, ProgLP51WriteCodePage)
{
    test_env_reset();
    options.options |= OPTION_USE_SS;
    // AA 55 50 aaaaaaaa asbbbbbb data...
    run_isp_command({ISP_PREAMBLE_1, ISP_PREAMBLE_2, ISP_WRITE_CODE_PAGE, 0x12},
                    {/*addrL*/ 0x40, 0xDE, 0xAD, 0xBE, 0xEF}, true);
}

TEST(OddProgStub, ProgLP51WriteCodePageAutoErase)
{
    test_env_reset();
    options.options |= OPTION_USE_SS;
    run_isp_command({ISP_PREAMBLE_1, ISP_PREAMBLE_2, ISP_WRITE_CODE_PAGE_AE, 0x12},
                    {/*addrL*/ 0x00, 0x01, 0x02, 0x03, 0x04}, true);
}

TEST(OddProgStub, ProgLP51ReadCodePage)
{
    test_env_reset();
    options.options |= OPTION_USE_SS;
    std::vector<uint8_t> code = {0x02, 0x00, 0x30, 0x75, 0x81};
    std::vector<uint8_t> miso = {0xFF}; // garbage while addrL shifts out
    miso.insert(miso.end(), code.begin(), code.end());
    std::vector<uint8_t> read_back =
        run_isp_command({ISP_PREAMBLE_1, ISP_PREAMBLE_2, ISP_READ_CODE_PAGE, 0x12},
                        std::vector<uint8_t>(code.size() + 1, 0x00), false, miso);
    EXPECT_EQ(std::vector<uint8_t>(read_back.begin() + 1, read_back.end()), code);
}

TEST(OddProgStub, ProgLP51WriteDataPage)
{
    test_env_reset();
    options.options |= OPTION_USE_SS;
    // AA 55 D0 000aaaaa asbbbbbb data...
    run_isp_command({ISP_PREAMBLE_1, ISP_PREAMBLE_2, ISP_WRITE_DATA_PAGE, 0x05},
                    {/*addrL*/ 0x00, 0xA5, 0x5A}, true);
}

TEST(OddProgStub, ProgLP51WriteDataPageAutoErase)
{
    test_env_reset();
    options.options |= OPTION_USE_SS;
    run_isp_command({ISP_PREAMBLE_1, ISP_PREAMBLE_2, ISP_WRITE_DATA_PAGE_AE, 0x05},
                    {/*addrL*/ 0x00, 0xA5, 0x5A}, true);
}

TEST(OddProgStub, ProgLP51ReadDataPage)
{
    test_env_reset();
    options.options |= OPTION_USE_SS;
    std::vector<uint8_t> data = {0x10, 0x20, 0x30};
    std::vector<uint8_t> miso = {0xFF};
    miso.insert(miso.end(), data.begin(), data.end());
    std::vector<uint8_t> read_back =
        run_isp_command({ISP_PREAMBLE_1, ISP_PREAMBLE_2, ISP_READ_DATA_PAGE, 0x05},
                        std::vector<uint8_t>(data.size() + 1, 0x00), false, miso);
    EXPECT_EQ(std::vector<uint8_t>(read_back.begin() + 1, read_back.end()), data);
}

TEST(OddProgStub, ProgLP51WriteUserFuses)
{
    test_env_reset();
    options.options |= OPTION_USE_SS;
    // Fuse data bytes must be 00h or FFh
    run_isp_command({ISP_PREAMBLE_1, ISP_PREAMBLE_2, ISP_WRITE_USER_FUSES, 0x00},
                    {/*addrL*/ 0x00, 0xFF, 0x00, 0xFF}, true);
}

TEST(OddProgStub, ProgLP51WriteUserFusesAutoErase)
{
    test_env_reset();
    options.options |= OPTION_USE_SS;
    run_isp_command({ISP_PREAMBLE_1, ISP_PREAMBLE_2, ISP_WRITE_USER_FUSES_AE, 0x00},
                    {/*addrL*/ 0x00, 0xFF, 0xFF}, true);
}

TEST(OddProgStub, ProgLP51ReadUserFuses)
{
    test_env_reset();
    options.options |= OPTION_USE_SS;
    std::vector<uint8_t> fuses = {0xFF, 0x00, 0xFF};
    std::vector<uint8_t> miso = {0xFF};
    miso.insert(miso.end(), fuses.begin(), fuses.end());
    std::vector<uint8_t> read_back =
        run_isp_command({ISP_PREAMBLE_1, ISP_PREAMBLE_2, ISP_READ_USER_FUSES, 0x00},
                        std::vector<uint8_t>(fuses.size() + 1, 0x00), false, miso);
    EXPECT_EQ(std::vector<uint8_t>(read_back.begin() + 1, read_back.end()), fuses);
}

TEST(OddProgStub, ProgLP51WriteLockBits)
{
    test_env_reset();
    options.options |= OPTION_USE_SS;
    run_isp_command({ISP_PREAMBLE_1, ISP_PREAMBLE_2, ISP_WRITE_LOCK_BITS, 0x00},
                    {/*addrL*/ 0x00, 0xFF, 0xFF}, true);
}

TEST(OddProgStub, ProgLP51ReadLockBits)
{
    test_env_reset();
    options.options |= OPTION_USE_SS;
    std::vector<uint8_t> locks = {0xFF, 0xFF, 0x00};
    std::vector<uint8_t> miso = {0xFF};
    miso.insert(miso.end(), locks.begin(), locks.end());
    std::vector<uint8_t> read_back =
        run_isp_command({ISP_PREAMBLE_1, ISP_PREAMBLE_2, ISP_READ_LOCK_BITS, 0x00},
                        std::vector<uint8_t>(locks.size() + 1, 0x00), false, miso);
    EXPECT_EQ(std::vector<uint8_t>(read_back.begin() + 1, read_back.end()), locks);
}

TEST(OddProgStub, ProgLP51WriteUserSignature)
{
    test_env_reset();
    options.options |= OPTION_USE_SS;
    run_isp_command({ISP_PREAMBLE_1, ISP_PREAMBLE_2, ISP_WRITE_USER_SIG, 0x00},
                    {/*addrL*/ 0x00, 'O', 'd', 'd'}, true);
}

TEST(OddProgStub, ProgLP51WriteUserSignatureAutoErase)
{
    test_env_reset();
    options.options |= OPTION_USE_SS;
    run_isp_command({ISP_PREAMBLE_1, ISP_PREAMBLE_2, ISP_WRITE_USER_SIG_AE, 0x00},
                    {/*addrL*/ 0x00, 'O', 'd', 'd'}, true);
}

TEST(OddProgStub, ProgLP51ReadUserSignature)
{
    test_env_reset();
    options.options |= OPTION_USE_SS;
    std::vector<uint8_t> sig = {'O', 'd', 'd'};
    std::vector<uint8_t> miso = {0xFF};
    miso.insert(miso.end(), sig.begin(), sig.end());
    std::vector<uint8_t> read_back =
        run_isp_command({ISP_PREAMBLE_1, ISP_PREAMBLE_2, ISP_READ_USER_SIG, 0x00},
                        std::vector<uint8_t>(sig.size() + 1, 0x00), false, miso);
    EXPECT_EQ(std::vector<uint8_t>(read_back.begin() + 1, read_back.end()), sig);
}

TEST(OddProgStub, ProgLP51ReadAtmelSignature)
{
    test_env_reset();
    options.options |= OPTION_USE_SS;
    std::vector<uint8_t> sig = {0x1E, 0x51, 0xE2}; // device signature bytes
    std::vector<uint8_t> miso = {0xFF};
    miso.insert(miso.end(), sig.begin(), sig.end());
    std::vector<uint8_t> read_back =
        run_isp_command({ISP_PREAMBLE_1, ISP_PREAMBLE_2, ISP_READ_ATMEL_SIG, 0x00},
                        std::vector<uint8_t>(sig.size() + 1, 0x00), false, miso);
    EXPECT_EQ(std::vector<uint8_t>(read_back.begin() + 1, read_back.end()), sig);
}

// --- Multi-packet transfers ------------------------------------------------

// A full 64-byte code page (+ addrL) does not fit in one 64-byte work buffer:
// the command is split into a NEW packet and a continuation LAST packet while
// SS is held low in between.
TEST(OddProgStub, ProgLP51WriteCodePageMultiPacket)
{
    test_env_reset();
    options.options |= OPTION_USE_SS;

    std::vector<uint8_t> instr = {ISP_PREAMBLE_1, ISP_PREAMBLE_2, ISP_WRITE_CODE_PAGE, 0x12};
    std::vector<uint8_t> payload;
    payload.push_back(0x40); // addrL
    for (uint8_t kk = 0; kk < 64; kk++) {
        payload.push_back(kk);
    }
    vcSentOut.insert(vcSentOut.end(), instr.size() + payload.size(), 0x00);

    // Packet 1: NEW, size byte + instruction + first 56 payload bytes -> packet_size 63
    const size_t first_chunk = BUFFER_SIZE - 3 /*header*/ - 1 /*size byte*/ - instr.size();
    std::vector<uint8_t> packet1;
    packet1.push_back(0);
    packet1.push_back(0);
    packet1.push_back(CMDFLAG_NEW_PACKET | CMDFLAG_WRITE_DIRECTION | CMDFLAG_DATASIZE |
                      INSTRUCTION_SIZE_4BYTES);
    packet1.push_back((uint8_t)payload.size());
    packet1.insert(packet1.end(), instr.begin(), instr.end());
    packet1.insert(packet1.end(), payload.begin(), payload.begin() + first_chunk);
    packet1[0] = (uint8_t)(packet1.size() - 1);
    fill_packet_checksum(packet1.data(), packet1.size());

    memcpy(work_buffer, packet1.data(), packet1.size());
    EXPECT_EQ(on_packet_received((uint8_t)packet1.size()), (int8_t)ERROR_OK);
    EXPECT_EQ(SPI_SS, 0); // SS stays low: command not finished yet
    EXPECT_EQ(bytes_left, payload.size() - first_chunk);

    // Packet 2: continuation with the remaining payload, LAST releases SS
    std::vector<uint8_t> packet2;
    packet2.push_back(0);
    packet2.push_back(0);
    packet2.push_back(CMDFLAG_LAST_PACKET | CMDFLAG_WRITE_DIRECTION);
    packet2.insert(packet2.end(), payload.begin() + first_chunk, payload.end());
    packet2[0] = (uint8_t)(packet2.size() - 1);
    fill_packet_checksum(packet2.data(), packet2.size());

    memcpy(work_buffer, packet2.data(), packet2.size());
    EXPECT_EQ(on_packet_received((uint8_t)packet2.size()), (int8_t)ERROR_OK);
    EXPECT_EQ(SPI_SS, 1);
    EXPECT_EQ(bytes_left, 0);

    std::vector<uint8_t> expected_mosi = instr;
    expected_mosi.insert(expected_mosi.end(), payload.begin(), payload.end());
    EXPECT_EQ(vcReceived, expected_mosi);
}

// Reading a full 64-byte code page across two packets: each packet's data
// area is replaced in place with the bytes read from MISO.
TEST(OddProgStub, ProgLP51ReadCodePageMultiPacket)
{
    test_env_reset();
    options.options |= OPTION_USE_SS;

    std::vector<uint8_t> instr = {ISP_PREAMBLE_1, ISP_PREAMBLE_2, ISP_READ_CODE_PAGE, 0x12};
    std::vector<uint8_t> code;
    for (uint8_t kk = 0; kk < 64; kk++) {
        code.push_back((uint8_t)(0x80 + kk));
    }
    const size_t payload_size = code.size() + 1; // addrL + 64 data bytes

    vcSentOut.insert(vcSentOut.end(), instr.size(), 0x00);
    vcSentOut.push_back(0xFF); // garbage while addrL shifts out
    vcSentOut.insert(vcSentOut.end(), code.begin(), code.end());

    const size_t first_chunk = BUFFER_SIZE - 3 - 1 - instr.size(); // 56

    // Packet 1: NEW, addrL + dummy bytes for the first chunk
    std::vector<uint8_t> packet1;
    packet1.push_back(0);
    packet1.push_back(0);
    packet1.push_back(CMDFLAG_NEW_PACKET | CMDFLAG_DATASIZE | INSTRUCTION_SIZE_4BYTES);
    packet1.push_back((uint8_t)payload_size);
    packet1.insert(packet1.end(), instr.begin(), instr.end());
    packet1.push_back(0x40); // addrL
    packet1.insert(packet1.end(), first_chunk - 1, 0x00); // dummy bytes
    packet1[0] = (uint8_t)(packet1.size() - 1);
    fill_packet_checksum(packet1.data(), packet1.size());

    memcpy(work_buffer, packet1.data(), packet1.size());
    EXPECT_EQ(on_packet_received((uint8_t)packet1.size()), (int8_t)ERROR_OK);
    EXPECT_EQ(SPI_SS, 0);

    // First chunk read back in place: garbage byte, then code[0..first_chunk-2]
    PACKET *p = (PACKET *)work_buffer;
    const uint8_t *chunk1 = p->data + 1 + instr.size();
    EXPECT_EQ(chunk1[0], 0xFF);
    for (size_t kk = 1; kk < first_chunk; kk++) {
        EXPECT_EQ(chunk1[kk], code[kk - 1]);
    }

    // Packet 2: continuation with dummy bytes for the rest
    std::vector<uint8_t> packet2;
    packet2.push_back(0);
    packet2.push_back(0);
    packet2.push_back(CMDFLAG_LAST_PACKET);
    packet2.insert(packet2.end(), payload_size - first_chunk, 0x00);
    packet2[0] = (uint8_t)(packet2.size() - 1);
    fill_packet_checksum(packet2.data(), packet2.size());

    memcpy(work_buffer, packet2.data(), packet2.size());
    EXPECT_EQ(on_packet_received((uint8_t)packet2.size()), (int8_t)ERROR_OK);
    EXPECT_EQ(SPI_SS, 1);
    EXPECT_EQ(bytes_left, 0);

    for (size_t kk = 0; kk < payload_size - first_chunk; kk++) {
        EXPECT_EQ(p->data[kk], code[first_chunk - 1 + kk]);
    }
}

// CMDFLAG_DATASIZE_256: 256 data bytes streamed over five packets
TEST(OddProgStub, ProgLP51DataSize256MultiPacket)
{
    test_env_reset();
    options.options |= OPTION_USE_SS;

    std::vector<uint8_t> instr = {ISP_PREAMBLE_1, ISP_PREAMBLE_2, ISP_LOAD_PAGE_BUFFER, 0x00};
    std::vector<uint8_t> payload;
    for (uint16_t kk = 0; kk < 256; kk++) {
        payload.push_back((uint8_t)kk);
    }
    vcSentOut.insert(vcSentOut.end(), instr.size() + payload.size(), 0x00);

    const size_t first_chunk = BUFFER_SIZE - 3 - 1 - instr.size(); // 56
    const size_t cont_chunk = BUFFER_SIZE - 3;                     // 61

    std::vector<uint8_t> packet1;
    packet1.push_back(0);
    packet1.push_back(0);
    packet1.push_back(CMDFLAG_NEW_PACKET | CMDFLAG_WRITE_DIRECTION | CMDFLAG_DATASIZE_256 |
                      INSTRUCTION_SIZE_4BYTES);
    packet1.push_back(0x00); // size byte is 0 for 256-byte transfers
    packet1.insert(packet1.end(), instr.begin(), instr.end());
    packet1.insert(packet1.end(), payload.begin(), payload.begin() + first_chunk);
    packet1[0] = (uint8_t)(packet1.size() - 1);
    fill_packet_checksum(packet1.data(), packet1.size());

    memcpy(work_buffer, packet1.data(), packet1.size());
    EXPECT_EQ(on_packet_received((uint8_t)packet1.size()), (int8_t)ERROR_OK);
    EXPECT_EQ(SPI_SS, 0);
    EXPECT_EQ(bytes_left, 256 - first_chunk);

    size_t sent = first_chunk;
    while (sent < payload.size()) {
        size_t chunk = std::min(cont_chunk, payload.size() - sent);
        bool last = (sent + chunk == payload.size());
        std::vector<uint8_t> packet;
        packet.push_back(0);
        packet.push_back(0);
        packet.push_back((last ? CMDFLAG_LAST_PACKET : 0) | CMDFLAG_WRITE_DIRECTION);
        packet.insert(packet.end(), payload.begin() + sent, payload.begin() + sent + chunk);
        packet[0] = (uint8_t)(packet.size() - 1);
        fill_packet_checksum(packet.data(), packet.size());

        memcpy(work_buffer, packet.data(), packet.size());
        EXPECT_EQ(on_packet_received((uint8_t)packet.size()), (int8_t)ERROR_OK);
        sent += chunk;
        EXPECT_EQ(SPI_SS, last ? 1 : 0);
        EXPECT_EQ(bytes_left, payload.size() - sent);
    }

    std::vector<uint8_t> expected_mosi = instr;
    expected_mosi.insert(expected_mosi.end(), payload.begin(), payload.end());
    EXPECT_EQ(vcReceived, expected_mosi);
}
