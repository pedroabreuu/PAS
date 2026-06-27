cmake_minimum_required(VERSION 3.16)

project(PAS-UNIFESP VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(PAS_ENABLE_NATIVE "Enable -march=native / -mtune=native" ON)
option(PAS_ENABLE_FAST_MATH "Enable -ffast-math" OFF)
option(PAS_ENABLE_UNROLL "Enable -funroll-loops" ON)
option(PAS_ENABLE_LTO "Enable IPO/LTO when supported" ON)
option(PAS_ENABLE_PGO_GENERATE "Build with profile generation" OFF)
option(PAS_ENABLE_PGO_USE "Build with profile use" OFF)

if(PAS_ENABLE_PGO_GENERATE AND PAS_ENABLE_PGO_USE)
    message(FATAL_ERROR "Ative apenas uma entre PAS_ENABLE_PGO_GENERATE e PAS_ENABLE_PGO_USE.")
endif()

file(GLOB PAS_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
)

add_executable(pas ${PAS_SOURCES})

target_include_directories(pas PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(pas PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(pas PRIVATE
        -Wall
        -Wextra
        -Wshadow
        -Wpedantic
    )
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(pas PRIVATE
        $<$<CONFIG:Debug>:-O0 -g3 -fno-omit-frame-pointer>
    )

    target_compile_options(pas PRIVATE
        $<$<CONFIG:Release>:-O3 -fomit-frame-pointer -fstrict-aliasing>
    )

    target_compile_definitions(pas PRIVATE
        $<$<CONFIG:Release>:NDEBUG>
    )

    target_compile_options(pas PRIVATE
        $<$<CONFIG:RelWithDebInfo>:-O3 -g -fomit-frame-pointer -fstrict-aliasing>
    )

    target_compile_definitions(pas PRIVATE
        $<$<CONFIG:RelWithDebInfo>:NDEBUG>
    )

    if(PAS_ENABLE_NATIVE)
        target_compile_options(pas PRIVATE
            $<$<CONFIG:Release>:-march=native -mtune=native>
            $<$<CONFIG:RelWithDebInfo>:-march=native -mtune=native>
        )
    endif()

    if(PAS_ENABLE_FAST_MATH)
        target_compile_options(pas PRIVATE
            $<$<CONFIG:Release>:-ffast-math>
            $<$<CONFIG:RelWithDebInfo>:-ffast-math>
        )
    endif()

    if(PAS_ENABLE_UNROLL)
        target_compile_options(pas PRIVATE
            $<$<CONFIG:Release>:-funroll-loops>
            $<$<CONFIG:RelWithDebInfo>:-funroll-loops>
        )
    endif()
endif()

set(EVAL_REAL_SHARED_SOURCES
    ${CMAKE_SOURCE_DIR}/src/parser.cpp
    ${CMAKE_SOURCE_DIR}/src/Solver.cpp
    ${CMAKE_SOURCE_DIR}/src/Utils.cpp
)

add_executable(eval_real
    ${CMAKE_SOURCE_DIR}/tools/eval_real.cpp
    ${EVAL_REAL_SHARED_SOURCES}
)

target_include_directories(eval_real PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(eval_real PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(PAS_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)

    if(ipo_supported)
        set_property(TARGET pas PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        set_property(TARGET pas PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
    else()
        message(WARNING "IPO/LTO nao suportado: ${ipo_error}")
    endif()
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(PAS_ENABLE_PGO_GENERATE)
        target_compile_options(pas PRIVATE -fprofile-generate)
        target_link_options(pas PRIVATE -fprofile-generate)
        message(STATUS "PGO generate ativado (GCC)")
    endif()

    if(PAS_ENABLE_PGO_USE)
        target_compile_options(pas PRIVATE -fprofile-use -fprofile-correction)
        target_link_options(pas PRIVATE -fprofile-use -fprofile-correction)
        message(STATUS "PGO use ativado (GCC)")
    endif()

elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    if(PAS_ENABLE_PGO_GENERATE)
        target_compile_options(pas PRIVATE -fprofile-instr-generate)
        target_link_options(pas PRIVATE -fprofile-instr-generate)
        message(STATUS "PGO generate ativado (Clang)")
    endif()

    if(PAS_ENABLE_PGO_USE)
        target_compile_options(pas PRIVATE -fprofile-instr-use=default.profdata)
        target_link_options(pas PRIVATE -fprofile-instr-use=default.profdata)
        message(STATUS "PGO use ativado (Clang)")
    endif()
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "PAS_ENABLE_NATIVE: ${PAS_ENABLE_NATIVE}")
message(STATUS "PAS_ENABLE_FAST_MATH: ${PAS_ENABLE_FAST_MATH}")
message(STATUS "PAS_ENABLE_UNROLL: ${PAS_ENABLE_UNROLL}")
message(STATUS "PAS_ENABLE_LTO: ${PAS_ENABLE_LTO}")
message(STATUS "PAS_ENABLE_PGO_GENERATE: ${PAS_ENABLE_PGO_GENERATE}")
message(STATUS "PAS_ENABLE_PGO_USE: ${PAS_ENABLE_PGO_USE}")
