/**
 * @file test_as608_packet_header.c
 * @brief Independent C11/C++11 compile-time packet-header contract.
 */
#include "as608_packet.h"
#include "as608_packet.h"

#if defined(AS608_H) || defined(AS608_TRANSACTION_H) || defined(AS608_PORT_H)
#error "Packet header must not depend on command, transaction or port layers"
#endif

#if defined(__cplusplus)
#define CHECK(expr) static_assert((expr), #expr)
#else
#define CHECK(expr) _Static_assert((expr), #expr)
#endif

CHECK(AS608_PACKET_START_BYTE_0 == 0xEFU);
CHECK(AS608_PACKET_START_BYTE_1 == 0x01U);
CHECK(AS608_PACKET_HEADER_BYTES == 9U);
CHECK(AS608_PACKET_CHECKSUM_BYTES == 2U);
CHECK(AS608_PACKET_OVERHEAD_BYTES == 11U);
CHECK(AS608_PACKET_WIRE_PAYLOAD_MAX == 65533U);
CHECK(AS608_PACKET_WIRE_PAYLOAD_MAX + AS608_PACKET_CHECKSUM_BYTES == UINT16_MAX);
CHECK(AS608_PACKET_COMMAND == 0x01);
CHECK(AS608_PACKET_DATA == 0x02);
CHECK(AS608_PACKET_ACK == 0x07);
CHECK(AS608_PACKET_END_DATA == 0x08);
CHECK(sizeof(((as608_packet_header_t *)0)->address) == 4U);
CHECK(sizeof(((as608_packet_header_t *)0)->kind) == 1U);
CHECK(sizeof(((as608_packet_header_t *)0)->payload_size) == 2U);

#undef CHECK
