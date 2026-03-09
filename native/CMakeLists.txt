cmake_minimum_required(VERSION 3.16)
project(PokemonPinball C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# SDL2 is in the project root's SDL2/ folder
set(SDL2_DIR "${CMAKE_SOURCE_DIR}/../SDL2")
set(SDL2_INCLUDE_DIR "${SDL2_DIR}/include")
set(SDL2_LIB_DIR "${SDL2_DIR}/lib/WIN/x64")

# Collect all source files
file(GLOB_RECURSE SOURCES "src/*.c")

# Application icon resource
set(APP_ICON "src/game.rc")

# Create executable (console app for debug output; change to WIN32 for release)
add_executable(game ${SOURCES} ${APP_ICON})
set_target_properties(game PROPERTIES OUTPUT_NAME "Pokemon Pinball Plus")

target_include_directories(game PRIVATE
    src/
    src/third_party
    ${SDL2_INCLUDE_DIR}
)

target_link_libraries(game
    "${SDL2_LIB_DIR}/SDL2.lib"
    winmm
)

# Copy SDL2.dll next to the executable
add_custom_command(TARGET game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${SDL2_LIB_DIR}/SDL2.dll"
    $<TARGET_FILE_DIR:game>
)

# Copy assets to build directory
add_custom_command(TARGET game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:game>/assets"
)

# Copy root asset directories (gfx, audio, data) next to executable
add_custom_command(TARGET game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/../gfx" "$<TARGET_FILE_DIR:game>/gfx"
)
add_custom_command(TARGET game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/../audio" "$<TARGET_FILE_DIR:game>/audio"
)
add_custom_command(TARGET game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/../data" "$<TARGET_FILE_DIR:game>/data"
)
