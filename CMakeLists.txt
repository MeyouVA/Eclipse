cmake_minimum_required(VERSION 3.21)
project(Eclipse VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg PrintSupport Qml)

# Eclipse is four libraries and a small program that starts them (see src/EclipseExport.h).
# Each folder of src is a module, sorted into sub-folders by what the files are for; a source
# file put anywhere under a module's folder is built into that module.
# The libraries land beside the programs, where Windows looks for them.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
# Plain functions and structs are shown to the other modules by the build; classes with
# signals and slots carry the macro of their module as well.
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

function(eclipse_module name folder define)
    file(GLOB_RECURSE sources CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/${folder}/*.cpp ${CMAKE_SOURCE_DIR}/src/${folder}/*.h)
    add_library(${name} SHARED ${sources} ${CMAKE_SOURCE_DIR}/src/EclipseExport.h)
    target_compile_definitions(${name} PRIVATE ${define})
    target_include_directories(${name} PUBLIC ${CMAKE_SOURCE_DIR}/src)
    if(MSVC)
        # 4251 / 4275: a class shown to other modules has Qt members and bases - which every module has too.
        target_compile_options(${name} PRIVATE /W4 /utf-8 /permissive- /wd4251 /wd4275)
    endif()
endfunction()

# The scene: layers, drawings, keyframes, and the commands that change them.
eclipse_module(EclipseModel model ECLIPSE_MODEL_BUILD)
target_link_libraries(EclipseModel PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)

# Sound: reading, mixing, playing; lip-sync detection.
eclipse_module(EclipseAudio audio ECLIPSE_AUDIO_BUILD)
target_link_libraries(EclipseAudio PUBLIC EclipseModel Qt6::Core PRIVATE winmm)

# The menus, toolbars and shortcuts read from `resources`, and the look of the program.
eclipse_module(EclipseActions actions ECLIPSE_ACTIONS_BUILD)
target_link_libraries(EclipseActions PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets PRIVATE Qt6::Svg)

# The window and all its views.
eclipse_module(EclipseUi ui ECLIPSE_UI_BUILD)
target_link_libraries(EclipseUi PUBLIC EclipseModel EclipseAudio EclipseActions Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Qml PRIVATE Qt6::Svg Qt6::PrintSupport winmm)

# The program: it starts the window.
add_executable(Eclipse WIN32 src/app/main.cpp)
if(WIN32)
    # The icon of the program file.
    target_sources(Eclipse PRIVATE src/app/Eclipse.rc)
endif()
target_link_libraries(Eclipse PRIVATE EclipseUi)
if(MSVC)
    target_compile_options(Eclipse PRIVATE /W4 /utf-8 /permissive- /wd4251 /wd4275)
endif()

# The tests, where the tests folder is there (it is not part of what is given out).
if(EXISTS ${CMAKE_SOURCE_DIR}/tests/model_test.cpp AND EXISTS ${CMAKE_SOURCE_DIR}/tests/ui_test.cpp)
    # A small console test of the model and the sound detection (no window needed).
    # (Its parts are in tests/model.)
    file(GLOB model_test_parts CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/model/*.cpp ${CMAKE_SOURCE_DIR}/tests/model/*.h)
    add_executable(EclipseModelTest tests/model_test.cpp ${model_test_parts})
    target_include_directories(EclipseModelTest PRIVATE ${CMAKE_SOURCE_DIR}/tests ${CMAKE_SOURCE_DIR}/tests/model)
    target_link_libraries(EclipseModelTest PRIVATE EclipseModel EclipseAudio)

    # Drives the real window with synthetic input and saves screenshots.
    # (Its parts are in tests/ui, a file for each kind of thing tried.)
    file(GLOB ui_test_parts CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/ui/*.cpp ${CMAKE_SOURCE_DIR}/tests/ui/*.h)
    add_executable(EclipseUiTest tests/ui_test.cpp ${ui_test_parts})
    target_include_directories(EclipseUiTest PRIVATE ${CMAKE_SOURCE_DIR}/tests ${CMAKE_SOURCE_DIR}/tests/ui)
    target_link_libraries(EclipseUiTest PRIVATE EclipseUi Qt6::Svg Qt6::PrintSupport)

    if(MSVC)
        target_compile_options(EclipseModelTest PRIVATE /utf-8 /permissive- /wd4251 /wd4275)
        target_compile_options(EclipseUiTest PRIVATE /utf-8 /permissive- /wd4251 /wd4275 /bigobj)
    endif()
endif()

# The menus, toolbars and shortcuts are read at run time from `resources` next to the program.
add_custom_target(EclipseResources ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/resources ${CMAKE_BINARY_DIR}/resources
    COMMENT "Copying resources")
