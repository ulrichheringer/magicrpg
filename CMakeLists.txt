cmake_minimum_required(VERSION 3.25)
project(magicrpg C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
find_package(PkgConfig REQUIRED)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/cmake-build-debug)

pkg_check_modules(
  ALLEGRO
  REQUIRED
  allegro-5
  allegro_primitives-5
  allegro_font-5
  allegro_ttf-5
  allegro_image-5)

# Include Allegro headers
include_directories(${ALLEGRO_INCLUDE_DIRS})
link_directories(${ALLEGRO_LIBRARY_DIRS})

add_executable(magicrpg main.c)

target_link_libraries(magicrpg ${ALLEGRO_LIBRARIES})

add_custom_command(
  TARGET magicrpg
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets
          $<TARGET_FILE_DIR:magicrpg>/assets)
