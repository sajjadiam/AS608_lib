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
#include "as608_types.h"
#include "as608_transaction.h"

#ifdef __cplusplus
extern "C" {
#endif

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

/** @brief Caller-owned context; fields are private to the driver.
 * Zero-initialize before init. Do not copy or modify during operation.
 * All calls on a context are serialized by the application.
 */
typedef struct {
    as608_state_t state;
    as608_result_t result;
    uint8_t step;
    as608_transaction_t transaction;
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

/**
 * @brief Initialize a fresh caller-owned AS608 device context.
 *
 * @details Validates the transport configuration, clears the command and
 * transaction state, copies the configuration and enters AS608_STATE_IDLE.
 * Does not send a command, verify a password, reset the sensor, read the clock
 * or invoke any port callback. No dynamic allocation is performed.
 *
 * @param[in,out] ctx Device context; must point to valid writable storage.
 * @param[in] config Configuration with all four non-NULL port callbacks
 * (try_write, try_read, tx_idle, clock_ms), device address and a timeout
 * in milliseconds within [1, AS608_MAX_TIMEOUT_MS]. The configuration is
 * copied; its storage need not remain alive after return. The objects
 * referenced by port.user and callbacks must remain valid while in use.
 *
 * @retval AS608_STATUS_OK Context initialized and ready for a future job.
 * @retval AS608_STATUS_INVALID_ARG Null argument, invalid configuration,
 * or a context state other than UNINITIALIZED, BUSY or DESYNCHRONIZED.
 * @retval AS608_STATUS_BUSY Valid configuration, but a job owns the context.
 * @retval AS608_STATUS_RECOVERY_REQUIRED Valid configuration, but the link
 * is quarantined; initialization must not bypass recovery.
 *
 * @pre For a new device, zero-initialize the entire context (e.g.,
 * @code
 * static as608_t sensor = {0};
 * @endcode
 * ) and ensure a fresh physical link epoch. Calls must be serialized.
 * @post On success, ctx->state is AS608_STATE_IDLE and no job is active.
 * @post On failure, the context is unchanged.
 * @note This is local initialization only; success does not establish
 * communication with the physical sensor.
 */
as608_status_t as608_init(as608_t *ctx, const as608_config_t *config);

/**
 * @brief Advance one step of an admitted non-blocking job.
 *
 * @details If BUSY, advance the existing transaction engine at most once
 * and handle its event. No UART wait loop, sleep, automatic retry or
 * allocation occurs. The transaction engine uses the original job
 * deadline, not a new deadline per call or packet.
 *
 * @param[in,out] ctx Initialized, caller-owned device context. A NULL
 * pointer is rejected without dereferencing it.
 *
 * @retval AS608_PENDING The job remains BUSY; call again from the scheduler.
 * @retval AS608_COMPLETE A validated terminal result exists (including a
 * negative sensor ACK). Repeated calls in DONE return COMPLETE without I/O.
 * @retval AS608_FAILED Invalid/idle/uninitialized/quarantined context,
 * or a host-side communication/protocol failure.
 *
 * @pre Calls sharing the context must be serialized. For useful progress,
 * a start API must have admitted a job, and its buffers must remain valid.
 * @post A valid terminal ACK sets DONE and stores its raw confirmation
 * code. A transport/protocol fault during BUSY sets DESYNCHRONIZED and
 * records the host error; a non-BUSY context is left unchanged.
 * @note During incremental rebuild stage 02, only the simple one-byte ACK
 * phase is implemented. No public command start API is active yet.
 * Future command-specific phases will extend this same engine.
 * @see as608_get_result
 * @see as608_confirm_reset
 */
as608_progress_t as608_process(as608_t *ctx);

/**
 * @brief Copy the result of the most recent terminal job.
 *
 * @details Reads the stored result only in DONE or DESYNCHRONIZED.
 * Does not advance the job, clear its result, change state, access UART,
 * or read the clock. Repeated calls return the same result until a later
 * operation changes the context.
 *
 * @param[in] ctx Device context.
 * @param[out] result Writable destination for the result snapshot.
 * @retval true A terminal result was copied (including negative sensor
 * ACKs and host-side errors).
 * @retval false Null argument or a context outside the terminal states.
 * @pre Calls sharing the context must be serialized.
 * @post On false, the destination (if valid) is unchanged.
 * @note A true return indicates availability, not command success.
 * Check status first; confirmation is meaningful only when status is
 * AS608_STATUS_OK. A negative sensor confirmation is not a host error.
 * @see as608_process
 */
bool as608_get_result(const as608_t *ctx, as608_result_t *result);

/* Incremental rebuild: APIs 01–03 are defined.
 * The following API declarations remain contracts until restored.
 * See FEATURES.md.
 */

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

/* -------------------------------------------------------------------------
 * Planned API contracts (declarations only; NOT implemented or linkable).
 *
 * Define AS608_DECLARE_PLANNED_API before including this header to review
 * the complete proposed API. This macro only exposes declarations; it
 * does not enable code or report device support. See FEATURES.md.
 *
 * All planned start calls use the same admission/process/result lifecycle,
 * operation-wide deadline and serialized per-device context. Caller-owned
 * descriptors and referenced memory must stay valid through termination.
 * ------------------------------------------------------------------------- */
#if defined(AS608_DECLARE_PLANNED_API)

/** Documented AS60x V1.0 sizes; actual firmware support must be verified. */
#define AS608_FEATURE_256_BYTES ((size_t)256U)
#define AS608_NOTEPAD_PAGE_BYTES ((size_t)32U)
#define AS608_NOTEPAD_PAGE_COUNT 16U
#define AS608_DEVICE_DESCRIPTOR_BYTES ((size_t)8U)

/** Raw system status-register masks (ReadSysPara, manual p. 3). */
typedef enum {
    AS608_SENSOR_BUSY = 0x01,
    AS608_SENSOR_MATCH_PASSED = 0x02,
    AS608_SENSOR_PASSWORD_VERIFIED = 0x04,
    AS608_SENSOR_IMAGE_VALID = 0x08
} as608_sensor_status_flag_t;

/** Security levels for PS_WriteReg, register 5. */
typedef enum {
    AS608_SECURITY_LEVEL_1 = 1,
    AS608_SECURITY_LEVEL_2 = 2,
    AS608_SECURITY_LEVEL_3 = 3,
    AS608_SECURITY_LEVEL_4 = 4,
    AS608_SECURITY_LEVEL_5 = 5
} as608_security_level_t;

/** Wire packet-size codes for PS_WriteReg, register 6. */
typedef enum {
    AS608_PACKET_SIZE_32 = 0,
    AS608_PACKET_SIZE_64 = 1,
    AS608_PACKET_SIZE_128 = 2,
    AS608_PACKET_SIZE_256 = 3
} as608_packet_size_t;

/** PS_GenBinImage (0x1C) output mode. */
typedef enum {
    AS608_IMAGE_BINARY = 0,
    AS608_IMAGE_THINNED = 1,
    AS608_IMAGE_THINNED_WITH_MINUTIAE = 2
} as608_image_mode_t;

/** PS_BurnCode (0x1A) upgrade mode; variant-specific, destructive. */
typedef enum {
    AS608_BURN_INFORMATION_PAGE = 0,
    AS608_BURN_FULL_FIRMWARE = 1
} as608_burn_mode_t;

/** Documented GPIO selector, not an MCU pin number. */
typedef enum {
    AS608_USER_GPIO_0 = 0,
    AS608_USER_GPIO_1 = 1
} as608_user_gpio_t;

/** Ordinary 256-byte feature file, distinct from a 512/768-byte template.
 * Do not assume this size/layout on unverified firmware.
 */
typedef struct {
    uint8_t data[AS608_FEATURE_256_BYTES];
} as608_feature_file_t;

/** Notepad page request/result. Each write replaces the full page. */
typedef struct {
    uint8_t page; /**< 0..15 according to the manual; verify on target. */
    uint8_t data[AS608_NOTEPAD_PAGE_BYTES];
} as608_notepad_page_t;

/** GPIO request and echoed state from a successful reply. */
typedef struct {
    as608_user_gpio_t pin;
    bool level;        /**< Requested output level. */
    bool actual_level; /**< Echoed output state, success only. */
} as608_gpio_request_t;

/** Fixed-width descriptor bytes, NOT guaranteed NUL-terminated.
 * Parsed from the information page only after layout verification.
 * product_model is not a guaranteed unique device serial number.
 */
typedef struct {
    uint8_t product_model[AS608_DEVICE_DESCRIPTOR_BYTES];
    uint8_t software_version[AS608_DEVICE_DESCRIPTOR_BYTES];
    uint8_t manufacturer[AS608_DEVICE_DESCRIPTOR_BYTES];
    uint8_t sensor_name[AS608_DEVICE_DESCRIPTOR_BYTES];
} as608_device_info_t;

/** Non-blocking stream source: provide exactly length bytes, 0 if not ready,
 * or a negative value on failure. No partial positive return is permitted.
 * Do not advance source state on a zero return. Each request is bounded by
 * the transport payload limit; no callback may wait or re-enter the driver.
 */
typedef int32_t (*as608_stream_read_fn)(void *user, uint8_t *data,
                                       size_t length);

/** Non-blocking stream sink: consume/copy the entire validated fragment
 * immediately and return true, or return false to fail the job. No RX
 * backpressure is assumed: an unready sink is a failure, not a retry.
 * The supplied pointer is borrowed for this call only.
 */
typedef bool (*as608_stream_write_fn)(void *user, const uint8_t *data,
                                     size_t length);

typedef struct {
    void *user;
    as608_stream_read_fn read;
    size_t length; /**< Exact nonzero number of wire bytes to send. */
} as608_stream_source_t;

typedef struct {
    void *user;
    as608_stream_write_fn write;
    size_t length; /**< Exact expected nonzero wire byte count. */
} as608_stream_sink_t;

/** Firmware request. Caller must verify device, image and recovery path.
 * The stream describes the vendor-format image, not arbitrary flash data.
 */
typedef struct {
    as608_burn_mode_t mode;
    as608_stream_source_t source;
} as608_firmware_request_t;

/** Host-managed two-capture enrollment request.
 * Poll only while waiting for finger placement/removal; no implicit retry
 * of uncertain write operations. Whole workflow uses the job deadline.
 */
typedef struct {
    uint16_t page_id;
    uint32_t poll_interval_ms; /**< Nonzero scheduler poll interval. */
} as608_enroll_request_t;

/** Free-slot lookup result, committed on successful completion.
 * found == false is a valid outcome (no free slot); page_id then invalid.
 */
typedef struct {
    bool found;
    uint16_t page_id;
} as608_free_slot_t;

/* ---- Fingerprint recognition ---- */

/** PS_Enroll (0x10): firmware-managed enrollment; may write flash. */
as608_status_t as608_enroll_start(as608_t *ctx, uint16_t *page_id);

/** PS_Identify (0x11): firmware-managed acquisition/search.
 * @warning Manual describes possible updates of incomplete templates.
 */
as608_status_t as608_identify_start(as608_t *ctx, as608_match_t *match);

/** PS_HighSpeedSearch (0x1B): same request/output contract as Search. */
as608_status_t as608_high_speed_search_start(as608_t *ctx,
                                             as608_search_t *search);

/* ---- Feature and image transfer ---- */

/** PS_UpChar (0x08), ordinary 256-byte feature file (not a template). */
as608_status_t as608_upload_feature_start(as608_t *ctx,
                                         as608_char_buffer_t buffer,
                                         as608_feature_file_t *output);

/** PS_DownChar (0x09), ordinary 256-byte feature file (not a template). */
as608_status_t as608_download_feature_start(as608_t *ctx,
                                           as608_char_buffer_t buffer,
                                           const as608_feature_file_t *input);

/** PS_UpImage (0x0A): stream verified wire payload to caller's sink.
 * UART format is two 4-bit pixels per byte; no assumed image dimensions.
 * An exact byte count and END_DATA are required for success. Previously
 * delivered fragments may remain in the sink if the job later fails.
 */
as608_status_t as608_upload_image_start(as608_t *ctx,
                                       const as608_stream_sink_t *sink);

/** PS_DownImage (0x0B): stream packed wire bytes from caller's source.
 * Validate actual dimensions, size and completion behavior per firmware.
 */
as608_status_t as608_download_image_start(as608_t *ctx,
                                         const as608_stream_source_t *source);

/** PS_GenBinImage (0x1C): process ImageBuffer; upload separately. */
as608_status_t as608_generate_binary_image_start(as608_t *ctx,
                                                 as608_image_mode_t mode);

/* ---- Configuration (PS_WriteReg 0x0E, typed register setters) ---- */

/** Set register 4. baud_rate must be a supported multiple of 9600.
 * Sensor ACK uses the old configuration (manual p. 19). On success,
 * application must reconfigure its UART to the new rate before starting
 * another job. No automatic baud guessing or HAL configuration.
 */
as608_status_t as608_set_baud_rate_start(as608_t *ctx, uint32_t baud_rate);

/** Set register 5, level 1..5. */
as608_status_t as608_set_security_level_start(as608_t *ctx,
                                              as608_security_level_t level);

/** Set register 6, code 0..3. */
as608_status_t as608_set_packet_size_start(as608_t *ctx,
                                           as608_packet_size_t size);

/** PS_Port_Control (0x17): control the OTHER interface, not the active one.
 * UART controls USB; USB controls UART. Requires verified variant support.
 */
as608_status_t as608_control_other_port_start(as608_t *ctx, bool enabled);

/** Composite ReadINFpage (0x16) + descriptor decoding. */
as608_status_t as608_read_device_info_start(as608_t *ctx,
                                            as608_device_info_t *info);

/* ---- Password, address and random number ---- */

/** PS_SetPwd (0x12): change handshake password; verify separately. */
as608_status_t as608_set_password_start(as608_t *ctx, uint32_t password);

/** PS_GetRandomCode (0x14): raw random code, not a security guarantee. */
as608_status_t as608_get_random_code_start(as608_t *ctx, uint32_t *code);

/** PS_SetChipAddr (0x15): address provisioning, not arbitrary reassignment.
 * Manual p. 23 specifies a default-address command and a new-address reply.
 * Require verified firmware transition rules; commit the local address
 * only on a validated successful ACK. An uncertain transition quarantines.
 */
as608_status_t as608_set_device_address_start(as608_t *ctx, uint32_t address);

/* ---- User flash, GPIO and firmware maintenance ---- */

/** PS_WriteNotepad (0x18): write the full 32-byte page. */
as608_status_t as608_write_notepad_start(as608_t *ctx,
                                         const as608_notepad_page_t *page);

/** PS_ReadNotepad (0x19): data committed on a successful ACK only. */
as608_status_t as608_read_notepad_start(as608_t *ctx,
                                        as608_notepad_page_t *page);

/** PS_UserGPIOCommand (0x1E): reserved/variant-specific. */
as608_status_t as608_write_user_gpio_start(as608_t *ctx,
                                           as608_gpio_request_t *request);

/** PS_BurnCode (0x1A): high-risk and firmware-specific.
 * Requires separate target validation and explicit application authorization.
 * No automatic retry of programming after failure. Do not use a generic
 * image or assume the module's bootloader/recovery behavior.
 */
as608_status_t as608_burn_code_start(as608_t *ctx,
                                     const as608_firmware_request_t *request);

/* ---- Host-managed composite operations (no extra wire opcodes) ---- */

/** Capture twice with finger removal, GenChar, RegModel and StoreChar. */
as608_status_t as608_enroll_two_scan_start(as608_t *ctx,
                                          const as608_enroll_request_t *request);

/** Capture, generate and search using the requested buffer/range. */
as608_status_t as608_identify_range_start(as608_t *ctx, as608_search_t *search);

/** Read capacity and index table(s) to find a free slot; no flash writes. */
as608_status_t as608_find_free_slot_start(as608_t *ctx, as608_free_slot_t *slot);

#endif /* AS608_DECLARE_PLANNED_API */

#ifdef __cplusplus
}
#endif
#endif /* AS608_H */
