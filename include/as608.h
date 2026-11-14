/**
 * @file as608.h
 * @brief Public API for AS608/AS60x UART fingerprint modules.
 * @details
 * ISO C11, blocking, caller-owned memory, no heap allocation, no HAL/RTOS
 * dependency. The reference chip is AS608_QCCF; the firmware's AS60x command
 * support and template interoperability must be validated per module.
 *
 * @warning Calls sharing one @ref as608_t are not thread-safe or ISR-safe.
 */
#ifndef AS608_H
#define AS608_H

#include <stddef.h>
#include <stdint.h>
#include "as608_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Factory-default AS60x packet address; not a unique serial number. */
#define AS608_DEFAULT_ADDRESS UINT32_C(0xFFFFFFFF)
/** @brief Size of a complete template in the referenced AS60x firmware. */
#define AS608_TEMPLATE_BYTES ((size_t)512U)
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
    AS608_STATUS_UNEXPECTED_REPLY /**< Unexpected packet kind or payload size. */
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

/** @brief Configuration passed to @ref as608_init. */
typedef struct {
    as608_port_t port;     /**< Caller-defined UART and clock callbacks. */
    uint32_t device_address; /**< Protocol address; often AS608_DEFAULT_ADDRESS. */
    uint32_t timeout_ms; /**< Per-command total budget: 1..AS608_MAX_TIMEOUT_MS. */
} as608_config_t;

/**
 * @brief Caller-owned device context; no dynamic allocation.
 * @warning Do not concurrently operate on, modify, or copy an active context.
 */
typedef struct {
    as608_config_t config; /**< Copy of initialization configuration. */
    uint8_t initialized;   /**< Internal initialization flag; do not modify. */
} as608_t;

/**
 * @brief Initialize the context without accessing the hardware.
 * @param[out] dev Caller-owned context.
 * @param[in] config Valid transport, address and nonzero timeout configuration.
 * @return Host-side status; no device confirmation code.
 */
as608_status_t as608_init(as608_t *dev, const as608_config_t *config);

/**
 * @brief Verify access password using PS_VfyPwd (0x13).
 * @param[in,out] dev Initialized device.
 * @param[in] password Device password (not stored in context).
 * @return Transaction status and device confirmation.
 */
as608_result_t as608_verify_password(as608_t *dev, uint32_t password);

/**
 * @brief Read parameters using PS_ReadSysPara (0x0F).
 * @param[in,out] dev Initialized device.
 * @param[out] out Decoded parameters; unchanged unless command succeeds.
 * @return Transaction status and device confirmation.
 */
as608_result_t as608_read_parameters(as608_t *dev, as608_parameters_t *out);

/**
 * @brief Count stored templates using PS_ValidTempleteNum (0x1D).
 * @param[in,out] dev Initialized device.
 * @param[out] count Occupied slot count; unchanged unless command succeeds.
 * @return Transaction status and device confirmation.
 */
as608_result_t as608_get_template_count(as608_t *dev, uint16_t *count);

/**
 * @brief Read a 32-byte occupancy bitmap from PS_ReadIndexTable (0x1F).
 * @param[in,out] dev Initialized device context.
 * @param[in] index_page Index-table page number (256 template slots per page).
 * @param[out] bitmap Destination for 32 occupancy bytes.
 * @param[in] capacity Destination capacity; must be at least 32 bytes.
 * @return Host transaction status and raw sensor confirmation.
 * @warning Availability is firmware-dependent; verify on the target AS608_QCCF.
 * @note bitmap is unchanged unless the command succeeds.
 */
as608_result_t as608_read_index_table(as608_t *dev, uint8_t index_page, uint8_t *bitmap, size_t capacity);

/**
 * @brief Read the raw 512-byte information page via PS_ReadINFpage (0x16).
 * @param[in,out] dev Initialized device.
 * @param[out] data Caller-owned destination, may be partially modified on error.
 * @param[in] capacity Available bytes in data; must be >= AS608_INFORMATION_PAGE_BYTES.
 * @return Transaction status and device confirmation.
 * @warning This page may contain sensitive settings. Do not dump it to logs.
 */
as608_result_t as608_read_information_page(as608_t *dev, uint8_t *data, size_t capacity);

/**
 * @brief Capture an image into the sensor's ImageBuffer (PS_GetImage, 0x01).
 * @param[in,out] dev Initialized device.
 * @return Transaction status and device confirmation.
 */
as608_result_t as608_capture_image(as608_t *dev);

/**
 * @brief Extract a feature file into a character buffer (PS_GenChar, 0x02).
 * @param[in,out] dev Initialized device.
 * @param[in] buffer Buffer 1 or 2.
 * @return Transaction status and device confirmation.
 */
as608_result_t as608_generate_character(as608_t *dev, as608_char_buffer_t buffer);

/**
 * @brief Compare CharBuffer1 and CharBuffer2 (PS_Match, 0x03).
 * @param[in,out] dev Initialized device.
 * @param[out] score Score; unchanged unless both result fields indicate success.
 * @return Transaction status and device confirmation.
 */
as608_result_t as608_match_buffers(as608_t *dev, uint16_t *score);

/**
 * @brief Combine the two feature buffers into a model (PS_RegModel, 0x05).
 * @param[in,out] dev Initialized device.
 * @return Transaction status and device confirmation.
 */
as608_result_t as608_create_model(as608_t *dev);

/**
 * @brief Search a template range (PS_Search, 0x04).
 * @param[in,out] dev Initialized device.
 * @param[in,out] search Search parameters and successful match output.
 * @return Transaction status and raw device confirmation (0x09 if not found).
 * @pre search->page_count != 0; search->buffer is 1 or 2.
 * @note The scanned half-open range is [start_page, start_page + page_count).
 *       Arithmetic must be widened and checked against reported capacity.
 * @note search->match is unchanged when there is no successful match.
 */
as608_result_t as608_search(as608_t *dev, as608_search_t *search);

/**
 * @brief Load a database template into CharBuffer1/2 (PS_LoadChar, 0x07).
 * @param[in,out] dev Initialized device.
 * @param[in] buffer Target character buffer.
 * @param[in] page_id Existing database position.
 * @return Transaction status and device confirmation.
 */
as608_result_t as608_load_template(as608_t *dev, as608_char_buffer_t buffer, uint16_t page_id);

/**
 * @brief Transfer an opaque 512-byte model to host memory (PS_UpChar, 0x08).
 * @param[in,out] dev Initialized device.
 * @param[in] buffer Source buffer containing a *complete* 512-byte model.
 * @param[out] data Destination buffer, may be partially modified on error.
 * @param[in] capacity Capacity of data; must be >= AS608_TEMPLATE_BYTES.
 * @return Transaction status and device confirmation.
 * @note Upload success does not guarantee cross-module template compatibility.
 */
as608_result_t as608_upload_template(as608_t *dev, as608_char_buffer_t buffer, uint8_t *data, size_t capacity);

/**
 * @brief Transfer an opaque 512-byte model to a sensor buffer (PS_DownChar, 0x09).
 * @param[in,out] dev Initialized device.
 * @param[in] buffer Destination character buffer.
 * @param[in] data Source model bytes.
 * @param[in] length Must equal AS608_TEMPLATE_BYTES.
 * @return Transaction status and device confirmation.
 * @warning ACK success does not prove the model can be matched by this module.
 */
as608_result_t as608_download_template(as608_t *dev, as608_char_buffer_t buffer, const uint8_t *data, size_t length);

/**
 * @brief Store a buffered model in flash (PS_StoreChar, 0x06).
 * @param[in,out] dev Initialized device.
 * @param[in] buffer Source character buffer.
 * @param[in] page_id Destination database position.
 * @return Transaction status and device confirmation.
 * @warning May overwrite an existing enrollment.
 */
as608_result_t as608_store_template(as608_t *dev, as608_char_buffer_t buffer, uint16_t page_id);

/**
 * @brief Read a database template into caller-owned memory.
 * @details
 * Convenience operation: PS_LoadChar (0x07), then PS_UpChar (0x08),
 * using CharBuffer1. The two commands and all data packets share one
 * overall transaction deadline.
 *
 * @param[in,out] dev Initialized device context.
 * @param[in] page_id Database slot to read (not an intrinsic template ID).
 * @param[out] data Destination for a complete, opaque template.
 * @param[in] capacity Available destination bytes.
 * @return Host status and device confirmation of the failing step,
 *         or successful completion of both steps.
 *
 * @pre capacity >= AS608_TEMPLATE_BYTES.
 * @note On success the first AS608_TEMPLATE_BYTES bytes contain the template.
 * @warning CharBuffer1 is overwritten. On failure data may be partially
 *          modified; discard it unless both result fields indicate success.
 * @note PageID is a database location, not a value to extract from the
 *       transferred template bytes.
 */
as608_result_t as608_read_template(as608_t *dev, uint16_t page_id, uint8_t *data, size_t capacity);

/**
 * @brief Import and store a template in a specified database slot.
 * @details
 * Convenience operation: PS_DownChar (0x09), then PS_StoreChar (0x06),
 * using CharBuffer1. All packets and both commands share one overall
 * transaction deadline. The caller explicitly chooses the same page_id
 * on each device when synchronizing database-slot assignments.
 *
 * @param[in,out] dev Initialized device context.
 * @param[in] page_id Destination database slot.
 * @param[in] data Complete, opaque template from a compatible module.
 * @param[in] length Template size; must equal AS608_TEMPLATE_BYTES.
 * @return Host status and device confirmation of the failing step,
 *         or successful completion of both steps.
 *
 * @pre data != NULL and length == AS608_TEMPLATE_BYTES.
 * @warning CharBuffer1 is overwritten; an occupied page_id may be replaced.
 * @warning This operation is not atomic: a failure after DownChar may leave
 *          the buffer changed, while StoreChar may fail or complete
 *          ambiguously after a transport timeout.
 * @note Transfer and store success do not prove that the template is
 *       compatible with the destination firmware's matching algorithm.
 * @note No slot allocation, cross-device synchronization or rollback
 *       is performed by this function.
 */
as608_result_t as608_write_template(as608_t *dev, uint16_t page_id, const uint8_t *data, size_t length);

/**
 * @brief Delete consecutive templates (PS_DeletChar, 0x0C).
 * @param[in,out] dev Initialized device.
 * @param[in] start_page First position to erase.
 * @param[in] page_count Number of positions, greater than zero.
 * @return Transaction status and device confirmation.
 * @warning Destructive command.
 */
as608_result_t as608_delete_templates(as608_t *dev, uint16_t start_page, uint16_t page_count);

/**
 * @brief Erase all enrolled templates (PS_Empty, 0x0D).
 * @param[in,out] dev Initialized device.
 * @return Transaction status and device confirmation.
 * @warning Irreversible destructive command.
 */
as608_result_t as608_empty_database(as608_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* AS608_H */
