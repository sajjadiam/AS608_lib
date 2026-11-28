/** @file test_as608_types.c @brief Independent compile-time common-type contract. */
#include "as608_types.h"
#include "as608_types.h"

#if defined(__cplusplus)
#define CHECK(expr) static_assert((expr), #expr)
#else
#define CHECK(expr) _Static_assert((expr), #expr)
#endif

CHECK(CHAR_BIT == 8);
CHECK(sizeof(uint8_t) == 1U);
CHECK(sizeof(uint16_t) == 2U);
CHECK(sizeof(uint32_t) == 4U);
CHECK(sizeof(int32_t) == 4U);
CHECK(AS608_DEFAULT_ADDRESS == UINT32_MAX);
CHECK(AS608_MAX_TIMEOUT_MS == UINT32_MAX / 2U);
CHECK((uint32_t)(UINT32_C(50) - (UINT32_MAX - UINT32_C(49))) == UINT32_C(100));
CHECK(AS608_STATUS_OK == 0);
CHECK(AS608_STATUS_INVALID_ARG == 1);
CHECK(AS608_STATUS_NOT_INITIALIZED == 2);
CHECK(AS608_STATUS_IO_ERROR == 3);
CHECK(AS608_STATUS_TIMEOUT == 4);
CHECK(AS608_STATUS_FRAME_ERROR == 5);
CHECK(AS608_STATUS_CHECKSUM_ERROR == 6);
CHECK(AS608_STATUS_UNEXPECTED_REPLY == 7);
CHECK(AS608_STATUS_RECOVERY_REQUIRED == 8);
CHECK(AS608_STATUS_BUSY == 9);

#undef CHECK
