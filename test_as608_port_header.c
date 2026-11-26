/** @file test_as608_port_header.c @brief Standalone port ABI/signature checks. */
#include "as608_port.h"
#include "as608_port.h"

#if defined(AS608_H) || defined(AS608_PACKET_H) || defined(AS608_TRANSACTION_H)
#error "Port header must not depend on higher driver layers"
#endif

typedef int32_t (*expected_write_t)(void *, const uint8_t *, size_t);
typedef int32_t (*expected_read_t)(void *, uint8_t *, size_t);
typedef int32_t (*expected_idle_t)(void *);
typedef uint32_t (*expected_clock_t)(void *);

#if defined(__cplusplus)
#include <type_traits>
#define CHECK_TYPE(expr, type) static_assert(std::is_same<decltype(expr), type>::value, #expr)
#else
#define CHECK_TYPE(expr, type) _Static_assert(_Generic((expr), type: 1, default: 0), #expr)
#endif

CHECK_TYPE(((as608_port_t *)0)->user, void *);
CHECK_TYPE(((as608_port_t *)0)->try_write, expected_write_t);
CHECK_TYPE(((as608_port_t *)0)->try_read, expected_read_t);
CHECK_TYPE(((as608_port_t *)0)->tx_idle, expected_idle_t);
CHECK_TYPE(((as608_port_t *)0)->clock_ms, expected_clock_t);

#undef CHECK_TYPE
