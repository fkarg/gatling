add_executable(
  gatling
  main.c
  mmap.c
  mmap.h
)

target_compile_definitions(
  gatling PRIVATE
  GATLING_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
  GATLING_VERSION_MINOR=${PROJECT_VERSION_MINOR}
  GATLING_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

if (NOT MSVC)
  set(MATH_LIB "m")
else()
  set(MATH_LIB "")
endif()

target_link_libraries(
  gatling PRIVATE
  cgpu
  ${MATH_LIB}
)

add_shader_library(
  gatling-shaders
  shaders/main.comp
  INCLUDES
    shaders/bvh.glsl
    shaders/common.glsl
    shaders/extensions.glsl
)

set_target_properties(
  gatling-shaders PROPERTIES
  SHADER_OUTPUT_DIRECTORY "${GATLING_OUTPUT_DIR}/shaders"
)

add_dependencies(
  gatling
  gatling-shaders
)
