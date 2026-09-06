add_executable(LCWStreamsTest lcw_streams_test.cpp
    "${CMAKE_SOURCE_DIR}/code/lcw.cpp"
    "${CMAKE_SOURCE_DIR}/code/lcwpipe.cpp"
    "${CMAKE_SOURCE_DIR}/code/lcwstraw.cpp"
    "${CMAKE_SOURCE_DIR}/code/pipe.cpp"
    "${CMAKE_SOURCE_DIR}/code/straw.cpp")
target_compile_features(LCWStreamsTest PRIVATE cxx_std_20)
target_include_directories(LCWStreamsTest PRIVATE "${CMAKE_SOURCE_DIR}/code")
target_compile_options(LCWStreamsTest PRIVATE /EHsc $<$<CONFIG:Debug>:/MTd> $<$<CONFIG:Release>:/MT>)
add_test(NAME lcwstreams COMMAND LCWStreamsTest)
