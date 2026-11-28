/** @file test_as608_packet.c @brief Packet codec wire vectors and exhaustive boundary tests. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "as608_packet.h"

#if defined(AS608_H) || defined(AS608_TRANSACTION_H) || defined(AS608_PORT_H)
#error "Packet codec must not depend on command, transport or port layers"
#endif

/* Check documented packet identifiers and reserved values. */
static void test_packet_identifiers(void) {
    /* Protocol wire IDs, not sequential enum ordinals. */
    assert(AS608_PACKET_START_BYTE_0 == 0xEFU);
    assert(AS608_PACKET_START_BYTE_1 == 0x01U);

    const uint8_t valid_kinds[] = { 0x01U, 0x02U, 0x07U, 0x08U };
    uint8_t wire[AS608_PACKET_HEADER_BYTES];
    for (size_t i = 0U; i < sizeof(valid_kinds); ++i) {
        const as608_packet_header_t h = { UINT32_C(0xFFFFFFFF), valid_kinds[i], 1U };
        assert(as608_packet_encode_header(&h, wire) == AS608_STATUS_OK);
        assert(wire[0] == AS608_PACKET_START_BYTE_0);
        assert(wire[1] == AS608_PACKET_START_BYTE_1);
        assert(wire[6] == valid_kinds[i]);
    }

    for (uint8_t kind = 0x03U; kind <= 0x06U; ++kind) {
        const as608_packet_header_t h = { UINT32_C(0xFFFFFFFF), kind, 1U };
        assert(as608_packet_encode_header(&h, wire) == AS608_STATUS_INVALID_ARG);
    }
}

/* Check an independent GetImage wire vector. */
static void test_get_image_vector(void) {
    const as608_packet_header_t h = { UINT32_C(0xFFFFFFFF), AS608_PACKET_COMMAND, 1U };
    const uint8_t want[] = { 0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x03 };
    uint8_t wire[AS608_PACKET_HEADER_BYTES] = { 0 };
    uint8_t trailer[AS608_PACKET_CHECKSUM_BYTES] = { 0 };
    const uint8_t command = 0x01;
    assert(as608_packet_encode_header(&h, wire) == AS608_STATUS_OK);
    assert(memcmp(wire, want, sizeof(want)) == 0);
    uint16_t sum = as608_packet_checksum_begin(&h);
    assert(sum == 0x0004);
    sum = as608_packet_checksum_update(sum, &command, 1U);
    assert(sum == 0x0005);
    as608_packet_checksum_encode(sum, trailer);
    assert(trailer[0] == 0x00 && trailer[1] == 0x05);
    assert(as608_packet_checksum_matches(sum, trailer));
    assert(!as608_packet_checksum_matches((uint16_t)(sum + 1U), trailer));

    as608_packet_header_t decoded = { 0U, 0U, 0U };
    assert(as608_packet_decode_header(wire, UINT32_MAX, 1U, &decoded) == AS608_STATUS_OK);
    assert(decoded.address == h.address && decoded.kind == h.kind &&
           decoded.payload_size == h.payload_size);
}

/* Verify maximum payload and rejected output preservation. */
static void test_packet_bounds(void) {
    as608_packet_header_t h = { UINT32_C(0x12345678), AS608_PACKET_END_DATA, 65533U };
    uint8_t wire[AS608_PACKET_HEADER_BYTES] = { 0 };
    assert(as608_packet_encode_header(&h, wire) == AS608_STATUS_OK);
    const uint8_t want[] = { 0xEF, 0x01, 0x12, 0x34, 0x56, 0x78, 0x08, 0xFF, 0xFF };
    assert(memcmp(wire, want, sizeof(want)) == 0);

    as608_packet_header_t parsed = { UINT32_C(0xDEADBEEF), 0xFA, 0xBABE };
    unsigned char saved[sizeof(parsed)];
    (void)memcpy(saved, &parsed, sizeof(saved));
    assert(as608_packet_decode_header(wire, h.address, 65532U, &parsed) == AS608_STATUS_FRAME_ERROR);
    assert(memcmp(saved, &parsed, sizeof(parsed)) == 0);
    assert(as608_packet_decode_header(wire, h.address, 65533U, &parsed) == AS608_STATUS_OK);
    assert(parsed.payload_size == 65533U);

    h.payload_size = 65534U;
    memset(wire, 0xAA, sizeof(wire));
    assert(as608_packet_encode_header(&h, wire) == AS608_STATUS_INVALID_ARG);
    for (size_t i = 0U; i < sizeof(wire); ++i) {
        assert(wire[i] == 0xAA);
    }

    h.payload_size = 1U;
    h.kind = 0x03;
    assert(as608_packet_encode_header(&h, wire) == AS608_STATUS_INVALID_ARG);
    for (size_t i = 0U; i < sizeof(wire); ++i) {
        assert(wire[i] == 0xAA);
    }
}

/* Reject malformed headers and null arguments. */
static void test_decode_bad_frames(void) {
    const as608_packet_header_t h = { UINT32_C(0x10203040), AS608_PACKET_ACK, 1U };
    uint8_t wire[AS608_PACKET_HEADER_BYTES];
    as608_packet_header_t parsed = { 0U, 0U, 0U };
    assert(as608_packet_encode_header(&h, wire) == AS608_STATUS_OK);
    assert(as608_packet_decode_header(NULL, h.address, 100U, &parsed) == AS608_STATUS_INVALID_ARG);
    assert(as608_packet_decode_header(wire, h.address, 100U, NULL) == AS608_STATUS_INVALID_ARG);
    assert(as608_packet_encode_header(NULL, wire) == AS608_STATUS_INVALID_ARG);
    assert(as608_packet_encode_header(&h, NULL) == AS608_STATUS_INVALID_ARG);

    assert(as608_packet_decode_header(wire, UINT32_MAX, 100U, &parsed) == AS608_STATUS_FRAME_ERROR);
    assert(as608_packet_decode_header(wire, h.address, 0U, &parsed) == AS608_STATUS_FRAME_ERROR);
    wire[0] = 0U;
    assert(as608_packet_decode_header(wire, h.address, 100U, &parsed) == AS608_STATUS_FRAME_ERROR);
    wire[0] = 0xEF;
    wire[6] = 0xFF;
    assert(as608_packet_decode_header(wire, h.address, 100U, &parsed) == AS608_STATUS_FRAME_ERROR);
    wire[6] = AS608_PACKET_ACK;
    wire[7] = 0U;
    wire[8] = 1U;
    assert(as608_packet_decode_header(wire, h.address, 100U, &parsed) == AS608_STATUS_FRAME_ERROR);
    wire[8] = 2U;
    assert(as608_packet_decode_header(wire, h.address, 0U, &parsed) == AS608_STATUS_OK);
    assert(parsed.payload_size == 0U);
}

/* Compare a complete checksum with fragmented accumulation. */
static void test_streaming_checksum(void) {
    uint8_t data[1024];
    memset(data, 0xFF, sizeof(data));
    const as608_packet_header_t h = { UINT32_C(0), AS608_PACKET_DATA, 1024U };
    uint16_t whole = as608_packet_checksum_update(as608_packet_checksum_begin(&h), data, sizeof(data));
    uint16_t parts = as608_packet_checksum_begin(&h);
    for (size_t i = 0U; i < sizeof(data); i += 17U) {
        const size_t n = (sizeof(data) - i < 17U) ? sizeof(data) - i : 17U;
        parts = as608_packet_checksum_update(parts, &data[i], n);
    }
    assert(parts == whole);
    assert(as608_packet_checksum_update(parts, NULL, 0U) == parts);
    uint8_t wire[AS608_PACKET_CHECKSUM_BYTES];
    as608_packet_checksum_encode(whole, wire);
    assert(as608_packet_checksum_matches(whole, wire));
}

/* Exercise all 256 packet IDs independently of the implementation enum. */
static void test_all_kinds(void) {
    for (unsigned int id = 0U; id < 256U; ++id) {
        const bool valid = (id == 1U || id == 2U || id == 7U || id == 8U);
        const as608_packet_header_t input = {UINT32_C(0x89ABCDEF), (uint8_t)id, 1U};
        uint8_t wire[11U];
        uint8_t saved_wire[sizeof(wire)];
        as608_packet_header_t output = {UINT32_C(0x12345678), 0xAAU, 0xABCDU};
        unsigned char saved[sizeof(output)];

        (void)memset(wire, 0xA5, sizeof(wire));
        (void)memcpy(saved_wire, wire, sizeof(wire));
        (void)memcpy(saved, &output, sizeof(saved));

        assert(as608_packet_encode_header(&input, wire + 1U) ==
               (valid ? AS608_STATUS_OK : AS608_STATUS_INVALID_ARG));
        if (!valid) {
            assert(memcmp(wire, saved_wire, sizeof(wire)) == 0);
        }
        assert(wire[0] == 0xA5U && wire[10] == 0xA5U);

        const uint8_t raw[] = {0xEFU, 1U, 0x89U, 0xABU, 0xCDU, 0xEFU,
                               (uint8_t)id, 0U, 3U};
        assert(as608_packet_decode_header(raw, input.address, 1U, &output) ==
               (valid ? AS608_STATUS_OK : AS608_STATUS_FRAME_ERROR));
        if (valid) {
            assert(memcmp(wire + 1U, raw, sizeof(raw)) == 0);
            assert(output.address == input.address && output.kind == id &&
                   output.payload_size == 1U);
        } else {
            assert(memcmp(saved, &output, sizeof(saved)) == 0);
        }
    }
}

/* Exhaust every 16-bit length for each packet kind using independent wire bytes. */
static void test_all_lengths(void) {
    const uint8_t kinds[] = {1U, 2U, 7U, 8U};
    for (size_t k = 0U; k < sizeof(kinds); ++k) {
        for (uint32_t length = 0U; length <= UINT16_MAX; ++length) {
            const uint8_t raw[] = {0xEFU, 1U, 0x80U, 0xFFU, 0x00U, 0x81U,
                kinds[k], (uint8_t)(length / 256U), (uint8_t)(length % 256U)};
            const uint32_t address = UINT32_C(0x80FF0081);
            as608_packet_header_t output = {0U, 0xAAU, 0xA55AU};
            unsigned char saved[sizeof(output)];
            (void)memcpy(saved, &output, sizeof(saved));

            if (length < 2U) {
                assert(as608_packet_decode_header(raw, address, SIZE_MAX, &output) ==
                       AS608_STATUS_FRAME_ERROR);
                assert(memcmp(saved, &output, sizeof(saved)) == 0);
                continue;
            }

            const size_t payload = (size_t)(length - 2U);
            if (payload != 0U) {
                assert(as608_packet_decode_header(raw, address, payload - 1U, &output) ==
                       AS608_STATUS_FRAME_ERROR);
                assert(memcmp(saved, &output, sizeof(saved)) == 0);
            }
            assert(as608_packet_decode_header(raw, address, payload, &output) == AS608_STATUS_OK);
            assert(output.address == address && output.kind == kinds[k] &&
                   output.payload_size == payload);

            uint8_t wire[11U];
            (void)memset(wire, 0xA5, sizeof(wire));
            assert(as608_packet_encode_header(&output, wire + 1U) == AS608_STATUS_OK);
            assert(memcmp(raw, wire + 1U, sizeof(raw)) == 0);
            assert(wire[0] == 0xA5U && wire[10] == 0xA5U);
            assert(as608_packet_checksum_begin(&output) ==
                   (uint16_t)(kinds[k] + length / 256U + length % 256U));
        }
    }

    /* Neither unrepresentable payload value may alter the wire buffer. */
    for (uint32_t n = 65534U; n <= UINT16_MAX; ++n) {
        const as608_packet_header_t input = {0U, 1U, (uint16_t)n};
        uint8_t wire[9U], saved[9U];
        (void)memset(wire, 0x5A, sizeof(wire));
        (void)memcpy(saved, wire, sizeof(saved));
        assert(as608_packet_encode_header(&input, wire) == AS608_STATUS_INVALID_ARG);
        assert(memcmp(wire, saved, sizeof(wire)) == 0);
    }
}

/* Check both start bytes and every individual address bit; no wildcard. */
static void test_header_mutations(void) {
    const uint8_t raw[] = {0xEFU, 1U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 7U, 0U, 3U};
    as608_packet_header_t output = {0U, 0xAAU, 0xA55AU};
    unsigned char saved[sizeof(output)];
    (void)memcpy(saved, &output, sizeof(saved));
    for (size_t byte = 0U; byte < 6U; ++byte) {
        for (unsigned int bit = 0U; bit < 8U; ++bit) {
            uint8_t wire[sizeof(raw)];
            (void)memcpy(wire, raw, sizeof(wire));
            wire[byte] ^= (uint8_t)(1U << bit);
            assert(as608_packet_decode_header(wire, UINT32_MAX, 1U, &output) ==
                   AS608_STATUS_FRAME_ERROR);
            assert(memcmp(saved, &output, sizeof(saved)) == 0);
        }
    }
}

/* Exhaust the checksum trailer domain and reject mutations of either byte. */
static void test_all_checksums(void) {
    for (uint32_t n = 0U; n <= UINT16_MAX; ++n) {
        const uint16_t sum = (uint16_t)n;
        const uint8_t raw[] = {(uint8_t)(n / 256U), (uint8_t)(n % 256U)};
        uint8_t wire[4U] = {0xA5U, 0U, 0U, 0x5AU};
        as608_packet_checksum_encode(sum, wire + 1U);
        assert(memcmp(raw, wire + 1U, sizeof(raw)) == 0);
        assert(wire[0] == 0xA5U && wire[3] == 0x5AU);
        assert(as608_packet_checksum_matches(sum, raw));
        wire[1] ^= 1U;
        assert(!as608_packet_checksum_matches(sum, wire + 1U));
        wire[1] ^= 1U;
        wire[2] ^= 1U;
        assert(!as608_packet_checksum_matches(sum, wire + 1U));
    }
}

/* Compare streamed maximum payload and every possible split of a sample
 * against an independent widened sum; no large stack allocation. */
static void test_checksum_oracle(void) {
    uint8_t data[256U];
    (void)memset(data, 0xFF, sizeof(data));
    const as608_packet_header_t h = {UINT32_MAX, 8U, 65533U};
    uint16_t sum = as608_packet_checksum_begin(&h);
    uint32_t left = 65533U;
    while (left != 0U) {
        const size_t count = left > sizeof(data) ? sizeof(data) : (size_t)left;
        sum = as608_packet_checksum_update(sum, data, count);
        left -= (uint32_t)count;
    }
    assert(sum == (uint16_t)(UINT32_C(8) + 255U + 255U + UINT32_C(65533) * 255U));

    uint32_t reference = UINT16_MAX;
    for (size_t i = 0U; i < sizeof(data); ++i) {
        data[i] = (uint8_t)(i * 37U + 13U);
        reference += data[i];
    }
    for (size_t split = 0U; split <= sizeof(data); ++split) {
        const uint16_t first = as608_packet_checksum_update(UINT16_MAX, data, split);
        const uint16_t actual = as608_packet_checksum_update(first, data + split,
                                                            sizeof(data) - split);
        assert(actual == (uint16_t)reference);
    }
}

int main(void) {
    test_packet_identifiers();
    test_get_image_vector();
    test_packet_bounds();
    test_decode_bad_frames();
    test_streaming_checksum();
    test_all_kinds();
    test_all_lengths();
    test_header_mutations();
    test_all_checksums();
    test_checksum_oracle();
    puts("as608_packet tests: PASS");
    return 0;
}
