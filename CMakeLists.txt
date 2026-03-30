cmake_minimum_required(VERSION 3.10)
project(easy_vfio C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -Werror -O2)
add_compile_definitions(_POSIX_C_SOURCE=200809L _GNU_SOURCE)

# Library sources
set(LIB_SOURCES
    src/vfio_internal_container.c
    src/vfio_internal_group.c
    src/vfio_internal_device.c
    src/vfio_internal_region.c
    src/vfio_internal_dma.c
    src/vfio_internal_interrupt.c
    src/vfio_internal_pci_config.c
    src/vfio_internal_utils.c
    src/vfio_ctx.c
)

# Static library
add_library(easy_vfio_static STATIC ${LIB_SOURCES})
target_include_directories(easy_vfio_static PUBLIC include PRIVATE src)
set_target_properties(easy_vfio_static PROPERTIES OUTPUT_NAME easy_vfio)

# Shared library
add_library(easy_vfio_shared SHARED ${LIB_SOURCES})
target_include_directories(easy_vfio_shared PUBLIC include PRIVATE src)
set_target_properties(easy_vfio_shared PROPERTIES OUTPUT_NAME easy_vfio)

# Test executable
add_executable(test_easy_vfio tests/test_easy_vfio.c)
target_link_libraries(test_easy_vfio PRIVATE easy_vfio_static)

# Example executable
add_executable(example_basic examples/example_basic.c)
target_link_libraries(example_basic PRIVATE easy_vfio_static)

# CTest integration
enable_testing()
add_test(NAME easy_vfio_tests COMMAND test_easy_vfio)

# Install rules
include(GNUInstallDirs)
install(TARGETS easy_vfio_static easy_vfio_shared
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/easy_vfio.h include/vfio_internal.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
