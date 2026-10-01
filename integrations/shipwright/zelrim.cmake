# Compile in soh to inherit its CRT, toolset and configuration.
if(NOT WIN32)
    message(FATAL_ERROR "The Zelrim heartbeat adapter requires Windows")
endif()
get_filename_component(ZELRIM_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
target_sources(soh PRIVATE
    "${ZELRIM_SOURCE_ROOT}/bridge/bridge.cpp"
    "${ZELRIM_SOURCE_ROOT}/integrations/shipwright/heartbeat.cpp")
target_include_directories(soh PRIVATE "${ZELRIM_SOURCE_ROOT}")
target_compile_definitions(soh PRIVATE ZELRIM_HEARTBEAT_ENABLED)
# Keep Windows header policy local to our sources; upstream uses common-dialog
# declarations excluded by WIN32_LEAN_AND_MEAN.
set_source_files_properties(
    "${ZELRIM_SOURCE_ROOT}/bridge/bridge.cpp"
    "${ZELRIM_SOURCE_ROOT}/integrations/shipwright/heartbeat.cpp"
    PROPERTIES COMPILE_DEFINITIONS "WIN32_LEAN_AND_MEAN;NOMINMAX")
