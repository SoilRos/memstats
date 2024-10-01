cmake_minimum_required(VERSION 3.21)
project(memstats LANGUAGES C CXX)

find_package(TBB)
find_package(Threads)

add_library(memstats memstats.cc)
target_link_libraries(memstats PRIVATE $<TARGET_NAME_IF_EXISTS:TBB::tbb> $<TARGET_NAME_IF_EXISTS:Threads::Threads>)

include(GNUInstallDirs)
install(FILES memstats.hh
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

target_include_directories(memstats PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

target_compile_features(memstats PRIVATE cxx_std_23)
target_compile_features(memstats INTERFACE cxx_std_11)
target_compile_definitions(memstats PRIVATE $<$<TARGET_EXISTS:TBB::tbb>:MEMSTAT_HAVE_TBB>)
set_target_properties(memstats PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    EXPORT_NAME MemStats)

add_library(MemStats::MemStats ALIAS memstats)

install(TARGETS memstats EXPORT memstats-targets ARCHIVE)

file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/config.cmake.in
[[
@PACKAGE_INIT@

include(CMakeFindDependencyMacro)
find_dependency(TBB)
find_dependency(Threads)

include("${CMAKE_CURRENT_LIST_DIR}/memstats-targets.cmake")
]])

include(CMakePackageConfigHelpers)
configure_package_config_file(${CMAKE_CURRENT_BINARY_DIR}/config.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/memstats-config.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/memstats
)

install(EXPORT memstats-targets
        FILE memstats-targets.cmake
        NAMESPACE MemStats::
        DESTINATION "${INSTALL_LIBDIR}/cmake/memstats"
)

export(EXPORT memstats-targets
       NAMESPACE MemStats::
)


if(memstats_IS_TOP_LEVEL)
    add_executable(example_01 example_01.cc)
    target_link_libraries(example_01 PUBLIC MemStats::MemStats)

    add_executable(example_02 example_02.cc)
    target_link_libraries(example_02 PUBLIC MemStats::MemStats)

    if(TARGET Threads::Threads)
        add_executable(example_03 example_03.cc)
        target_link_libraries(example_03 PUBLIC MemStats::MemStats)
    endif()
endif()
