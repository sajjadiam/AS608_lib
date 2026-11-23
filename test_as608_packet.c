#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "as608_packet.h"

static void test_packet_identifiers(void)
{
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

static void test_get_image_vector(void)
{
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

static void test_packet_bounds(void)
{
    as608_packet_header_t h = { UINT32_C(0x12345678), AS608_PACKET_END_DATA, 65533U };
    uint8_t wire[AS608_PACKET_HEADER_BYTES] = { 0 };
    assert(as608_packet_encode_header(&h, wire) == AS608_STATUS_OK);
    const uint8_t want[] = { 0xEF, 0x01, 0x12, 0x34, 0x56, 0x78, 0x08, 0xFF, 0xFF };
    assert(memcmp(wire, want, sizeof(want)) == 0);

    as608_packet_header_t parsed = { UINT32_C(0xDEADBEEF), 0xFA, 0xBABE };
    const as608_packet_header_t saved = parsed;
    assert(as608_packet_decode_header(wire, h.address, 65532U, &parsed) == AS608_STATUS_FRAME_ERROR);
    assert(memcmp(&saved, &parsed, sizeof(parsed)) == 0);
    assert(as608_packet_decode_header(wire, h.address, 65533U, &parsed) == AS608_STATUS_OK);
    assert(parsed.payload_size == 65533U);

    h.payload_size = 65534U;
    memset(wire, 0xAA, sizeof(wire));
    assert(as608_packet_encode_header(&h, wire) == AS608_STATUS_INVALID_ARG);
    for (size_t i=0; i<sizeof(wire); i++) assert(wire[i] == 0xAA);

    h.payload_size = 1U;
    h.kind = 0x03;
    assert(as608_packet_encode_header(&h, wire) == AS608_STATUS_INVALID_ARG);
    for (size_t i=0; i<sizeof(wire); i++) assert(wire[i] == 0xAA);
}

static void test_decode_bad_frames(void)
{
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

static void test_streaming_checksum(void)
{
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

int main(void)
{
    test_packet_identifiers();
    test_get_image_vector();
    test_packet_bounds();
    test_decode_bad_frames();
    test_streaming_checksum();
    puts("as608_packet tests: PASS");
    return 0;
}
