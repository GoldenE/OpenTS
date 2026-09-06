add_executable(RenderClockTest
    "${CMAKE_CURRENT_SOURCE_DIR}/renderclock_test.cpp"
    "${CMAKE_SOURCE_DIR}/code/renderclock.cpp"
    "${CMAKE_SOURCE_DIR}/code/renderposition.cpp"
)

target_compile_features(RenderClockTest PRIVATE cxx_std_20)
target_include_directories(RenderClockTest PRIVATE "${CMAKE_SOURCE_DIR}/code")
target_compile_options(RenderClockTest PRIVATE
    $<$<CONFIG:Debug>:/MTd /EHsc /Zc:__cplusplus>
    $<$<CONFIG:Release>:/MT /EHsc /Zc:__cplusplus>
)
set_target_properties(RenderClockTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME renderclock COMMAND RenderClockTest)
