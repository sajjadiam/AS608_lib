/**
 * @file as608.h
 * @brief Public API for AS608/AS60x UART fingerprint modules.
 * @details
 * ISO C11, cooperative non-blocking, caller-owned memory, no heap allocation, no HAL/RTOS
 * dependency. The reference chip is AS608_QCCF; the firmware's AS60x command
 * support and template interoperability must be validated per module.
 *
 * @warning Calls sharing one @ref as608_t are not thread-safe or ISR-safe.
 */
#ifndef AS608_H
#define AS608_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "as608_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Factory-default AS60x packet address; not a unique serial number. */
#define AS608_DEFAULT_ADDRESS UINT32_C(0xFFFFFFFF)
/** @brief Documented AS60x template size (Communication Manual v1.0, p. 6). */
#define AS608_TEMPLATE_512_BYTES ((size_t)512U)
/** @brief Template stream size verified on the two tested AS608_QCCF modules. */
#define AS608_TEMPLATE_768_BYTES ((size_t)768U)
/** @brief Largest template size supported by this driver version. */
#define AS608_TEMPLATE_MAX_BYTES AS608_TEMPLATE_768_BYTES
/** @brief Legacy storage-allocation alias; NOT a template's actual length.
 * @deprecated Prefer AS608_TEMPLATE_MAX_BYTES and the returned buffer.length.
 */
#define AS608_TEMPLATE_BYTES AS608_TEMPLATE_MAX_BYTES
/** @brief Size of the flash information page returned by ReadINFpage. */
#define AS608_INFORMATION_PAGE_BYTES ((size_t)512U)
/** @brief Maximum whole-transaction timeout for wrap-safe uint32_t ticks. */
#define AS608_MAX_TIMEOUT_MS UINT32_C(0x7FFFFFFF)

/**
 * @brief Host-side transaction status; never contains a sensor ACK code.
 * @note AS608_STATUS_OK means a syntactically valid response was received,
 *       not necessarily that the sensor executed the command successfully.
 */
typedef enum {
    AS608_STATUS_OK = 0,          /**< A valid response was received. */
    AS608_STATUS_INVALID_ARG,     /**< Caller supplied an invalid argument. */
    AS608_STATUS_NOT_INITIALIZED, /**< Device context is not initialized. */
    AS608_STATUS_IO_ERROR,        /**< Platform transport reported an error. */
    AS608_STATUS_TIMEOUT,         /**< Overall transaction deadline expired. */
    AS608_STATUS_FRAME_ERROR,     /**< Invalid frame header, address or length. */
    AS608_STATUS_CHECKSUM_ERROR,  /**< Received frame checksum is invalid. */
    AS608_STATUS_UNEXPECTED_REPLY, /**< Unexpected packet kind or payload size. */
    AS608_STATUS_RECOVERY_REQUIRED, /**< An earlier transaction left the link uncertain. */
    AS608_STATUS_BUSY /**< Another operation owns this context. */
} as608_status_t;

/**
 * @brief AS60x device confirmation codes (wire values, not ordinal values).
 * @details From the AS60x communication manual, confirmation-code table.
 * Reserved values are deliberately not named; unrecognized codes are returned
 * unchanged as uint8_t in @ref as608_result_t.
 */
typedef enum {
    AS608_ACK_OK                  = 0x00, /**< Command completed successfully. */
    AS608_ACK_PACKET_ERROR        = 0x01, /**< Module rejected received packet. */
    AS608_ACK_NO_FINGER           = 0x02, /**< No finger detected. */
    AS608_ACK_IMAGE_ERROR         = 0x03, /**< Image acquisition failed. */
    AS608_ACK_IMAGE_DRY           = 0x04, /**< Image too dry or faint. */
    AS608_ACK_IMAGE_WET           = 0x05, /**< Image too wet or blurred. */
    AS608_ACK_IMAGE_MESSY         = 0x06, /**< Features cannot be extracted. */
    AS608_ACK_FEATURE_TOO_FEW     = 0x07, /**< Too few fingerprint features. */
    AS608_ACK_NO_MATCH            = 0x08, /**< Two features do not match. */
    AS608_ACK_NOT_FOUND           = 0x09, /**< No match in searched database. */
    AS608_ACK_MERGE_ERROR         = 0x0A, /**< Feature/template merge failed. */
    AS608_ACK_INVALID_PAGE        = 0x0B, /**< Database position out of range. */
    AS608_ACK_TEMPLATE_ERROR      = 0x0C, /**< Template load failed/invalid. */
    AS608_ACK_UPLOAD_ERROR        = 0x0D, /**< Feature/template upload failed. */
    AS608_ACK_TRANSFER_REJECTED   = 0x0E, /**< Module cannot receive data. */
    AS608_ACK_IMAGE_UPLOAD_ERROR  = 0x0F, /**< Image upload failed. */
    AS608_ACK_DELETE_ERROR        = 0x10, /**< Template deletion failed. */
    AS608_ACK_EMPTY_ERROR         = 0x11, /**< Database erase failed. */
    AS608_ACK_SLEEP_ERROR         = 0x12, /**< Low-power entry failed. */
    AS608_ACK_PASSWORD_ERROR      = 0x13, /**< Password verification failed. */
    AS608_ACK_RESET_ERROR         = 0x14, /**< System reset failed. */
    AS608_ACK_INVALID_IMAGE       = 0x15, /**< Image buffer has no valid image. */
    AS608_ACK_UPGRADE_ERROR       = 0x16, /**< Online upgrade failed. */
    AS608_ACK_RESIDUAL_FINGER     = 0x17, /**< Finger not removed between scans. */
    AS608_ACK_FLASH_ERROR         = 0x18, /**< Internal flash read/write failed. */
    AS608_ACK_UNDEFINED_ERROR     = 0x19, /**< Undefined module error. */
    AS608_ACK_INVALID_REGISTER    = 0x1A, /**< Unsupported register address. */
    AS608_ACK_INVALID_REG_VALUE   = 0x1B, /**< Invalid register configuration. */
    AS608_ACK_INVALID_NOTE_PAGE   = 0x1C, /**< Invalid notepad page. */
    AS608_ACK_PORT_ERROR          = 0x1D, /**< Communication-port operation failed. */
    AS608_ACK_ENROLL_ERROR        = 0x1E, /**< Automatic enrollment failed. */
    AS608_ACK_DATABASE_FULL       = 0x1F, /**< Template database is full. */
    AS608_ACK_DATA_ACCEPTED       = 0xF0, /**< Follow-up data command accepted. */
    AS608_ACK_COMMAND_ACCEPTED    = 0xF1, /**< Command expects following data. */
    AS608_ACK_BURN_CHECKSUM_ERROR = 0xF2, /**< Firmware burn checksum error. */
    AS608_ACK_BURN_PACKET_ERROR   = 0xF3, /**< Firmware burn packet-type error. */
    AS608_ACK_BURN_LENGTH_ERROR   = 0xF4, /**< Firmware burn packet-length error. */
    AS608_ACK_BURN_SIZE_ERROR     = 0xF5, /**< Firmware image too large. */
    AS608_ACK_BURN_FLASH_ERROR    = 0xF6  /**< Firmware flash write failed. */
} as608_confirmation_t;

/**
 * @brief Result of a sensor transaction.
 * @details If status is AS608_STATUS_OK, confirmation contains the raw, valid
 * one-byte sensor code (including unknown values). Otherwise confirmation is
 * invalid and shall be ignored. A command succeeds only if both fields are OK.
 */
typedef struct {
    as608_status_t status; /**< Host transaction status. */
    uint8_t confirmation;  /**< Valid only when status == AS608_STATUS_OK. */
} as608_result_t;

/** @brief IDs of the sensor's two character buffers. */
typedef enum {
    AS608_CHAR_BUFFER_1 = 1, /**< CharBuffer1. */
    AS608_CHAR_BUFFER_2 = 2  /**< CharBuffer2. */
} as608_char_buffer_t;

/** @brief Decoded 16-byte response to PS_ReadSysPara (0x0F). */
typedef struct {
    uint16_t status_register;    /**< System status flags. */
    uint16_t sensor_type;        /**< Connected sensor driver type, not QCCF/QYCF. */
    uint16_t template_capacity;  /**< Number of database slots. */
    uint16_t security_level;     /**< Configured matching threshold level. */
    uint32_t device_address;     /**< UART packet address. */
    uint16_t packet_size_code;   /**< Encoded transfer packet size. */
    uint16_t baud_multiplier;    /**< UART baud = multiplier * 9600. */
} as608_parameters_t;

/** @brief Successful fingerprint search output. */
typedef struct {
    uint16_t page_id; /**< Matched database position. */
    uint16_t score;   /**< Firmware-defined score; not a probability. */
} as608_match_t;

/**
 * @brief Caller-owned destination for an uploaded fingerprint template.
 * @details Set data and capacity before reading. On successful completion,
 * length is the actual number of bytes received up to END_DATA; 512 and 768
 * are accepted. On any failure, length is unchanged and the data buffer may
 * contain partial data.
 */
typedef struct {
    uint8_t *data;   /**< [in] Writable template storage. */
    size_t capacity; /**< [in] Available storage bytes (at least 512). */
    size_t length;   /**< [out] Actual template bytes, success only. */
} as608_template_buffer_t;

/**
 * @brief Search input and output in a single caller-owned object.
 * @details Set buffer, start_page and page_count before calling the function.
 * match is modified only if both result fields indicate AS608_STATUS_OK and
 * AS608_ACK_OK; otherwise match is left unchanged.
 */
typedef struct {
    as608_char_buffer_t buffer; /**< [in] CharBuffer1 or CharBuffer2. */
    uint16_t start_page;        /**< [in] First page, inclusive. */
    uint16_t page_count;        /**< [in] Number of pages to scan; must be nonzero. */
    as608_match_t match;        /**< [out] Valid only following successful match. */
} as608_search_t;

/** @brief Transport and timeout configuration, copied by as608_init(). */
typedef struct {
    as608_port_t port;
    uint32_t device_address;
    uint32_t timeout_ms;
} as608_config_t;

typedef enum {
    AS608_STATE_UNINITIALIZED = 0,
    AS608_STATE_IDLE,
    AS608_STATE_BUSY,
    AS608_STATE_DONE,
    AS608_STATE_DESYNCHRONIZED
} as608_state_t;

typedef enum {
    AS608_PENDING = 0,
    AS608_COMPLETE, /**< A validated sensor response or completed data transfer. */
    AS608_FAILED    /**< Host-side error; inspect the result. */
} as608_progress_t;

/** @brief Internal transport storage; owned by the enclosing device. */
typedef struct {
    uint32_t started_ms;
    uint8_t wire_phase;
    uint8_t tx_frame[267U];
    size_t tx_len;
    size_t tx_pos;
    uint8_t rx_header[9U];
    size_t rx_header_pos;
    uint8_t rx_body[258U];
    size_t rx_body_len;
    size_t rx_body_pos;
    uint8_t rx_kind;
    size_t rx_payload_len;
    size_t rx_limit;
} as608_transaction_storage_t;

/** @brief Caller-owned context; fields are private to the driver.
 * Zero-initialize before init. Do not copy or modify during operation.
 * All calls on a context are serialized by the application.
 */
typedef struct {
    as608_config_t config;
    as608_state_t state;
    as608_result_t result;
    uint8_t step;
    as608_transaction_storage_t transaction;
    uint16_t page_id;
    uint16_t chunk_size;
    as608_char_buffer_t download_buffer;
    bool store_after_download;
    const uint8_t *input;
    size_t input_length;
    size_t input_offset;
    uint8_t *stream_data;
    size_t stream_capacity;
    size_t stream_offset;
    size_t *stream_length;
    void *reply_output;
} as608_t;

/** @brief Initialize a zeroed context without accessing UART. */
as608_status_t as608_init(as608_t *ctx, const as608_config_t *config);

/** @brief Advance one bounded transport operation; never wait for UART.
 * @note Call only for a started job, in a serialized scheduler.
 */
as608_progress_t as608_process(as608_t *ctx);

/** @brief Copy the terminal result; false before a job terminates.
 * COMPLETE does not imply ACK_OK. Check both status and confirmation.
 */
bool as608_get_result(const as608_t *ctx, as608_result_t *result);

/** @brief Abort an in-flight job and quarantine its link. */
as608_status_t as608_abort(as608_t *ctx);

/** @brief Acknowledge externally completed physical recovery.
 * @pre The application has stopped old TX, physically reset/power-cycled
 * the sensor, waited for boot, and purged all old RX/TX bytes and errors.
 * This function performs no reset, I/O, delays or verification itself.
 * Calling without satisfying that precondition is unsafe.
 */
as608_status_t as608_confirm_reset(as608_t *ctx);

/**
 * @brief Start functions: return admission status, never wait for a reply.
 * @details The single operation-wide timeout begins on successful start.
 * Keep all pointed-to inputs (immutable) and output storage valid until
 * completion or abort. Output scalars and template length change only on
 * successful ACK/stream; stream bytes may be partially modified on failure.
 * Poll as608_process(), then use as608_get_result(). No automatic retries.
 */
as608_status_t as608_verify_password_start(as608_t *ctx, uint32_t password);
as608_status_t as608_read_parameters_start(as608_t *ctx, as608_parameters_t *out);
as608_status_t as608_get_template_count_start(as608_t *ctx, uint16_t *count);
as608_status_t as608_read_index_table_start(as608_t *ctx, uint8_t index_page,
                                         uint8_t *bitmap, size_t capacity);
as608_status_t as608_read_information_page_start(as608_t *ctx, uint8_t *data,
                                               size_t capacity);
as608_status_t as608_capture_image_start(as608_t *ctx);
as608_status_t as608_generate_character_start(as608_t *ctx,
                                             as608_char_buffer_t buffer);
as608_status_t as608_match_buffers_start(as608_t *ctx, uint16_t *score);
as608_status_t as608_create_model_start(as608_t *ctx);
as608_status_t as608_search_start(as608_t *ctx, as608_search_t *search);
as608_status_t as608_load_template_start(as608_t *ctx, as608_char_buffer_t buffer,
                                       uint16_t page_id);
as608_status_t as608_upload_template_start(as608_t *ctx, as608_char_buffer_t buffer,
                                         as608_template_buffer_t *output);
as608_status_t as608_download_template_start(as608_t *ctx, as608_char_buffer_t buffer,
                                           const uint8_t *data, size_t length);
as608_status_t as608_store_template_start(as608_t *ctx, as608_char_buffer_t buffer,
                                        uint16_t page_id);
as608_status_t as608_read_template_start(as608_t *ctx, uint16_t page_id,
                                       as608_template_buffer_t *output);
as608_status_t as608_write_template_start(as608_t *ctx, uint16_t page_id,
                                        const uint8_t *data, size_t length);
as608_status_t as608_delete_templates_start(as608_t *ctx, uint16_t start_page,
                                          uint16_t page_count);
as608_status_t as608_empty_database_start(as608_t *ctx);

#ifdef __cplusplus
}
#endif
#endif /* AS608_H */
