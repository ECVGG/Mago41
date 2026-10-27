/**
 * @file test_mago_mfsk.c
 * @brief Mago41 - Unit tests for MFSK/Golay encoding
 *
 * Run with: make test (requires Unity test framework)
 */

#include "unity.h"
#include "mago_mfsk.h"

/* ============================================================================
 * GOLAY (24,12) TESTS
 * ============================================================================ */

void test_golay_encode_decode_no_errors(void) {
    // Test all 12-bit patterns (sample)
    for (uint16_t data = 0; data < 4096; data += 17) {  // Sample every 17th
        uint32_t encoded = MagoMFSK_GolayEncode(data);
        uint16_t decoded = MagoMFSK_GolayDecode(encoded);
        TEST_ASSERT_EQUAL_UINT16(data, decoded);
    }
}

void test_golay_encode_decode_single_error(void) {
    uint16_t data = 0xABC;
    uint32_t encoded = MagoMFSK_GolayEncode(data);

    // Introduce single bit error at each position
    for (int bit = 0; bit < 24; bit++) {
        uint32_t corrupted = encoded ^ (1u << bit);
        uint16_t decoded = MagoMFSK_GolayDecode(corrupted);
        TEST_ASSERT_EQUAL_UINT16(data, decoded);
    }
}

void test_golay_encode_decode_double_error(void) {
    uint16_t data = 0x555;
    uint32_t encoded = MagoMFSK_GolayEncode(data);

    // Test some double error patterns
    for (int bit1 = 0; bit1 < 24; bit1 += 3) {
        for (int bit2 = bit1 + 1; bit2 < 24; bit2 += 5) {
            uint32_t corrupted = encoded ^ (1u << bit1) ^ (1u << bit2);
            uint16_t decoded = MagoMFSK_GolayDecode(corrupted);
            // Golay corrects up to 3 errors, so double should work
            TEST_ASSERT_EQUAL_UINT16(data, decoded);
        }
    }
}

/* ============================================================================
 * INTERLEAVING TESTS
 * ============================================================================ */

void test_interleave_deinterleave_2x(void) {
    uint8_t input[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    uint8_t interleaved[16];
    uint8_t output[16];

    MagoMFSK_Interleave(input, interleaved, 16, 2);
    MagoMFSK_Deinterleave(interleaved, output, 16, 2);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(input, output, 16);
}

void test_interleave_deinterleave_4x(void) {
    uint8_t input[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    uint8_t interleaved[16];
    uint8_t output[16];

    MagoMFSK_Interleave(input, interleaved, 16, 4);
    MagoMFSK_Deinterleave(interleaved, output, 16, 4);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(input, output, 16);
}

/* ============================================================================
 * SCRAMBLING TESTS
 * ============================================================================ */

void test_scramble_descramble(void) {
    uint8_t data[32];
    for (int i = 0; i < 32; i++) data[i] = i * 7;

    uint8_t scrambled[32];
    memcpy(scrambled, data, 32);

    MagoMFSK_ScramblerReset(0x123);
    MagoMFSK_Scramble(scrambled, 32);

    MagoMFSK_ScramblerReset(0x123);
    MagoMFSK_Descramble(scrambled, 32);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, scrambled, 32);
}

/* ============================================================================
 * CRC TESTS
 * ============================================================================ */

void test_crc16_known_values(void) {
    // Known CRC-CCITT values
    uint8_t data1[] = {0x00};
    TEST_ASSERT_EQUAL_HEX16(0x84C0, MagoMFSK_CRC16(data1, 1));

    uint8_t data2[] = {0x41, 0x42, 0x43, 0x44};  // "ABCD"
    TEST_ASSERT_EQUAL_HEX16(0x29B1, MagoMFSK_CRC16(data2, 4));

    uint8_t data3[] = "123456789";
    TEST_ASSERT_EQUAL_HEX16(0x29B1, MagoMFSK_CRC16(data3, 9));
}

/* ============================================================================
 * HORUS V1 TESTS
 * ============================================================================ */

void test_horus_v1_encode_decode(void) {
    MagoHorusPacketV1_t pkt = {
        .callsign_id = 0x1234,
        .sequence = 0x42,
        .latitude = 12345678,
        .longitude = -9876543,
        .altitude = 10000,
        .speed = 150,
        .direction = 180,
        .temperature = -20,
        .battery_mv = 3600,
        .solar_mv = 4000,
        .flags = 0x01,
        .reserved = 0
    };

    uint8_t encoded[MAGO_MFSK_MAX_PACKET];
    uint16_t encoded_len;

    MagoHALStatus_t status = MagoMFSK_EncodeHorusV1(&pkt, encoded, &encoded_len);
    TEST_ASSERT_EQUAL(MAGO_HAL_OK, status);
    TEST_ASSERT_GREATER_THAN(0, encoded_len);

    MagoHorusPacketV1_t decoded = {0};
    status = MagoMFSK_DecodeHorusV1(encoded, encoded_len, &decoded);
    TEST_ASSERT_EQUAL(MAGO_HAL_OK, status);

    TEST_ASSERT_EQUAL_UINT16(pkt.callsign_id, decoded.callsign_id);
    TEST_ASSERT_EQUAL_UINT8(pkt.sequence, decoded.sequence);
    TEST_ASSERT_EQUAL_INT32(pkt.latitude, decoded.latitude);
    TEST_ASSERT_EQUAL_INT32(pkt.longitude, decoded.longitude);
    TEST_ASSERT_EQUAL_INT16(pkt.altitude, decoded.altitude);
}

/* ============================================================================
 * SYMBOL MAPPING TESTS
 * ============================================================================ */

void test_symbol_mapping_4fsk(void) {
    uint8_t bits[] = {0,0, 0,1, 1,0, 1,1, 0,0, 1,1};
    uint8_t symbols[6];

    MagoMFSK_MapSymbols4FSK(bits, 12, symbols);

    TEST_ASSERT_EQUAL_UINT8(0, symbols[0]);  // 00
    TEST_ASSERT_EQUAL_UINT8(1, symbols[1]);  // 01
    TEST_ASSERT_EQUAL_UINT8(2, symbols[2]);  // 10
    TEST_ASSERT_EQUAL_UINT8(3, symbols[3]);  // 11
    TEST_ASSERT_EQUAL_UINT8(0, symbols[4]);
    TEST_ASSERT_EQUAL_UINT8(3, symbols[5]);
}

void test_symbol_mapping_16fsk(void) {
    uint8_t bits[] = {0,0,0,0, 1,1,1,1, 1,0,1,0, 0,1,0,1};
    uint8_t symbols[4];

    MagoMFSK_MapSymbols16FSK(bits, 16, symbols);

    TEST_ASSERT_EQUAL_UINT8(0x0, symbols[0]);
    TEST_ASSERT_EQUAL_UINT8(0xF, symbols[1]);
    TEST_ASSERT_EQUAL_UINT8(0xA, symbols[2]);
    TEST_ASSERT_EQUAL_UINT8(0x5, symbols[3]);
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_golay_encode_decode_no_errors);
    RUN_TEST(test_golay_encode_decode_single_error);
    RUN_TEST(test_golay_encode_decode_double_error);
    RUN_TEST(test_interleave_deinterleave_2x);
    RUN_TEST(test_interleave_deinterleave_4x);
    RUN_TEST(test_scramble_descramble);
    RUN_TEST(test_crc16_known_values);
    RUN_TEST(test_horus_v1_encode_decode);
    RUN_TEST(test_symbol_mapping_4fsk);
    RUN_TEST(test_symbol_mapping_16fsk);

    return UNITY_END();
}