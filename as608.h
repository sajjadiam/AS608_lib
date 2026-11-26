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

/** @brief Documented AS60x ordinary feature file size (MCU User's Guide V1.0, section 2.6). */
#define AS608_FEATURE_256_BYTES ((size_t)256U)
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

/** Ordinary 256-byte feature file, distinct from a 512/768-byte template.
 * Do not assume this size/layout on unverified firmware.
 */
typedef struct {
    uint8_t data[AS608_FEATURE_256_BYTES];
} as608_feature_file_t;

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
 * @note At rebuild stage 28, simple, system-parameter, 16-bit result,
 * index-table and search ACK phases plus information-page and template
 * upload/download streaming are active.
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

/**
 * @brief Logically abort an admitted job and quarantine its link.
 *
 * @details Only a BUSY job can be aborted. This function does not send
 * a cancellation command, cancel queued UART bytes, stop DMA/interrupts,
 * flush RX, reset the sensor, read the clock or wait for I/O.
 * Previously accepted TX bytes remain owned by the port. The sensor may
 * still execute the command and may still send a reply.
 *
 * @param[in,out] ctx Caller-owned device context.
 * @retval AS608_STATUS_OK The BUSY job was logically aborted.
 * @retval AS608_STATUS_RECOVERY_REQUIRED The context is already quarantined.
 * @retval AS608_STATUS_INVALID_ARG NULL context or a state other than
 * BUSY or DESYNCHRONIZED.
 * @pre Calls sharing the context must be serialized.
 * @post On success, the state is DESYNCHRONIZED and the stored result
 * status is AS608_STATUS_RECOVERY_REQUIRED; confirmation is invalid.
 * Subsequent as608_process() calls return AS608_FAILED without I/O.
 * The application may release the aborted job's caller-owned buffers.
 * @post On failure, the context is unchanged.
 * @warning Abort does not roll back sensor-side effects, including flash
 * writes. External physical recovery is required before further commands.
 * @see as608_get_result
 * @see as608_confirm_reset
 */
as608_status_t as608_abort(as608_t *ctx);

/**
 * @brief Release a quarantined context after external physical recovery.
 *
 * @details Preserves the configured port, device address and timeout.
 * Clears all command/transaction state, pending data references and the
 * previous result, then returns the context to IDLE. Does not perform
 * a physical reset, cancel TX, flush a UART queue, read the clock, wait
 * for boot, verify communication or authenticate the sensor.
 *
 * @param[in,out] ctx Caller-owned device context.
 * @retval AS608_STATUS_OK Recovery acknowledged; context returned to IDLE.
 * @retval AS608_STATUS_NOT_INITIALIZED Context is UNINITIALIZED.
 * @retval AS608_STATUS_BUSY A job is still active.
 * @retval AS608_STATUS_INVALID_ARG Null pointer or any other state
 * except DESYNCHRONIZED.
 *
 * @pre Calls sharing the context must be serialized. Before calling on a
 * DESYNCHRONIZED context, the application must stop/cancel old TX,
 * physically reset or power-cycle the sensor, wait for boot, and purge
 * all stale RX/TX bytes and UART errors. The port and sensor must have a
 * fresh communication epoch and compatible retained configuration.
 * @post On success, no previous result is available and a new job may
 * be admitted. The application must authenticate again if required.
 * @post On failure, the context is unchanged.
 * @warning The function trusts the caller's physical recovery guarantee.
 * It cannot establish that guarantee itself or undo earlier sensor-side
 * effects (including uncertain flash writes).
 * @see as608_abort
 * @see as608_get_result
 */
as608_status_t as608_confirm_reset(as608_t *ctx);



/* =========================================================================
 * Command APIs — common start contract
 * =========================================================================
 * Start returns admission status, never waits for a reply. A single job
 * deadline begins on successful admission. Keep pointed-to input immutable
 * and output storage valid until completion or abort. Output scalars and
 * lengths change only on successful ACK/stream; stream bytes may be partially
 * modified on failure. Poll as608_process(), then call as608_get_result().
 * No automatic retries.
 */
/**
 * @brief Start password verification (PS_VfyPwd, opcode 0x13).
 *
 * @details Encodes the 32-bit password most-significant byte first and
 * queues a COMMAND frame in the transaction context. No UART callback
 * is invoked here; the clock is sampled once on successful admission.
 * The sensor reply is a one-byte confirmation in a validated ACK frame.
 * Advance the job with as608_process() and read its terminal result
 * with as608_get_result().
 *
 * @param[in,out] ctx Initialized caller-owned context.
 * @param[in] password Device handshake password; all uint32_t values
 * are representable, including the factory-default value zero.
 * @retval AS608_STATUS_OK Job admitted, not password verified.
 * @retval AS608_STATUS_INVALID_ARG Null context.
 * @retval AS608_STATUS_NOT_INITIALIZED Context is not initialized.
 * @retval AS608_STATUS_BUSY Another job owns the context.
 * @retval AS608_STATUS_RECOVERY_REQUIRED Link is quarantined.
 * @pre Calls sharing the context must be serialized.
 * @post On successful admission, state is BUSY and the original
 * operation-wide deadline is armed. On admission rejection, the
 * previous job/result is unchanged.
 * @note On completion, AS608_STATUS_OK with AS608_ACK_OK means the
 * sensor accepted the password. AS608_ACK_PASSWORD_ERROR (0x13)
 * means rejection, not a transport failure.
 * @warning The wire password is plaintext; do not log or expose TX
 * buffers. Verification is not cryptographic authentication.
 * @see as608_process
 * @see as608_get_result
 * @see AS60x Communication Manual V1.0, printed page 22.
 */
as608_status_t as608_verify_password_start(as608_t *ctx, uint32_t password);

/**
 * @brief Start reading the system parameters (PS_ReadSysPara, opcode 0x0F).
 *
 * @details Queues a command with no arguments. A successful ACK contains
 * one confirmation byte and 16 bytes of big-endian parameters. All seven
 * fields are decoded explicitly, without casting the wire buffer to a
 * structure. The reported address, packet-size code and baud multiplier
 * are returned as read; the host's transport configuration is not changed.
 *
 * @param[in,out] ctx Initialized caller-owned device context.
 * @param[out] out Writable caller-owned parameter structure.
 * @retval AS608_STATUS_OK Job admitted; output is not yet available.
 * @retval AS608_STATUS_INVALID_ARG Null context or output.
 * @retval AS608_STATUS_NOT_INITIALIZED Context is not initialized.
 * @retval AS608_STATUS_BUSY Another job owns the context.
 * @retval AS608_STATUS_RECOVERY_REQUIRED Link is quarantined.
 * @pre Serialize calls on ctx. Keep out valid and separate from driver
 * storage until the job terminates or is aborted.
 * @post On successful admission the state is BUSY. The output is written
 * only after a complete, checksum-verified ACK_OK of the expected size.
 * On rejection, negative ACK, protocol error, timeout or abort, out is
 * unchanged.
 * @note Poll as608_process() and check both result fields before using out.
 * A negative ACK is a terminal sensor result, not a host transport error.
 * @see as608_process
 * @see as608_get_result
 * @see AS60x Communication Manual V1.0, printed pages 19–20.
 */
as608_status_t as608_read_parameters_start(as608_t *ctx, as608_parameters_t *out);

/**
 * @brief Start reading the number of valid stored templates (opcode 0x1D).
 *
 * @details PS_ValidTempleteNum takes no command arguments. A successful
 * ACK contains one confirmation byte and a 16-bit big-endian count.
 * The result is the number of enrolled templates, not database capacity
 * or a free PageID. Zero is a valid successful count.
 *
 * @param[in,out] ctx Initialized caller-owned device context.
 * @param[out] count Writable caller-owned uint16_t result.
 * @retval AS608_STATUS_OK Job admitted; count is not yet available.
 * @retval AS608_STATUS_INVALID_ARG Null context or output.
 * @retval AS608_STATUS_NOT_INITIALIZED Context is not initialized.
 * @retval AS608_STATUS_BUSY Another job owns the context.
 * @retval AS608_STATUS_RECOVERY_REQUIRED Link is quarantined.
 * @pre Serialize calls on ctx. Keep count valid and separate from driver
 * storage until the job terminates or is aborted.
 * @post On successful admission the state is BUSY. Count is written
 * only after a complete checksum-verified ACK_OK with a 3-byte payload.
 * On rejection, negative ACK, protocol error, timeout or abort, count
 * remains unchanged.
 * @note Poll as608_process() and check both result fields before using count.
 * @see as608_process
 * @see as608_get_result
 * @see AS60x Communication Manual V1.0, printed pages 26–27.
 */
as608_status_t as608_get_template_count_start(as608_t *ctx, uint16_t *count);

/**
 * @brief Start reading one template index-table page (PS_ReadIndexTable, 0x1F).
 *
 * @details Each page contains 32 bytes (256 occupancy bits). Page n
 * represents template slots 256*n through 256*n+255, for n in [0, 3].
 * A set bit denotes an occupied slot; a cleared bit denotes an unoccupied
 * slot. The bitmap is returned verbatim; the manual does not establish
 * within-byte bit ordering. Slots beyond the actual database capacity
 * must not be treated as available storage.
 *
 * @param[in,out] ctx Initialized caller-owned device context.
 * @param[in] index_page Index-table page, 0 through 3 inclusive.
 * @param[out] bitmap Writable caller-owned byte buffer.
 * @param[in] capacity Available bytes in bitmap; at least 32 required.
 * @retval AS608_STATUS_OK Job admitted; bitmap not yet available.
 * @retval AS608_STATUS_INVALID_ARG Null context or bitmap, page out of
 * range, or insufficient capacity.
 * @retval AS608_STATUS_NOT_INITIALIZED Context is not initialized.
 * @retval AS608_STATUS_BUSY Another job owns the context.
 * @retval AS608_STATUS_RECOVERY_REQUIRED Link is quarantined.
 * @pre Serialize calls on ctx. Keep bitmap valid and separate from
 * driver storage until the job terminates or is aborted.
 * @post On successful admission, state is BUSY. Exactly 32 bytes are
 * written only after a complete checksum-verified ACK_OK with a 33-byte
 * payload. Extra buffer bytes remain unchanged. On rejection, negative
 * ACK, timeout, protocol error or abort, bitmap remains unchanged.
 * @note The AS60x V1.0 manual marks this command reserved. Host protocol
 * support does not imply that a particular sensor firmware supports it.
 * @see as608_process
 * @see as608_get_result
 * @see AS60x Communication Manual V1.0, printed pages 27–28.
 */
as608_status_t as608_read_index_table_start(as608_t *ctx, uint8_t index_page, uint8_t *bitmap, size_t capacity);

/**
 * @brief Start reading the 512-byte flash information page (PS_ReadINFpage, 0x16).
 *
 * @details Sends a command without arguments. A validated ACK_OK starts
 * a sequence of checksum-verified DATA packets ending in END_DATA.
 * Completion requires exactly AS608_INFORMATION_PAGE_BYTES bytes and
 * the final packet marker. A single job deadline covers ACK and stream.
 * Returns the page verbatim; does not interpret firmware-specific fields.
 *
 * @param[in,out] ctx Initialized caller-owned device context.
 * @param[out] data Writable caller-owned byte buffer.
 * @param[in] capacity Available bytes; at least AS608_INFORMATION_PAGE_BYTES.
 * @retval AS608_STATUS_OK Job admitted; page not yet available.
 * @retval AS608_STATUS_INVALID_ARG Null context or data, insufficient capacity.
 * @retval AS608_STATUS_NOT_INITIALIZED Context is not initialized.
 * @retval AS608_STATUS_BUSY Another job owns the context.
 * @retval AS608_STATUS_RECOVERY_REQUIRED Link is quarantined.
 * @pre Serialize calls on ctx. Keep data valid and separate from driver
 * storage until the job terminates or is aborted.
 * @post On successful admission, state is BUSY. On successful terminal
 * result, exactly 512 bytes have been written. Extra buffer bytes are
 * untouched. Admission rejection and negative initial ACK leave data
 * unchanged. Later faults or abort may leave previously verified fragments
 * in data; do not consume a partial page as a successful result.
 * @note Check both result fields after polling as608_process(). No retries,
 * additional allocation, or whole-page staging buffer is used.
 * @see as608_process
 * @see as608_get_result
 * @see AS60x Communication Manual V1.0, printed page 23.
 */
as608_status_t as608_read_information_page_start(as608_t *ctx, uint8_t *data, size_t capacity);

/**
 * @brief Start fingerprint image acquisition (PS_GetImage, opcode 0x01).
 *
 * @details Requests one sensor-side finger detection and image capture.
 * On success, the image resides in the sensor's internal ImageBuffer;
 * no image bytes are transferred to the host. The command expects a
 * one-byte confirmation ACK.
 *
 * @param[in,out] ctx Initialized caller-owned device context.
 * @retval AS608_STATUS_OK Job admitted; capture is not yet confirmed.
 * @retval AS608_STATUS_INVALID_ARG Null context.
 * @retval AS608_STATUS_NOT_INITIALIZED Context is not initialized.
 * @retval AS608_STATUS_BUSY Another job owns the context.
 * @retval AS608_STATUS_RECOVERY_REQUIRED Link is quarantined.
 * @pre Calls sharing ctx must be serialized.
 * @post On admission, state is BUSY and the job deadline starts.
 * Rejected admission leaves the current job and result unchanged.
 * @note After completion, AS608_STATUS_OK with AS608_ACK_OK means the
 * sensor captured an image. AS608_ACK_NO_FINGER (0x02) and
 * AS608_ACK_IMAGE_ERROR (0x03) are sensor results, not host faults.
 * No automatic finger polling, retries or image upload are performed.
 * Do not use ImageBuffer as a newly captured image after a failed capture.
 * @see as608_process
 * @see as608_get_result
 * @see AS60x Communication Manual V1.0, printed page 13.
 */
as608_status_t as608_capture_image_start(as608_t *ctx);

/**
 * @brief Start fingerprint feature extraction (PS_GenChar, opcode 0x02).
 *
 * @details Converts the image in the sensor's internal ImageBuffer into
 * a fingerprint feature file in the selected CharBuffer. No image or
 * feature bytes are transferred to the host. The reply is a one-byte ACK.
 *
 * @param[in,out] ctx Initialized caller-owned device context.
 * @param[in] buffer Destination AS608_CHAR_BUFFER_1 or AS608_CHAR_BUFFER_2.
 * @retval AS608_STATUS_OK Job admitted; feature extraction not yet confirmed.
 * @retval AS608_STATUS_INVALID_ARG Null context or unsupported buffer ID.
 * @retval AS608_STATUS_NOT_INITIALIZED Context is not initialized.
 * @retval AS608_STATUS_BUSY Another job owns the context.
 * @retval AS608_STATUS_RECOVERY_REQUIRED Link is quarantined.
 * @pre Serialize calls sharing ctx. A valid image must already exist in
 * ImageBuffer for successful extraction (normally after PS_GetImage).
 * The driver does not track or enforce sensor-side image validity.
 * @post On admission, state is BUSY and the job deadline starts.
 * Rejected admission leaves the context and previous result unchanged.
 * @note Check both terminal result fields. Confirmation 0x00 means
 * successful extraction; 0x01 is a sensor packet error, 0x06 indicates
 * poor image quality, 0x07 insufficient features, and 0x15 invalid image.
 * A negative ACK is not a host transport fault. Do not rely on the
 * destination buffer containing fresh features after a failed operation.
 * The manual maps other buffer IDs to CharBuffer2; this library deliberately
 * rejects them to prevent accidental writes to the wrong buffer.
 * @see as608_capture_image_start
 * @see as608_process
 * @see as608_get_result
 * @see AS60x Communication Manual V1.0, printed pages 13–14.
 */
as608_status_t as608_generate_character_start(as608_t *ctx, as608_char_buffer_t buffer);

/**
 * @brief Start a one-to-one feature comparison (PS_Match, opcode 0x03).
 *
 * @details Compares the sensor's CharBuffer1 and CharBuffer2 without
 * searching the flash database. The successful ACK contains a
 * confirmation byte and a 16-bit big-endian match score. The raw score
 * is sensor-specific; it is not a probability or percentage.
 *
 * @param[in,out] ctx Initialized caller-owned device context.
 * @param[out] score Writable caller-owned uint16_t result.
 * @retval AS608_STATUS_OK Job admitted; comparison not yet confirmed.
 * @retval AS608_STATUS_INVALID_ARG Null context or score.
 * @retval AS608_STATUS_NOT_INITIALIZED Context is not initialized.
 * @retval AS608_STATUS_BUSY Another job owns the context.
 * @retval AS608_STATUS_RECOVERY_REQUIRED Link is quarantined.
 * @pre Serialize calls sharing ctx. Keep score valid and separate from
 * driver storage until completion or abort. Both character buffers
 * must contain valid comparable features for successful matching;
 * the driver does not track their sensor-side validity.
 * @post On admission, state is BUSY and the job deadline starts.
 * Score is written only after a checksum-verified ACK_OK with a
 * 3-byte payload. Rejection, negative ACK, timeout, protocol error
 * and abort leave score unchanged.
 * @note Check both terminal result fields. AS608_ACK_NO_MATCH (0x08)
 * is a valid terminal sensor result, not a host communication fault.
 * The score field in a negative ACK is deliberately ignored, even
 * when present. AS608_ACK_PACKET_ERROR (0x01) is also a sensor result.
 * @see as608_generate_character_start
 * @see as608_process
 * @see as608_get_result
 * @see AS60x Communication Manual V1.0, printed page 14.
 */
as608_status_t as608_match_buffers_start(as608_t *ctx, uint16_t *score);

/**
 * @brief Start merging two fingerprint features into a model (PS_RegModel, 0x05).
 *
 * @details Requests sensor-side merging of the features in CharBuffer1
 * and CharBuffer2. On success, the generated model is stored in both
 * character buffers. The command takes no arguments and returns a
 * one-byte confirmation ACK. It does not write the flash database.
 *
 * @param[in,out] ctx Initialized caller-owned device context.
 * @retval AS608_STATUS_OK Job admitted; model creation not yet confirmed.
 * @retval AS608_STATUS_INVALID_ARG Null context.
 * @retval AS608_STATUS_NOT_INITIALIZED Context is not initialized.
 * @retval AS608_STATUS_BUSY Another job owns the context.
 * @retval AS608_STATUS_RECOVERY_REQUIRED Link is quarantined.
 * @pre Calls sharing ctx must be serialized. Both character buffers
 * must contain valid features from the same finger for successful merging.
 * The driver does not track sensor-side buffer validity or finger identity.
 * @post On admission, state is BUSY and the job deadline starts.
 * Rejected admission leaves the context and previous result unchanged.
 * @note Check both terminal result fields. AS608_ACK_OK (0x00) means
 * merging succeeded, AS608_ACK_PACKET_ERROR (0x01) indicates a sensor
 * packet error, and AS608_ACK_MERGE_ERROR (0x0A) indicates merge failure.
 * A negative ACK is a terminal sensor result, not a host transport fault.
 * Do not assume either buffer contains a newly generated model after failure.
 * No implicit capture, match, retry or flash storage is performed.
 * @see as608_generate_character_start
 * @see as608_store_template_start
 * @see as608_process
 * @see as608_get_result
 * @see AS60x Communication Manual V1.0, printed page 15.
 */
as608_status_t as608_create_model_start(as608_t *ctx);

/**
 * @brief Start a one-to-many fingerprint database search (PS_Search, 0x04).
 *
 * @details Searches the specified range of flash template slots using
 * the feature in the selected CharBuffer. The command encodes BufferID,
 * StartPage and PageNum; the latter two are 16-bit big-endian fields.
 * A successful ACK contains the matched PageID and raw 16-bit score.
 *
 * @param[in,out] ctx Initialized caller-owned device context.
 * @param[in,out] search Caller-owned search request and match output.
 * @retval AS608_STATUS_OK Job admitted; search is not yet confirmed.
 * @retval AS608_STATUS_INVALID_ARG Null context or search, invalid
 * buffer ID, zero page_count, or range exceeding the 16-bit PageID space.
 * @retval AS608_STATUS_NOT_INITIALIZED Context is not initialized.
 * @retval AS608_STATUS_BUSY Another job owns the context.
 * @retval AS608_STATUS_RECOVERY_REQUIRED Link is quarantined.
 * @pre Serialize calls sharing ctx. Keep search valid and separate from
 * driver storage, and its input fields unchanged, until completion or
 * abort. The selected CharBuffer must contain valid features.
 * The application must choose a range within actual sensor capacity;
 * the driver checks the 16-bit address space, not physical capacity.
 * @post On admission, state is BUSY and the job deadline starts.
 * Only a checksum-verified ACK_OK of length 5 with a PageID in the
 * requested range changes search->match. Admission rejection,
 * negative ACK, protocol fault, timeout or abort leave it unchanged.
 * @note AS608_ACK_NOT_FOUND (0x09) is a valid terminal sensor result.
 * The manual specifies zero PageID and score for this result; the
 * library intentionally preserves the previous output instead.
 * Always check both result fields before consuming match.
 * @see as608_generate_character_start
 * @see as608_read_parameters_start
 * @see as608_process
 * @see as608_get_result
 * @see AS60x Communication Manual V1.0, printed page 14.
 */
as608_status_t as608_search_start(as608_t *ctx, as608_search_t *search);

/**
 * @brief Start loading a flash template (PS_LoadChar, opcode 0x07).
 *
 * @details Reads the template at PageID into the selected internal
 * CharBuffer. Encodes BufferID and a 16-bit big-endian PageID. The
 * response is a one-byte confirmation ACK; no template data is
 * transferred to the host and no flash write is requested.
 *
 * @param[in,out] ctx Initialized caller-owned device context.
 * @param[in] buffer Destination AS608_CHAR_BUFFER_1 or AS608_CHAR_BUFFER_2.
 * @param[in] page_id Flash database slot to load.
 * @retval AS608_STATUS_OK Job admitted; load is not yet confirmed.
 * @retval AS608_STATUS_INVALID_ARG Null context or unsupported buffer ID.
 * @retval AS608_STATUS_NOT_INITIALIZED Context is not initialized.
 * @retval AS608_STATUS_BUSY Another job owns the context.
 * @retval AS608_STATUS_RECOVERY_REQUIRED Link is quarantined.
 * @pre Serialize calls sharing ctx. Choose a PageID within the actual
 * sensor capacity. The driver does not cache or enforce that capacity;
 * the sensor reports an invalid page or template.
 * @post On admission, state is BUSY and the job deadline starts.
 * Rejected admission leaves the context and previous result unchanged.
 * @note Check both terminal result fields. AS608_ACK_OK means the
 * selected buffer now contains the loaded template. AS608_ACK_PACKET_ERROR
 * (0x01), AS608_ACK_INVALID_PAGE (0x0B) and AS608_ACK_TEMPLATE_ERROR
 * (0x0C) are sensor results, not host transport faults. After failure,
 * do not assume the destination buffer contains the requested template.
 * No automatic upload or retry is performed.
 * @see as608_read_parameters_start
 * @see as608_upload_template_start
 * @see as608_process
 * @see as608_get_result
 * @see AS60x Communication Manual V1.0, printed pages 15–16.
 */
as608_status_t as608_load_template_start(as608_t *ctx, as608_char_buffer_t buffer, uint16_t page_id);

/**
 * @brief Start uploading a template from a CharBuffer (PS_UpChar, 0x08).
 *
 * @details ACK_OK begins a sequence of DATA packets terminated by END_DATA.
 * This API accepts only complete 512-byte (documented) or 768-byte
 * (observed on tested AS608_QCCF modules) templates, not the 256-byte
 * ordinary feature file described by the manual. The single job deadline
 * covers ACK and all data packets. No flash load or automatic retry occurs.
 *
 * @param[in,out] ctx Initialized caller-owned device context.
 * @param[in] buffer Source AS608_CHAR_BUFFER_1 or AS608_CHAR_BUFFER_2.
 * @param[in,out] output Destination descriptor; set data and capacity.
 * @retval AS608_STATUS_OK Job admitted; template not yet available.
 * @retval AS608_STATUS_INVALID_ARG Null context, invalid buffer ID, null
 * output or data pointer, or capacity below 512 bytes.
 * @retval AS608_STATUS_NOT_INITIALIZED Context is not initialized.
 * @retval AS608_STATUS_BUSY Another job owns the context.
 * @retval AS608_STATUS_RECOVERY_REQUIRED Link is quarantined.
 * @pre Serialize calls sharing ctx. The selected CharBuffer must contain
 * a template. Keep output and its data storage valid, nonoverlapping,
 * separate from driver storage and unchanged by the caller until terminal
 * completion or abort. Provide capacity >= 768 for either supported size.
 * @post On admission, state is BUSY. Verified fragments are copied to
 * data; length is committed only on a valid END_DATA at a supported size.
 * The effective receive limit is 512 or 768, whichever is the largest
 * supported size not exceeding capacity. Bytes beyond that limit are
 * untouched. On rejection or negative initial ACK, data and length are
 * unchanged. On later fault or abort, length is unchanged but data may
 * contain partial verified fragments.
 * @note Check both terminal result fields before using the template.
 * Firmware compatibility and actual template length must be verified.
 * @see as608_load_template_start
 * @see as608_process
 * @see as608_get_result
 * @see AS60x Communication Manual V1.0, printed pages 6 and 16.
 */
as608_status_t as608_upload_template_start(as608_t *ctx, as608_char_buffer_t buffer, as608_template_buffer_t *output);

/**
 * @brief Start downloading a template to a CharBuffer (PS_DownChar, 0x09).
 *
 * @details Reads system parameters (0x0F), validates the packet-size
 * code (0 through 3: 32, 64, 128, 256 bytes), then sends DownChar.
 * A verified readiness ACK_OK permits DATA and END_DATA transmission.
 * One job deadline covers all phases and physical TX drain.
 * Supports 512-byte documented and 768-byte observed templates.
 * No automatic retry or flash store is performed.
 *
 * @param[in,out] ctx Initialized caller-owned device context.
 * @param[in] buffer AS608_CHAR_BUFFER_1 or AS608_CHAR_BUFFER_2.
 * @param[in] data Immutable caller-owned template bytes.
 * @param[in] length Exactly 512 or 768 bytes.
 * @retval AS608_STATUS_OK Job admitted, transfer not yet complete.
 * @retval AS608_STATUS_INVALID_ARG Null context/data, invalid buffer
 * ID or unsupported template length.
 * @retval AS608_STATUS_NOT_INITIALIZED Context is not initialized.
 * @retval AS608_STATUS_BUSY Another job owns the context.
 * @retval AS608_STATUS_RECOVERY_REQUIRED Link is quarantined.
 * @pre Serialize calls sharing ctx. Keep data valid, immutable and
 * separate from driver storage through completion or abort.
 * Verify the target firmware accepts the template format.
 * @post State becomes BUSY on admission. Negative parameter or
 * readiness ACK terminates without sending template data. Fault or
 * abort can leave partial or uncertain sensor contents.
 * @warning Success means a positive readiness ACK and physical UART
 * drain (tx_idle), NOT verified acceptance of the full template.
 * The command description (pp. 16–17) specifies a readiness ACK,
 * but the example flow (p. 31) shows another ACK after END_DATA.
 * This implementation retains the historical no-final-ACK profile.
 * Verify that the target sends no trailing ACK before issuing another
 * command; a trailing ACK can desynchronize the link.
 * @note On drained success confirmation represents the readiness ACK.
 * @see as608_process
 * @see as608_get_result
 * @see AS60x Communication Manual V1.0, printed pages 16–17, 19 and 31.
 */
as608_status_t as608_download_template_start(as608_t *ctx, as608_char_buffer_t buffer, const uint8_t *data, size_t length);

/**
 * @brief Start storing a template in flash (PS_StoreChar, opcode 0x06).
 *
 * @details Stores the template from the selected internal CharBuffer
 * at the specified database PageID. The command contains BufferID and
 * a 16-bit big-endian PageID and expects a one-byte confirmation ACK.
 * No host template bytes are transferred.
 *
 * @param[in,out] ctx Initialized caller-owned device context.
 * @param[in] buffer Source AS608_CHAR_BUFFER_1 or AS608_CHAR_BUFFER_2.
 * @param[in] page_id Destination flash database slot.
 * @retval AS608_STATUS_OK Job admitted; storage not yet confirmed.
 * @retval AS608_STATUS_INVALID_ARG Null context or unsupported buffer ID.
 * @retval AS608_STATUS_NOT_INITIALIZED Context is not initialized.
 * @retval AS608_STATUS_BUSY Another job owns the context.
 * @retval AS608_STATUS_RECOVERY_REQUIRED Link is quarantined.
 * @pre Serialize calls sharing ctx. The selected CharBuffer must contain
 * a valid template. The application must choose an authorized destination
 * slot within actual sensor capacity and handle existing contents.
 * The driver neither checks slot occupancy nor caches database capacity.
 * If the template was downloaded, verify the target's DownChar behavior
 * before storing; physical TX drain alone does not confirm acceptance.
 * @post On admission, state is BUSY and the job deadline starts.
 * Rejected admission leaves the context and previous result unchanged.
 * @warning This is a flash-writing command. Timeout, abort or transport
 * failure may leave the destination slot modified or its contents uncertain.
 * No rollback, readback, implicit overwrite check or automatic retry occurs.
 * @note Check both terminal result fields. ACK_OK (0x00) confirms the
 * sensor reports successful storage. Packet error (0x01), invalid PageID
 * (0x0B) and flash write error (0x18) are terminal sensor results, not
 * host transport faults. A negative ACK does not prove flash is unchanged.
 * @see as608_create_model_start
 * @see as608_download_template_start
 * @see as608_process
 * @see as608_get_result
 * @see AS60x Communication Manual V1.0, printed page 15.
 */
as608_status_t as608_store_template_start(as608_t *ctx, as608_char_buffer_t buffer, uint16_t page_id);

/**
 * @brief Start reading a flash template into host memory.
 *
 * @details Composite host job: PS_LoadChar (0x07) loads PageID into
 * CharBuffer1, then PS_UpChar (0x08) uploads it after a verified ACK_OK.
 * One deadline covers both commands and the entire DATA/END_DATA stream.
 * No flash write, implicit retry, or additional full-template buffer.
 *
 * @param[in,out] ctx Initialized caller-owned device context.
 * @param[in] page_id Flash database slot to read.
 * @param[in,out] output Caller-owned descriptor; set data and capacity.
 * @retval AS608_STATUS_OK Job admitted; template not yet available.
 * @retval AS608_STATUS_INVALID_ARG Null context, output or data,
 * or capacity below 512 bytes.
 * @retval AS608_STATUS_NOT_INITIALIZED Context is not initialized.
 * @retval AS608_STATUS_BUSY Another job owns the context.
 * @retval AS608_STATUS_RECOVERY_REQUIRED Link is quarantined.
 * @pre Serialize calls sharing ctx. Choose a PageID within actual
 * database capacity. Keep output and its data valid, nonoverlapping,
 * separate from driver storage and unchanged by the caller until
 * completion or abort. Capacity >= 768 accepts both supported sizes.
 * @post State becomes BUSY on admission. CharBuffer1 is overwritten
 * by a successful load. A negative LoadChar ACK prevents UpChar.
 * A negative ACK in either command leaves output data and length
 * unchanged. Verified fragments are copied directly; length is committed
 * only on valid END_DATA totaling 512 or 768 bytes within capacity.
 * Later faults or abort leave length unchanged but may leave partial data.
 * @note Check both terminal result fields before using the template.
 * A valid negative ACK is a terminal sensor result, not a host fault.
 * This is a host-side composition, not a separate sensor opcode.
 * @see as608_load_template_start
 * @see as608_upload_template_start
 * @see as608_process
 * @see as608_get_result
 * @see AS60x Communication Manual V1.0, printed pages 15–16.
 */
as608_status_t as608_read_template_start(as608_t *ctx, uint16_t page_id, as608_template_buffer_t *output);

/**
 * @brief Start writing a host template into the sensor flash database.
 *
 * @details Composite host job: ReadSysPara (0x0F) determines packet
 * size, DownChar (0x09) transfers the template into CharBuffer1,
 * and StoreChar (0x06) writes it to PageID after physical TX drain.
 * One deadline covers all commands, fragments, drain and final ACK.
 * Accepts 512-byte documented or 768-byte observed templates.
 *
 * @param[in,out] ctx Initialized caller-owned device context.
 * @param[in] page_id Destination flash database slot.
 * @param[in] data Immutable caller-owned template bytes.
 * @param[in] length Exactly 512 or 768 bytes.
 * @retval AS608_STATUS_OK Job admitted; flash write not yet confirmed.
 * @retval AS608_STATUS_INVALID_ARG Null context/data or unsupported length.
 * @retval AS608_STATUS_NOT_INITIALIZED Context is not initialized.
 * @retval AS608_STATUS_BUSY Another job owns the context.
 * @retval AS608_STATUS_RECOVERY_REQUIRED Link is quarantined.
 * @pre Serialize calls sharing ctx. Keep data valid, immutable and
 * separate from driver storage until completion or abort. Choose an
 * authorized destination within actual capacity and handle occupancy.
 * Verify template compatibility and the no-final-ACK DownChar profile.
 * @post On admission, state is BUSY. Negative parameter or readiness
 * ACK terminates before data or flash storage. StoreChar is scheduled
 * only after successful download TX drain. Final ACK_OK reports storage
 * success on the supported firmware profile; no readback is performed.
 * @warning DownChar final ACK behavior differs between the manual's
 * detailed description (pp. 16–17) and example (p. 31). On firmware
 * sending a trailing ACK, it could be mistaken for the StoreChar ACK.
 * Do not use this composite job until no-final-ACK behavior is verified.
 * Timeout, abort or I/O fault can leave CharBuffer1 or flash uncertain.
 * No automatic retry, rollback, occupancy check or readback verification.
 * @note Check both terminal result fields. Negative ACKs are terminal
 * sensor results, not host faults. This is not an independent opcode.
 * @see as608_download_template_start
 * @see as608_store_template_start
 * @see as608_process
 * @see as608_get_result
 * @see AS60x Communication Manual V1.0, printed pages 15–17, 19–20, 31.
 */
as608_status_t as608_write_template_start(as608_t *ctx, uint16_t page_id, const uint8_t *data, size_t length);

/**
 * @brief Start deleting a contiguous range of flash templates (PS_DeletChar, 0x0C).
 *
 * @details Encodes the starting PageID and number of slots as two
 * 16-bit big-endian fields. Expects a one-byte confirmation ACK.
 *
 * @param[in,out] ctx Initialized caller-owned device context.
 * @param[in] start_page First database slot to delete.
 * @param[in] page_count Number of consecutive slots, not the last PageID.
 * @retval AS608_STATUS_OK Job admitted; deletion not yet confirmed.
 * @retval AS608_STATUS_INVALID_ARG Null context, zero count, or range
 * exceeding the 16-bit PageID space.
 * @retval AS608_STATUS_NOT_INITIALIZED Context is not initialized.
 * @retval AS608_STATUS_BUSY Another job owns the context.
 * @retval AS608_STATUS_RECOVERY_REQUIRED Link is quarantined.
 * @pre Serialize calls sharing ctx. Authorize deletion and ensure the
 * requested range fits actual database capacity. The driver checks the
 * representable PageID range only, not physical capacity or occupancy.
 * @post On admission, state is BUSY and the job deadline starts.
 * Rejected admission leaves the context and previous result unchanged.
 * @warning This is a destructive flash operation. Timeout, abort, or
 * a failed reply can leave database contents uncertain. No automatic
 * retry, rollback, occupancy check, or verification is performed.
 * @note Check both terminal result fields. ACK_OK (0x00) reports
 * successful deletion, 0x01 a packet error, and 0x10 deletion failure.
 * A negative ACK is a terminal sensor result, not a host transport fault,
 * and does not guarantee the database is unchanged.
 * @see as608_read_parameters_start
 * @see as608_read_index_table_start
 * @see as608_process
 * @see as608_get_result
 * @see AS60x Communication Manual V1.0, printed page 18.
 */
as608_status_t as608_delete_templates_start(as608_t *ctx, uint16_t start_page, uint16_t page_count);

/**
 * @brief Start deleting all flash templates (PS_Empty, opcode 0x0D).
 *
 * @details Sends the argument-free database-clear command and expects
 * a one-byte confirmation ACK. This is not a factory reset, secure erase,
 * or a command to change system configuration.
 *
 * @param[in,out] ctx Initialized caller-owned device context.
 * @retval AS608_STATUS_OK Job admitted; deletion not yet confirmed.
 * @retval AS608_STATUS_INVALID_ARG Null context.
 * @retval AS608_STATUS_NOT_INITIALIZED Context is not initialized.
 * @retval AS608_STATUS_BUSY Another job owns the context.
 * @retval AS608_STATUS_RECOVERY_REQUIRED Link is quarantined.
 * @pre Serialize calls sharing ctx. The application must explicitly
 * authorize removal of all templates on the selected sensor.
 * @post On admission, state is BUSY and the original job deadline starts.
 * Rejected admission leaves the context and previous result unchanged.
 * @warning Destructive flash operation. Timeout, abort, negative ACK
 * or transport error may leave database contents uncertain.
 * No automatic retry, rollback or post-erase verification is performed.
 * @note Check both terminal result fields. ACK_OK (0x00) reports
 * successful clearing, 0x01 a packet error, and 0x11 clearing failure.
 * A negative ACK is a terminal sensor result, not a host transport fault.
 * @see as608_delete_templates_start
 * @see as608_get_template_count_start
 * @see as608_process
 * @see as608_get_result
 * @see AS60x Communication Manual V1.0, printed pages 18–19.
 */
as608_status_t as608_empty_database_start(as608_t *ctx);

/**
 * @brief Start sensor-managed single-scan enrollment (opcode 0x10).
 *
 * @details PS_Enroll takes no command arguments. The sensor captures a
 * fingerprint, selects a free database slot and stores the template in
 * flash. A successful ACK contains a confirmation byte and a 16-bit
 * big-endian PageID. This is not the host-managed two-scan workflow.
 *
 * @param[in,out] ctx Initialized caller-owned device context.
 * @param[out] page_id Writable caller-owned uint16_t result.
 * @retval AS608_STATUS_OK Job admitted; PageID is not yet available.
 * @retval AS608_STATUS_INVALID_ARG Null context or output.
 * @retval AS608_STATUS_NOT_INITIALIZED Context is not initialized.
 * @retval AS608_STATUS_BUSY Another job owns the context.
 * @retval AS608_STATUS_RECOVERY_REQUIRED Link is quarantined.
 * @pre Serialize calls on ctx. Keep page_id valid and separate from
 * driver storage until the job terminates or is aborted.
 * @post PageID is written only after a checksum-verified ACK_OK with
 * a 3-byte payload. On rejection, negative ACK, timeout, protocol error
 * or abort, page_id remains unchanged.
 * @warning Enrollment writes flash. Timeout, abort or communication
 * failure may leave an enrolled template at an unknown PageID.
 * No automatic retry or rollback is performed.
 * @note Firmware support must be verified on the target. Poll
 * as608_process() and check both result fields before using PageID.
 * @see as608_process
 * @see as608_get_result
 * @see AS60x Communication Manual V1.0, printed pages 20–21.
 */
as608_status_t as608_enroll_start(as608_t *ctx, uint16_t *page_id);

/**
 * @brief Start sensor-managed fingerprint identification (opcode 0x11).
 *
 * @details PS_Identify takes no command arguments. The sensor acquires
 * a fingerprint and searches its database. A successful ACK contains
 * a confirmation byte, a 16-bit big-endian PageID and a 16-bit
 * big-endian MatchScore. No host-selected search range is used.
 *
 * @param[in,out] ctx Initialized caller-owned device context.
 * @param[out] match Writable caller-owned match result.
 * @retval AS608_STATUS_OK Job admitted; match is not yet available.
 * @retval AS608_STATUS_INVALID_ARG Null context or output.
 * @retval AS608_STATUS_NOT_INITIALIZED Context is not initialized.
 * @retval AS608_STATUS_BUSY Another job owns the context.
 * @retval AS608_STATUS_RECOVERY_REQUIRED Link is quarantined.
 * @pre Serialize calls on ctx. Keep match valid and separate from
 * driver storage until the job terminates or is aborted.
 * @post Match is written only after a checksum-verified ACK_OK with
 * a 5-byte payload. On rejection, negative ACK, timeout, protocol
 * error or abort, match remains unchanged.
 * @warning The firmware may update an incomplete stored template when
 * the score exceeds its upper threshold. Timeout or abort does not
 * undo sensor-side changes. No automatic retry is performed.
 * @note Firmware support must be verified on the target. Poll
 * as608_process() and check both result fields before using match.
 * ACK_NOT_FOUND (0x09) is a terminal sensor result, not a host fault.
 * @see as608_process
 * @see as608_get_result
 * @see AS60x Communication Manual V1.0, printed page 21.
 */
as608_status_t as608_identify_start(as608_t *ctx, as608_match_t *match);

/**
 * @brief Start a high-speed fingerprint database search (opcode 0x1B).
 *
 * @details PS_HighSpeedSearch uses the same BufferID, StartPage, PageNum
 * and match ACK fields as PS_Search. The search algorithm runs inside
 * the sensor; the driver makes no latency or accuracy guarantee.
 *
 * @param[in,out] ctx Initialized caller-owned device context.
 * @param[in,out] search Caller-owned search request and match output.
 * @retval AS608_STATUS_OK Job admitted; search is not yet confirmed.
 * @retval AS608_STATUS_INVALID_ARG Null context or search, invalid
 * buffer ID, zero page_count, or range exceeding the 16-bit PageID space.
 * @retval AS608_STATUS_NOT_INITIALIZED Context is not initialized.
 * @retval AS608_STATUS_BUSY Another job owns the context.
 * @retval AS608_STATUS_RECOVERY_REQUIRED Link is quarantined.
 * @pre Serialize calls sharing ctx. Keep search valid and separate from
 * driver storage, and its input fields unchanged, until completion or
 * abort. The selected CharBuffer must contain valid features. Select a
 * range within the actual sensor capacity.
 * @post Only a checksum-verified ACK_OK of length 5 with a PageID in
 * the requested range changes search->match. Admission rejection,
 * negative ACK, protocol fault, timeout or abort leave it unchanged.
 * @note Firmware support must be verified on the target. ACK_NOT_FOUND
 * is a terminal sensor result. Check both result fields before using match.
 * @see as608_search_start
 * @see as608_process
 * @see as608_get_result
 * @see AS60x MCU User's Guide V1.0, printed page 30.
 */
as608_status_t as608_high_speed_search_start(as608_t *ctx, as608_search_t *search);

/**
 * @brief Start uploading an ordinary 256-byte feature file (opcode 0x08).
 *
 * @details PS_UpChar selects a CharBuffer, not a file type. The caller
 * must ensure that it contains an ordinary feature file, not a template.
 * A validated readiness ACK_OK is followed by DATA/END_DATA packets.
 * Success requires exactly 256 payload bytes and a valid END_DATA.
 *
 * @param[in,out] ctx Initialized caller-owned device context.
 * @param[in] buffer AS608_CHAR_BUFFER_1 or AS608_CHAR_BUFFER_2.
 * @param[out] output Writable caller-owned feature file.
 * @retval AS608_STATUS_OK Job admitted; feature is not yet available.
 * @retval AS608_STATUS_INVALID_ARG Null context or output, invalid buffer.
 * @retval AS608_STATUS_NOT_INITIALIZED Context is not initialized.
 * @retval AS608_STATUS_BUSY Another job owns the context.
 * @retval AS608_STATUS_RECOVERY_REQUIRED Link is quarantined.
 * @pre Serialize calls on ctx. Keep output valid and separate from
 * driver storage until termination or abort.
 * @post Validated fragments are copied directly into output->data.
 * Rejected admission or negative readiness ACK leaves output unchanged.
 * After a later fault or abort, output may contain a partial prefix;
 * use it only after both terminal result fields indicate success.
 * @note Firmware support and the actual wire size must be verified on
 * the target. A 512/768-byte template is not accepted by this API.
 * @see as608_generate_character_start
 * @see as608_upload_template_start
 * @see as608_process
 * @see as608_get_result
 * @see AS60x MCU User's Guide V1.0, sections 2.6 and PS_UpChar.
 */
as608_status_t as608_upload_feature_start(as608_t *ctx, as608_char_buffer_t buffer, as608_feature_file_t *output);

/**
 * @brief Start downloading an ordinary 256-byte feature file (opcode 0x09).
 *
 * @details Reads system parameters to select the 32/64/128/256-byte
 * fragment size, sends PS_DownChar and waits for a valid readiness ACK.
 * Sends exactly 256 bytes with END_DATA on the last fragment. One job
 * deadline covers all phases and physical TX drain. No flash store occurs.
 *
 * @param[in,out] ctx Initialized caller-owned device context.
 * @param[in] buffer AS608_CHAR_BUFFER_1 or AS608_CHAR_BUFFER_2.
 * @param[in] input Immutable caller-owned 256-byte feature file.
 * @retval AS608_STATUS_OK Job admitted; transfer is not yet complete.
 * @retval AS608_STATUS_INVALID_ARG Null context or input, invalid buffer.
 * @retval AS608_STATUS_NOT_INITIALIZED Context is not initialized.
 * @retval AS608_STATUS_BUSY Another job owns the context.
 * @retval AS608_STATUS_RECOVERY_REQUIRED Link is quarantined.
 * @pre Serialize calls on ctx. Keep input valid, immutable and separate
 * from driver storage through termination or abort. Verify the feature
 * format and 256-byte transfer support on the target firmware.
 * @post A negative parameter or readiness ACK prevents feature data
 * transmission. Abort or fault can leave partial sensor buffer contents.
 * @warning Completion means readiness ACK_OK and physical TX drain,
 * not verified sensor acceptance of the entire feature. This API uses
 * the existing no-final-ACK DownChar profile. The manual's example
 * flow shows an additional ACK; verify its absence on the target
 * before issuing another command. No automatic retry is performed.
 * @note On drained success, confirmation represents the readiness ACK.
 * @see as608_download_template_start
 * @see as608_process
 * @see as608_get_result
 * @see AS60x MCU User's Guide V1.0, section 2.6 and PS_DownChar.
 */
as608_status_t as608_download_feature_start(as608_t *ctx, as608_char_buffer_t buffer, const as608_feature_file_t *input);

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

/* ---- Feature and image transfer ---- */

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
