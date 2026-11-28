/** @file test_as608_transaction_header.c @brief Standalone transaction header contract. */
#include "as608_transaction.h"
#include "as608_transaction.h"

#ifdef AS608_H
#error "Transaction must not depend on the public command header"
#endif

#if defined(__cplusplus)
#include <type_traits>
#define CHECK(expr) static_assert((expr), #expr)
#define CHECK_TYPE(expr, type) static_assert(std::is_same<decltype(expr), type>::value, #expr)
#else
#define CHECK(expr) _Static_assert((expr), #expr)
#define CHECK_TYPE(expr, type) _Static_assert(_Generic((expr), type: 1, default: 0), #expr)
#endif

CHECK(AS608_TRANSACTION_MAX_PAYLOAD_BYTES == 256U);
CHECK(AS608_TRANSACTION_MAX_PAYLOAD_BYTES <= AS608_PACKET_WIRE_PAYLOAD_MAX);
CHECK(sizeof(((as608_transaction_t *)0)->tx_frame) == 267U);
CHECK(sizeof(((as608_transaction_t *)0)->rx_header) == 9U);
CHECK(sizeof(((as608_transaction_t *)0)->rx_body) == 258U);
CHECK(AS608_TXN_PENDING == 0);
CHECK(AS608_TXN_SENT == 1);
CHECK(AS608_TXN_RECEIVED == 2);
CHECK(AS608_TXN_DRAINED == 3);
CHECK(AS608_TXN_FAULT == 4);

typedef bool (*config_fn)(const as608_config_t *);
typedef void (*init_fn)(as608_transaction_t *, const as608_config_t *);
typedef void (*phase_fn)(as608_transaction_t *);
typedef uint8_t *(*tx_payload_fn)(as608_transaction_t *);
typedef as608_status_t (*send_fn)(as608_transaction_t *, const as608_transaction_frame_t *);
typedef void (*receive_fn)(as608_transaction_t *, size_t);
typedef void (*receive_at_fn)(as608_transaction_t *, size_t, uint32_t);
typedef as608_transaction_result_t (*process_fn)(as608_transaction_t *);
typedef as608_transaction_frame_t (*received_fn)(const as608_transaction_t *);

CHECK_TYPE(&as608_transaction_config_valid, config_fn);
CHECK_TYPE(&as608_transaction_init, init_fn);
CHECK_TYPE(&as608_transaction_begin, phase_fn);
CHECK_TYPE(&as608_transaction_tx_payload, tx_payload_fn);
CHECK_TYPE(&as608_transaction_send, send_fn);
CHECK_TYPE(&as608_transaction_receive, receive_fn);
CHECK_TYPE(&as608_transaction_receive_at, receive_at_fn);
CHECK_TYPE(&as608_transaction_drain, phase_fn);
CHECK_TYPE(&as608_transaction_process, process_fn);
CHECK_TYPE(&as608_transaction_received, received_fn);
CHECK_TYPE(((as608_transaction_frame_t *)0)->data, const uint8_t *);

#undef CHECK
#undef CHECK_TYPE
