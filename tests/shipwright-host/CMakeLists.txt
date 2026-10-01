# Exercise the actual opt-in CMake module against a tiny soh host target.
add_executable(soh ../shipwright_host.cpp)
target_compile_features(soh PRIVATE cxx_std_20)
set_target_properties(soh PROPERTIES OUTPUT_NAME shipwright_adapter_host)
include("${PROJECT_SOURCE_DIR}/integrations/shipwright/zelrim.cmake")
