#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <iostream>
#include <vector>
#include <map>
#include <array>
#include <optional>

#include "defs.h"


uint8_t interrupt_flags;
uint8_t SBUF;
uint8_t SPI_SS;

uint8_t spi_exchange(uint8_t data) {
    return data;
}

TEST(OddProgStub, PacketReceive)
{
    EXPECT_STREQ("5", "5");
    EXPECT_EQ(0,7);
}

