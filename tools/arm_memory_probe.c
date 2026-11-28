/** @file arm_memory_probe.c @brief Compile-only ABI measurement; never link into firmware. */
#include "as608.h"

/* Symbol sizes carry sizeof, alignment and offsetof (+1 to avoid zero arrays).
 * The probe is a separate object and contributes no bytes to libas608.a. */
#define MEASURE_TYPE(type) \
    const unsigned char size_##type[sizeof(type)] = {0}; \
    const unsigned char align_##type[_Alignof(type)] = {0}
#define MEASURE_FIELD(type, field) \
    const unsigned char offset_##type##__##field[offsetof(type, field) + 1U] = {0}; \
    const unsigned char field_##type##__##field[sizeof(((type *)0)->field)] = {0}

MEASURE_TYPE(as608_t);
MEASURE_TYPE(as608_transaction_t);
MEASURE_TYPE(as608_config_t);
MEASURE_TYPE(as608_port_t);
MEASURE_TYPE(as608_result_t);
MEASURE_TYPE(as608_state_t);
MEASURE_TYPE(as608_status_t);
MEASURE_TYPE(as608_packet_kind_t);
MEASURE_TYPE(as608_template_buffer_t);
MEASURE_TYPE(as608_stream_source_t);
MEASURE_TYPE(as608_stream_sink_t);
MEASURE_TYPE(as608_parameters_t);
MEASURE_TYPE(as608_device_info_t);
const unsigned char size_pointer[sizeof(void *)] = {0};
const unsigned char size_size_t[sizeof(size_t)] = {0};

MEASURE_FIELD(as608_t, state);
MEASURE_FIELD(as608_t, result);
MEASURE_FIELD(as608_t, step);
MEASURE_FIELD(as608_t, poll_step);
MEASURE_FIELD(as608_t, transaction);
MEASURE_FIELD(as608_t, page_id);
MEASURE_FIELD(as608_t, free_slot_capacity);
MEASURE_FIELD(as608_t, chunk_size);
MEASURE_FIELD(as608_t, poll_interval_ms);
MEASURE_FIELD(as608_t, poll_started_ms);
MEASURE_FIELD(as608_t, download_buffer);
MEASURE_FIELD(as608_t, store_after_download);
MEASURE_FIELD(as608_t, input);
MEASURE_FIELD(as608_t, input_length);
MEASURE_FIELD(as608_t, input_offset);
MEASURE_FIELD(as608_t, stream_data);
MEASURE_FIELD(as608_t, stream_capacity);
MEASURE_FIELD(as608_t, stream_offset);
MEASURE_FIELD(as608_t, stream_length);
MEASURE_FIELD(as608_t, reply_output);
MEASURE_FIELD(as608_t, stream_write);
MEASURE_FIELD(as608_t, stream_user);
MEASURE_FIELD(as608_t, stream_read);
MEASURE_FIELD(as608_t, device_info_data);

MEASURE_FIELD(as608_transaction_t, config);
MEASURE_FIELD(as608_transaction_t, started_ms);
MEASURE_FIELD(as608_transaction_t, wire_phase);
MEASURE_FIELD(as608_transaction_t, tx_frame);
MEASURE_FIELD(as608_transaction_t, tx_len);
MEASURE_FIELD(as608_transaction_t, tx_pos);
MEASURE_FIELD(as608_transaction_t, rx_header);
MEASURE_FIELD(as608_transaction_t, rx_header_pos);
MEASURE_FIELD(as608_transaction_t, rx_body);
MEASURE_FIELD(as608_transaction_t, rx_body_len);
MEASURE_FIELD(as608_transaction_t, rx_body_pos);
MEASURE_FIELD(as608_transaction_t, rx_kind);
MEASURE_FIELD(as608_transaction_t, rx_payload_len);
MEASURE_FIELD(as608_transaction_t, rx_limit);
MEASURE_FIELD(as608_transaction_t, rx_address);

MEASURE_FIELD(as608_config_t, port);
MEASURE_FIELD(as608_config_t, device_address);
MEASURE_FIELD(as608_config_t, timeout_ms);

MEASURE_FIELD(as608_port_t, user);
MEASURE_FIELD(as608_port_t, try_write);
MEASURE_FIELD(as608_port_t, try_read);
MEASURE_FIELD(as608_port_t, tx_idle);
MEASURE_FIELD(as608_port_t, clock_ms);

MEASURE_FIELD(as608_result_t, status);
MEASURE_FIELD(as608_result_t, confirmation);

#undef MEASURE_FIELD
#undef MEASURE_TYPE
