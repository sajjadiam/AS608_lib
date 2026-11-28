cmake_minimum_required(VERSION 3.20)
project(AS608 LANGUAGES C)

# Native standalone builds include tests. Embedded and subdirectory
# consumers build only the core unless explicitly requested otherwise.
set(_as608_test_default OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND NOT CMAKE_CROSSCOMPILING)
    set(_as608_test_default ON)
endif()
option(AS608_BUILD_TESTS "Build native AS608 host tests" ${_as608_test_default})
unset(_as608_test_default)
option(AS608_ENABLE_SANITIZERS "Instrument native host tests with ASan and UBSan" OFF)
if(AS608_ENABLE_SANITIZERS AND NOT AS608_BUILD_TESTS)
    message(FATAL_ERROR "AS608_ENABLE_SANITIZERS requires AS608_BUILD_TESTS=ON.")
endif()

# Compile each production translation unit once. The two object targets
# also support independent lower-layer test linkage.
add_library(as608_packet_obj OBJECT src/as608_packet.c)
add_library(as608_transaction_obj OBJECT src/as608_transaction.c)
add_library(as608 STATIC
    src/as608.c
    $<TARGET_OBJECTS:as608_packet_obj>
    $<TARGET_OBJECTS:as608_transaction_obj>
)
add_library(as608::as608 ALIAS as608)

foreach(target IN ITEMS as608_packet_obj as608_transaction_obj as608)
    set_target_properties(${target} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
    )
    target_compile_features(${target} PUBLIC c_std_11)
    target_include_directories(${target} PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
    )
endforeach()

if(AS608_BUILD_TESTS)
    if(CMAKE_CROSSCOMPILING)
        message(FATAL_ERROR "AS608 host tests require a native build; set AS608_BUILD_TESTS=OFF.")
    endif()
    enable_language(CXX)
    enable_testing()

    if(AS608_ENABLE_SANITIZERS)
        if(MSVC OR NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" OR
                NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            message(FATAL_ERROR "AS608 sanitizers require GCC or Clang with GNU-style flags.")
        endif()
    endif()

    # Instrument every core and test translation unit, and link the runtime
    # into each test executable (object libraries have no link step).
    function(as608_apply_sanitizers target)
        if(AS608_ENABLE_SANITIZERS)
            target_compile_options(${target} PRIVATE
                -fsanitize=address,undefined
                -fno-sanitize-recover=all
                -fno-omit-frame-pointer
            )
            get_target_property(target_type ${target} TYPE)
            if(target_type STREQUAL "EXECUTABLE")
                target_link_options(${target} PRIVATE
                    -fsanitize=address,undefined
                    -fno-sanitize-recover=all
                )
            endif()
        endif()
    endfunction()
    foreach(target IN ITEMS as608_packet_obj as608_transaction_obj as608)
        if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND NOT MSVC)
            target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -pedantic)
        endif()
        as608_apply_sanitizers(${target})
    endforeach()
    add_subdirectory(tests)
endif()
