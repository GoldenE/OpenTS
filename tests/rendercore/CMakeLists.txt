set(ui_functions "")
foreach(entry "srfcache.cpp|SurfaceCacheClass::Draw" "srfcache.cpp|SurfaceCacheClass::DrawTrans" "srfcache.cpp|SurfaceCacheClass::DrawMasked" "ownrdraw.cpp|ODDrawEdgeGlow" "ownrdraw.cpp|ODDrawGradientRect" "ownrdraw.cpp|ODDrawBevelDarken" "ownrdraw.cpp|ODFillRectTrans")
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 filename)
    list(GET parts 1 function)
    set(path "${CMAKE_SOURCE_DIR}/code/${filename}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
    file(READ "${path}" source)
    string(REGEX MATCH "[^\n]* ${function}\\([^\n]*\\)\n\\{[^\n]*(\n[^}][^\n]*)*\n\\}" body "${source}")
    if(NOT body)
        message(FATAL_ERROR "Cannot extract UI raster function ${function}")
    endif()
    string(APPEND ui_functions "${body}\n\n")
endforeach()
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/render_ui_functions.inc" CONTENT "${ui_functions}" @ONLY)
set(startup_path "${CMAKE_SOURCE_DIR}/code/startup.cpp")
set(options_path "${CMAKE_SOURCE_DIR}/code/options.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${startup_path}" "${options_path}")
file(READ "${startup_path}" startup_source)
file(READ "${options_path}" options_source)
set(startup_steps "")
foreach(step "Configuration|ConfigINI.Load(*cfile, false)" "RenderSettings|Load_Render_Settings(ConfigINI)" "Video|Video_Init(MainWindow)" "Primary|DSurface::Create_Primary()" "Surfaces|Allocate_Surfaces(VisibleRect," "Game|Main_Game(argc, argv)")
    string(REPLACE "|" ";" parts "${step}")
    list(GET parts 0 name)
    list(GET parts 1 statement)
    string(FIND "${startup_source}" "${statement}" position)
    if(position LESS 0)
        if(NOT name STREQUAL "RenderSettings")
            message(FATAL_ERROR "Cannot identify startup step ${name}")
        endif()
    else()
        list(APPEND startup_steps "${position}|${name}")
    endif()
endforeach()
list(SORT startup_steps COMPARE NATURAL)
set(startup_order "constexpr StartupStep Startup_Order[] = {")
foreach(step ${startup_steps})
    string(REGEX REPLACE "^[0-9]+\\|" "" name "${step}")
    string(APPEND startup_order "StartupStep::${name},")
endforeach()
string(APPEND startup_order "};\n")
string(FIND "${options_source}" "Load_Render_Settings(ConfigINI)" late_settings)
if(late_settings LESS 0)
    string(APPEND startup_order "constexpr bool Late_Render_Settings = false;\n")
else()
    string(APPEND startup_order "constexpr bool Late_Render_Settings = true;\n")
endif()
set(unit_path "${CMAKE_SOURCE_DIR}/code/unittype.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${unit_path}")
file(READ "${unit_path}" unit_source)
string(REGEX MATCH "EightBitSurface = new BSurface\\(([^\n;]*)\\);" unit_allocator "${unit_source}")
if(NOT unit_allocator)
    message(FATAL_ERROR "Cannot extract unit composite surface allocator")
endif()
string(APPEND startup_order "std::unique_ptr<BSurface> Allocate_Unit_Composite_From_Production() { return std::unique_ptr<BSurface>(new BSurface(${CMAKE_MATCH_1})); }\n")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/render_startup_order.inc" CONTENT "${startup_order}" @ONLY)
add_executable(RenderCoreTest main.cpp ui.cpp cache.cpp startup.cpp
    "${CMAKE_SOURCE_DIR}/code/rendercontext.cpp"
    "${CMAKE_SOURCE_DIR}/code/_rendercontext.cpp"
    "${CMAKE_SOURCE_DIR}/code/xsurface.cpp"
    "${CMAKE_SOURCE_DIR}/code/dsurface.cpp"
    "${CMAKE_SOURCE_DIR}/code/blit.cpp"
    "${CMAKE_SOURCE_DIR}/code/buff.cpp"
    "${CMAKE_SOURCE_DIR}/code/bsurface.cpp"
    "${CMAKE_SOURCE_DIR}/code/rastercache.cpp"
    "${CMAKE_SOURCE_DIR}/code/rasterscratch.cpp"
    "${CMAKE_SOURCE_DIR}/code/_rastercache.cpp"
    "${CMAKE_SOURCE_DIR}/code/rgb.cpp"
    "${CMAKE_SOURCE_DIR}/code/renderportable.cpp"
    "${CMAKE_SOURCE_DIR}/code/abuffer.cpp"
    "${CMAKE_SOURCE_DIR}/code/zbuffer.cpp"
    "${CMAKE_SOURCE_DIR}/code/_zbuffer.cpp")
target_include_directories(RenderCoreTest PRIVATE "${CMAKE_SOURCE_DIR}/code" "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_features(RenderCoreTest PRIVATE cxx_std_20)
target_compile_options(RenderCoreTest PRIVATE /EHsc /Gy)
target_link_options(RenderCoreTest PRIVATE /OPT:REF /INCREMENTAL:NO)
target_link_libraries(RenderCoreTest PRIVATE gdi32 user32 winmm)
add_test(NAME rendercore COMMAND RenderCoreTest)
