/** @file test_as608_header.c @brief Public API signature and C/C++ link contract. */
#include "as608.h"
#include "as608.h"

#if defined(__cplusplus)
#include <type_traits>
#define CHECK(expr) static_assert((expr), #expr)
#define CHECK_TYPE(expr, type) static_assert(std::is_same<decltype(expr), type>::value, #expr)
#else
#define CHECK(expr) _Static_assert((expr), #expr)
#define CHECK_TYPE(expr, type) _Static_assert(_Generic((expr), type: 1, default: 0), #expr)
#endif

CHECK(AS608_FEATURE_256_BYTES == 256U);
CHECK(AS608_TEMPLATE_512_BYTES == 512U);
CHECK(AS608_TEMPLATE_768_BYTES == 768U);
CHECK(AS608_TEMPLATE_MAX_BYTES == 768U);
CHECK(AS608_TEMPLATE_BYTES == AS608_TEMPLATE_MAX_BYTES);
CHECK(AS608_INFORMATION_PAGE_BYTES == 512U);
CHECK(AS608_DEVICE_DESCRIPTOR_BYTES == 8U);
CHECK(AS608_NOTEPAD_PAGE_BYTES == 32U);
CHECK(AS608_NOTEPAD_PAGE_COUNT == 16U);
CHECK(AS608_STATE_UNINITIALIZED == 0);
CHECK(AS608_STATE_IDLE == 1);
CHECK(AS608_STATE_BUSY == 2);
CHECK(AS608_STATE_DONE == 3);
CHECK(AS608_STATE_DESYNCHRONIZED == 4);
CHECK(AS608_STATE_DONE_RECOVERY_REQUIRED == 5);
CHECK(AS608_PENDING == 0);
CHECK(AS608_COMPLETE == 1);
CHECK(AS608_FAILED == 2);
CHECK(sizeof(((as608_result_t *)0)->confirmation) == 1U);
CHECK(sizeof(((as608_feature_file_t *)0)->data) == 256U);
CHECK(sizeof(((as608_notepad_page_t *)0)->data) == 32U);
CHECK(sizeof(((as608_device_info_t *)0)->product_model) == 8U);
CHECK(sizeof(((as608_device_info_t *)0)->software_version) == 8U);
CHECK(sizeof(((as608_device_info_t *)0)->manufacturer) == 8U);
CHECK(sizeof(((as608_device_info_t *)0)->sensor_name) == 8U);
CHECK(sizeof(((as608_t *)0)->device_info_data) == 32U);

CHECK(AS608_ACK_OK == 0x00);
CHECK(AS608_ACK_PACKET_ERROR == 0x01);
CHECK(AS608_ACK_NO_FINGER == 0x02);
CHECK(AS608_ACK_IMAGE_ERROR == 0x03);
CHECK(AS608_ACK_IMAGE_DRY == 0x04);
CHECK(AS608_ACK_IMAGE_WET == 0x05);
CHECK(AS608_ACK_IMAGE_MESSY == 0x06);
CHECK(AS608_ACK_FEATURE_TOO_FEW == 0x07);
CHECK(AS608_ACK_NO_MATCH == 0x08);
CHECK(AS608_ACK_NOT_FOUND == 0x09);
CHECK(AS608_ACK_MERGE_ERROR == 0x0A);
CHECK(AS608_ACK_INVALID_PAGE == 0x0B);
CHECK(AS608_ACK_TEMPLATE_ERROR == 0x0C);
CHECK(AS608_ACK_UPLOAD_ERROR == 0x0D);
CHECK(AS608_ACK_TRANSFER_REJECTED == 0x0E);
CHECK(AS608_ACK_IMAGE_UPLOAD_ERROR == 0x0F);
CHECK(AS608_ACK_DELETE_ERROR == 0x10);
CHECK(AS608_ACK_EMPTY_ERROR == 0x11);
CHECK(AS608_ACK_SLEEP_ERROR == 0x12);
CHECK(AS608_ACK_PASSWORD_ERROR == 0x13);
CHECK(AS608_ACK_RESET_ERROR == 0x14);
CHECK(AS608_ACK_INVALID_IMAGE == 0x15);
CHECK(AS608_ACK_UPGRADE_ERROR == 0x16);
CHECK(AS608_ACK_RESIDUAL_FINGER == 0x17);
CHECK(AS608_ACK_FLASH_ERROR == 0x18);
CHECK(AS608_ACK_UNDEFINED_ERROR == 0x19);
CHECK(AS608_ACK_INVALID_REGISTER == 0x1A);
CHECK(AS608_ACK_INVALID_REG_VALUE == 0x1B);
CHECK(AS608_ACK_INVALID_NOTE_PAGE == 0x1C);
CHECK(AS608_ACK_PORT_ERROR == 0x1D);
CHECK(AS608_ACK_ENROLL_ERROR == 0x1E);
CHECK(AS608_ACK_DATABASE_FULL == 0x1F);
CHECK(AS608_ACK_DATA_ACCEPTED == 0xF0);
CHECK(AS608_ACK_COMMAND_ACCEPTED == 0xF1);
CHECK(AS608_ACK_BURN_CHECKSUM_ERROR == 0xF2);
CHECK(AS608_ACK_BURN_PACKET_ERROR == 0xF3);
CHECK(AS608_ACK_BURN_LENGTH_ERROR == 0xF4);
CHECK(AS608_ACK_BURN_SIZE_ERROR == 0xF5);
CHECK(AS608_ACK_BURN_FLASH_ERROR == 0xF6);
CHECK(AS608_CHAR_BUFFER_1 == 1);
CHECK(AS608_CHAR_BUFFER_2 == 2);
CHECK(AS608_IMAGE_BINARY == 0);
CHECK(AS608_IMAGE_THINNED == 1);
CHECK(AS608_IMAGE_THINNED_WITH_MINUTIAE == 2);
CHECK(AS608_SECURITY_LEVEL_1 == 1);
CHECK(AS608_SECURITY_LEVEL_2 == 2);
CHECK(AS608_SECURITY_LEVEL_3 == 3);
CHECK(AS608_SECURITY_LEVEL_4 == 4);
CHECK(AS608_SECURITY_LEVEL_5 == 5);
CHECK(AS608_PACKET_SIZE_32 == 0);
CHECK(AS608_PACKET_SIZE_64 == 1);
CHECK(AS608_PACKET_SIZE_128 == 2);
CHECK(AS608_PACKET_SIZE_256 == 3);
CHECK(AS608_SENSOR_BUSY == 0x01);
CHECK(AS608_SENSOR_MATCH_PASSED == 0x02);
CHECK(AS608_SENSOR_PASSWORD_VERIFIED == 0x04);
CHECK(AS608_SENSOR_IMAGE_VALID == 0x08);
CHECK(AS608_USER_GPIO_0 == 0);
CHECK(AS608_USER_GPIO_1 == 1);
CHECK(AS608_BURN_INFORMATION_PAGE == 0);
CHECK(AS608_BURN_FULL_FIRMWARE == 1);
CHECK(AS608_BURN_PROFILE_DISABLED == 0);
CHECK(AS608_BURN_PROFILE_ACK_EACH == 1);

typedef bool (*sink_fn)(void *, const uint8_t *, size_t);
typedef int32_t (*source_fn)(void *, uint8_t *, size_t);
CHECK_TYPE(((as608_stream_sink_t *)0)->write, sink_fn);
CHECK_TYPE(((as608_stream_source_t *)0)->read, source_fn);

/* Freeze the signatures independently of future header changes. */
typedef as608_status_t (*api_01_fn)(as608_t *ctx, const as608_config_t *config);
CHECK_TYPE(&as608_init, api_01_fn);
typedef as608_progress_t (*api_02_fn)(as608_t *ctx);
CHECK_TYPE(&as608_process, api_02_fn);
typedef bool (*api_03_fn)(const as608_t *ctx, as608_result_t *result);
CHECK_TYPE(&as608_get_result, api_03_fn);
typedef as608_status_t (*api_04_fn)(as608_t *ctx);
CHECK_TYPE(&as608_abort, api_04_fn);
typedef as608_status_t (*api_05_fn)(as608_t *ctx);
CHECK_TYPE(&as608_confirm_reset, api_05_fn);
typedef as608_status_t (*api_06_fn)(as608_t *ctx, uint32_t password);
CHECK_TYPE(&as608_verify_password_start, api_06_fn);
typedef as608_status_t (*api_07_fn)(as608_t *ctx, as608_parameters_t *out);
CHECK_TYPE(&as608_read_parameters_start, api_07_fn);
typedef as608_status_t (*api_08_fn)(as608_t *ctx, uint16_t *count);
CHECK_TYPE(&as608_get_template_count_start, api_08_fn);
typedef as608_status_t (*api_09_fn)(as608_t *ctx, uint8_t index_page, uint8_t *bitmap, size_t capacity);
CHECK_TYPE(&as608_read_index_table_start, api_09_fn);
typedef as608_status_t (*api_10_fn)(as608_t *ctx, uint8_t *data, size_t capacity);
CHECK_TYPE(&as608_read_information_page_start, api_10_fn);
typedef as608_status_t (*api_11_fn)(as608_t *ctx);
CHECK_TYPE(&as608_capture_image_start, api_11_fn);
typedef as608_status_t (*api_12_fn)(as608_t *ctx, as608_char_buffer_t buffer);
CHECK_TYPE(&as608_generate_character_start, api_12_fn);
typedef as608_status_t (*api_13_fn)(as608_t *ctx, uint16_t *score);
CHECK_TYPE(&as608_match_buffers_start, api_13_fn);
typedef as608_status_t (*api_14_fn)(as608_t *ctx);
CHECK_TYPE(&as608_create_model_start, api_14_fn);
typedef as608_status_t (*api_15_fn)(as608_t *ctx, as608_search_t *search);
CHECK_TYPE(&as608_search_start, api_15_fn);
typedef as608_status_t (*api_16_fn)(as608_t *ctx, as608_char_buffer_t buffer, uint16_t page_id);
CHECK_TYPE(&as608_load_template_start, api_16_fn);
typedef as608_status_t (*api_17_fn)(as608_t *ctx, as608_char_buffer_t buffer, as608_template_buffer_t *output);
CHECK_TYPE(&as608_upload_template_start, api_17_fn);
typedef as608_status_t (*api_18_fn)(as608_t *ctx, as608_char_buffer_t buffer, const uint8_t *data, size_t length);
CHECK_TYPE(&as608_download_template_start, api_18_fn);
typedef as608_status_t (*api_19_fn)(as608_t *ctx, as608_char_buffer_t buffer, uint16_t page_id);
CHECK_TYPE(&as608_store_template_start, api_19_fn);
typedef as608_status_t (*api_20_fn)(as608_t *ctx, uint16_t page_id, as608_template_buffer_t *output);
CHECK_TYPE(&as608_read_template_start, api_20_fn);
typedef as608_status_t (*api_21_fn)(as608_t *ctx, uint16_t page_id, const uint8_t *data, size_t length);
CHECK_TYPE(&as608_write_template_start, api_21_fn);
typedef as608_status_t (*api_22_fn)(as608_t *ctx, uint16_t start_page, uint16_t page_count);
CHECK_TYPE(&as608_delete_templates_start, api_22_fn);
typedef as608_status_t (*api_23_fn)(as608_t *ctx);
CHECK_TYPE(&as608_empty_database_start, api_23_fn);
typedef as608_status_t (*api_24_fn)(as608_t *ctx, uint16_t *page_id);
CHECK_TYPE(&as608_enroll_start, api_24_fn);
typedef as608_status_t (*api_25_fn)(as608_t *ctx, as608_match_t *match);
CHECK_TYPE(&as608_identify_start, api_25_fn);
typedef as608_status_t (*api_26_fn)(as608_t *ctx, as608_search_t *search);
CHECK_TYPE(&as608_high_speed_search_start, api_26_fn);
typedef as608_status_t (*api_27_fn)(as608_t *ctx, as608_char_buffer_t buffer, as608_feature_file_t *output);
CHECK_TYPE(&as608_upload_feature_start, api_27_fn);
typedef as608_status_t (*api_28_fn)(as608_t *ctx, as608_char_buffer_t buffer, const as608_feature_file_t *input);
CHECK_TYPE(&as608_download_feature_start, api_28_fn);
typedef as608_status_t (*api_29_fn)(as608_t *ctx, const as608_stream_sink_t *sink);
CHECK_TYPE(&as608_upload_image_start, api_29_fn);
typedef as608_status_t (*api_30_fn)(as608_t *ctx, const as608_stream_source_t *source);
CHECK_TYPE(&as608_download_image_start, api_30_fn);
typedef as608_status_t (*api_31_fn)(as608_t *ctx, as608_image_mode_t mode);
CHECK_TYPE(&as608_generate_binary_image_start, api_31_fn);
typedef as608_status_t (*api_32_fn)(as608_t *ctx, uint32_t baud_rate);
CHECK_TYPE(&as608_set_baud_rate_start, api_32_fn);
typedef as608_status_t (*api_33_fn)(as608_t *ctx, as608_security_level_t level);
CHECK_TYPE(&as608_set_security_level_start, api_33_fn);
typedef as608_status_t (*api_34_fn)(as608_t *ctx, as608_packet_size_t size);
CHECK_TYPE(&as608_set_packet_size_start, api_34_fn);
typedef as608_status_t (*api_35_fn)(as608_t *ctx, bool enabled);
CHECK_TYPE(&as608_control_other_port_start, api_35_fn);
typedef as608_status_t (*api_36_fn)(as608_t *ctx, as608_device_info_t *info);
CHECK_TYPE(&as608_read_device_info_start, api_36_fn);
typedef as608_status_t (*api_37_fn)(as608_t *ctx, uint32_t password);
CHECK_TYPE(&as608_set_password_start, api_37_fn);
typedef as608_status_t (*api_38_fn)(as608_t *ctx, uint32_t *code);
CHECK_TYPE(&as608_get_random_code_start, api_38_fn);
typedef as608_status_t (*api_39_fn)(as608_t *ctx, uint32_t address);
CHECK_TYPE(&as608_set_device_address_start, api_39_fn);
typedef as608_status_t (*api_40_fn)(as608_t *ctx, const as608_notepad_page_t *page);
CHECK_TYPE(&as608_write_notepad_start, api_40_fn);
typedef as608_status_t (*api_41_fn)(as608_t *ctx, as608_notepad_page_t *page);
CHECK_TYPE(&as608_read_notepad_start, api_41_fn);
typedef as608_status_t (*api_42_fn)(as608_t *ctx, as608_gpio_request_t *request);
CHECK_TYPE(&as608_write_user_gpio_start, api_42_fn);
typedef as608_status_t (*api_43_fn)(as608_t *ctx, const as608_firmware_request_t *request);
CHECK_TYPE(&as608_burn_code_start, api_43_fn);
typedef as608_status_t (*api_44_fn)(as608_t *ctx, const as608_enroll_request_t *request);
CHECK_TYPE(&as608_enroll_two_scan_start, api_44_fn);
typedef as608_status_t (*api_45_fn)(as608_t *ctx, as608_search_t *search);
CHECK_TYPE(&as608_identify_range_start, api_45_fn);
typedef as608_status_t (*api_46_fn)(as608_t *ctx, as608_free_slot_t *slot);
CHECK_TYPE(&as608_find_free_slot_start, api_46_fn);

#undef CHECK
#undef CHECK_TYPE

/* Volatile pointer reads force references to all public symbols without
 * executing commands or invoking port callbacks. Link against C objects. */
int main(void) {
    api_01_fn volatile fn_1 = &as608_init;
    if (!fn_1) {
        return 1;
    }
    api_02_fn volatile fn_2 = &as608_process;
    if (!fn_2) {
        return 1;
    }
    api_03_fn volatile fn_3 = &as608_get_result;
    if (!fn_3) {
        return 1;
    }
    api_04_fn volatile fn_4 = &as608_abort;
    if (!fn_4) {
        return 1;
    }
    api_05_fn volatile fn_5 = &as608_confirm_reset;
    if (!fn_5) {
        return 1;
    }
    api_06_fn volatile fn_6 = &as608_verify_password_start;
    if (!fn_6) {
        return 1;
    }
    api_07_fn volatile fn_7 = &as608_read_parameters_start;
    if (!fn_7) {
        return 1;
    }
    api_08_fn volatile fn_8 = &as608_get_template_count_start;
    if (!fn_8) {
        return 1;
    }
    api_09_fn volatile fn_9 = &as608_read_index_table_start;
    if (!fn_9) {
        return 1;
    }
    api_10_fn volatile fn_10 = &as608_read_information_page_start;
    if (!fn_10) {
        return 1;
    }
    api_11_fn volatile fn_11 = &as608_capture_image_start;
    if (!fn_11) {
        return 1;
    }
    api_12_fn volatile fn_12 = &as608_generate_character_start;
    if (!fn_12) {
        return 1;
    }
    api_13_fn volatile fn_13 = &as608_match_buffers_start;
    if (!fn_13) {
        return 1;
    }
    api_14_fn volatile fn_14 = &as608_create_model_start;
    if (!fn_14) {
        return 1;
    }
    api_15_fn volatile fn_15 = &as608_search_start;
    if (!fn_15) {
        return 1;
    }
    api_16_fn volatile fn_16 = &as608_load_template_start;
    if (!fn_16) {
        return 1;
    }
    api_17_fn volatile fn_17 = &as608_upload_template_start;
    if (!fn_17) {
        return 1;
    }
    api_18_fn volatile fn_18 = &as608_download_template_start;
    if (!fn_18) {
        return 1;
    }
    api_19_fn volatile fn_19 = &as608_store_template_start;
    if (!fn_19) {
        return 1;
    }
    api_20_fn volatile fn_20 = &as608_read_template_start;
    if (!fn_20) {
        return 1;
    }
    api_21_fn volatile fn_21 = &as608_write_template_start;
    if (!fn_21) {
        return 1;
    }
    api_22_fn volatile fn_22 = &as608_delete_templates_start;
    if (!fn_22) {
        return 1;
    }
    api_23_fn volatile fn_23 = &as608_empty_database_start;
    if (!fn_23) {
        return 1;
    }
    api_24_fn volatile fn_24 = &as608_enroll_start;
    if (!fn_24) {
        return 1;
    }
    api_25_fn volatile fn_25 = &as608_identify_start;
    if (!fn_25) {
        return 1;
    }
    api_26_fn volatile fn_26 = &as608_high_speed_search_start;
    if (!fn_26) {
        return 1;
    }
    api_27_fn volatile fn_27 = &as608_upload_feature_start;
    if (!fn_27) {
        return 1;
    }
    api_28_fn volatile fn_28 = &as608_download_feature_start;
    if (!fn_28) {
        return 1;
    }
    api_29_fn volatile fn_29 = &as608_upload_image_start;
    if (!fn_29) {
        return 1;
    }
    api_30_fn volatile fn_30 = &as608_download_image_start;
    if (!fn_30) {
        return 1;
    }
    api_31_fn volatile fn_31 = &as608_generate_binary_image_start;
    if (!fn_31) {
        return 1;
    }
    api_32_fn volatile fn_32 = &as608_set_baud_rate_start;
    if (!fn_32) {
        return 1;
    }
    api_33_fn volatile fn_33 = &as608_set_security_level_start;
    if (!fn_33) {
        return 1;
    }
    api_34_fn volatile fn_34 = &as608_set_packet_size_start;
    if (!fn_34) {
        return 1;
    }
    api_35_fn volatile fn_35 = &as608_control_other_port_start;
    if (!fn_35) {
        return 1;
    }
    api_36_fn volatile fn_36 = &as608_read_device_info_start;
    if (!fn_36) {
        return 1;
    }
    api_37_fn volatile fn_37 = &as608_set_password_start;
    if (!fn_37) {
        return 1;
    }
    api_38_fn volatile fn_38 = &as608_get_random_code_start;
    if (!fn_38) {
        return 1;
    }
    api_39_fn volatile fn_39 = &as608_set_device_address_start;
    if (!fn_39) {
        return 1;
    }
    api_40_fn volatile fn_40 = &as608_write_notepad_start;
    if (!fn_40) {
        return 1;
    }
    api_41_fn volatile fn_41 = &as608_read_notepad_start;
    if (!fn_41) {
        return 1;
    }
    api_42_fn volatile fn_42 = &as608_write_user_gpio_start;
    if (!fn_42) {
        return 1;
    }
    api_43_fn volatile fn_43 = &as608_burn_code_start;
    if (!fn_43) {
        return 1;
    }
    api_44_fn volatile fn_44 = &as608_enroll_two_scan_start;
    if (!fn_44) {
        return 1;
    }
    api_45_fn volatile fn_45 = &as608_identify_range_start;
    if (!fn_45) {
        return 1;
    }
    api_46_fn volatile fn_46 = &as608_find_free_slot_start;
    if (!fn_46) {
        return 1;
    }
    return 0;
}
