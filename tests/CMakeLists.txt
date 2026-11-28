# Host-only test targets. Production source is never compiled as C++.
function(as608_test_options target)
    set_target_properties(${target} PROPERTIES
        C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO
        CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO
    )
    # Existing fixtures use assert(), including calls with side effects.
    # Release/NDEBUG must not silently remove test operations.
    if(MSVC)
        target_compile_options(${target} PRIVATE /UNDEBUG)
    elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -pedantic -UNDEBUG)
    else()
        message(FATAL_ERROR "Unsupported host test compiler; use GCC/Clang or disable AS608_BUILD_TESTS.")
    endif()
    target_include_directories(${target} PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        "${PROJECT_SOURCE_DIR}"
    )
    as608_apply_sanitizers(${target})
endfunction()

function(as608_add_test name group)
    add_executable(test_${name} ${group}/test_as608_${name}.c)
    as608_test_options(test_${name})
    if(name STREQUAL "packet")
        target_link_libraries(test_${name} PRIVATE as608_packet_obj)
    elseif(name STREQUAL "transaction")
        target_link_libraries(test_${name} PRIVATE as608_packet_obj as608_transaction_obj)
    else()
        target_link_libraries(test_${name} PRIVATE as608::as608)
    endif()
    add_test(NAME ${name} COMMAND test_${name})
    set_tests_properties(${name} PROPERTIES LABELS "${group}" TIMEOUT 60)
endfunction()

# Explicit inventory of all former CI runtime checks.
set(unit_tests
    transaction
    packet
    init
    process
    get_result
    abort
    confirm_reset
    verify_password
    read_parameters
    get_template_count
    read_index_table
    read_information_page
    capture_image
    generate_character
    match_buffers
    create_model
    search
    load_template
    upload_template
    download_template
    store_template
    read_template
    write_template
    delete_templates
    empty_database
    enroll
    identify
    high_speed_search
    upload_feature
    download_feature
    upload_image
    download_image
    generate_binary_image
    set_baud_rate
    set_security_level
    set_packet_size
    control_other_port
    read_device_info
    set_password
    get_random_code
    set_device_address
    write_notepad
    read_notepad
    write_user_gpio
    burn_code
    enroll_two_scan
    identify_range
    find_free_slot
)
foreach(name IN LISTS unit_tests)
    as608_add_test(${name} unit)
endforeach()
foreach(name IN ITEMS commands async async_faults)
    as608_add_test(${name} integration)
endforeach()

# Generate a C++ translation unit that includes the original C fixture.
function(as608_cpp_wrapper name output)
    set(path "${CMAKE_CURRENT_BINARY_DIR}/test_${name}_cpp.cpp")
    file(GENERATE OUTPUT "${path}"
        CONTENT "#include \"${CMAKE_CURRENT_SOURCE_DIR}/unit/test_as608_${name}.c\"\n")
    set(${output} "${path}" PARENT_SCOPE)
endfunction()

foreach(name IN ITEMS types packet_header port_header transaction_header)
    add_library(check_${name}_c OBJECT unit/test_as608_${name}.c)
    as608_test_options(check_${name}_c)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND NOT MSVC)
        target_compile_options(check_${name}_c PRIVATE -ffreestanding)
    endif()
    as608_cpp_wrapper(${name} wrapper)
    add_library(check_${name}_cpp OBJECT "${wrapper}")
    as608_test_options(check_${name}_cpp)
endforeach()

# Independent public-header check and C/C++ linkage against the C archive.
add_library(check_public_c OBJECT unit/test_as608_header.c)
as608_test_options(check_public_c)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND NOT MSVC)
    target_compile_options(check_public_c PRIVATE -ffreestanding)
endif()
add_executable(test_public_c unit/test_as608_header.c)
as608_cpp_wrapper(header public_wrapper)
add_executable(test_public_cpp "${public_wrapper}")
foreach(name IN ITEMS public_c public_cpp)
    as608_test_options(test_${name})
    target_link_libraries(test_${name} PRIVATE as608::as608)
    add_test(NAME ${name} COMMAND test_${name})
    set_tests_properties(${name} PROPERTIES LABELS "unit;header" TIMEOUT 60)
endforeach()
