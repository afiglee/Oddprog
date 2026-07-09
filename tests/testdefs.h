#ifndef _TESTDEFS_H__
#define _TESTDEFS_H__

#include "defs.h"

extern "C" {
    void uart(void);
}

void test_env_reset();

#include <vector>
extern std::vector<uint8_t> vcSerialOut; // bytes sent out through SERIAL_SEND_BYTE

#endif