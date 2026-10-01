set(ZELRIM_SKSE_SOURCE "${PROJECT_SOURCE_DIR}/external/skse64" CACHE PATH "Pinned official SKSE source")
if(NOT EXISTS "${ZELRIM_SKSE_SOURCE}/skse64/PluginAPI.h")
    message(FATAL_ERROR "Fetch the pinned SKSE source described in docs/transport-foundation.md")
endif()
add_library(Zelrim SHARED plugin.cpp world_telemetry.cpp movement_authority.cpp
    "${ZELRIM_SKSE_SOURCE}/skse64/GameAPI.cpp"
    "${ZELRIM_SKSE_SOURCE}/skse64/GameUtilities.cpp"
    "${ZELRIM_SKSE_SOURCE}/skse64_common/Relocation.cpp")
target_include_directories(Zelrim PRIVATE "${PROJECT_SOURCE_DIR}" "${ZELRIM_SKSE_SOURCE}")
target_link_libraries(Zelrim PRIVATE zelrim_bridge)
target_compile_options(Zelrim PRIVATE /W4 /permissive-
    "/FI${PROJECT_SOURCE_DIR}/integrations/skse/api.h")
set_target_properties(Zelrim PROPERTIES PREFIX "")
