#include <stdint.h>
#include <stdio.h>

#include <cgpu.h>

#include "mmap.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static uint32_t DEFAULT_IMAGE_WIDTH = 1200;
static uint32_t DEFAULT_IMAGE_HEIGHT = 1200;
static uint32_t DEFAULT_SPP = 256;
static uint32_t DEFAULT_BOUNCES = 4;
static float DEFAULT_CAMERA_ORIGIN[3] = { 0.0f, 1.0f, 3.1f };
static float DEFAULT_CAMERA_TARGET[3] = { 0.0f, 1.0f, 0.0f };
static float DEFAULT_CAMERA_FOV = 1.0f;

typedef struct program_options {
  const char* input_file;
  const char* output_file;
  uint32_t image_width;
  uint32_t image_height;
  uint32_t spp;
  uint32_t bounces;
  float camera_origin[3];
  float camera_target[3];
  float camera_fov;
} program_options;

#define gatling_fail(msg)                                                         \
  do {                                                                            \
    printf("Gatling encountered a fatal error at line %d: %s\n", __LINE__, msg);  \
    exit(EXIT_FAILURE);                                                           \
  } while(0)

#define gatling_cgpu_ensure(result)                                                         \
  do {                                                                                      \
    if (result != CGPU_OK) {                                                                \
      printf("Gatling encountered a fatal CGPU error at line %d: %d\n", __LINE__, result);  \
      exit(EXIT_FAILURE);                                                                   \
    }                                                                                       \
  } while (0)

static void gatling_save_img_wfunc(void *context, void *data, int size)
{
  gatling_file* file;
  const char* file_path = (const char*) context;
  const bool success = gatling_file_create(file_path, size, &file);
  if (!success) {
    gatling_fail("Unable to open output file.");
  }

  void* mapped_mem = gatling_mmap(file, 0, size);
  if (!mapped_mem) {
    gatling_fail("Unable to map output file.");
  }
  memcpy(mapped_mem, data, size);

  gatling_munmap(file, mapped_mem);
  gatling_file_close(file);
}

static void gatling_save_img(
  const float* data,
  size_t float_count,
  const program_options* options)
{
  uint8_t* temp_data = malloc(float_count);
  const float gamma = 1.0f / 2.2f;

  for (size_t i = 0; i < float_count; ++i)
  {
    const float value = fmaxf(0.0f, fminf(1.0f, data[i]));
    temp_data[i] = (uint8_t) (powf(value, gamma) * 255.0f + 0.5f);
  }

  stbi_flip_vertically_on_write(true);

  const uint32_t component_count = 4;

  const int result = stbi_write_png_to_func(
    gatling_save_img_wfunc,
    (void*) options->output_file,
    options->image_width,
    options->image_height,
    component_count,
    temp_data,
    (int) (options->image_width * component_count)
  );

  free(temp_data);

  if (!result) {
    gatling_fail("Unable to save image.");
  }
}

static void gatling_get_parent_directory(
  const char* file_path,
  char* dir_path)
{
  char* last_path_sep = strrchr(file_path, '/');

  if (last_path_sep == NULL)
  {
    last_path_sep = strrchr(file_path, '\\');
  }

  if (last_path_sep != NULL)
  {
    const uint32_t char_index = (uint32_t) (last_path_sep - file_path);
    memcpy(dir_path, file_path, char_index);
    dir_path[char_index] = '\0';
  }
  else
  {
    dir_path = ".\0";
  }
}

static void gatling_print_usage_and_exit()
{
  printf("Usage: gatling <scene.gsd> <output.png> [options]\n");
  printf("\n");
  printf("Options:\n");
  printf("--image-width   [default: %u]\n", DEFAULT_IMAGE_WIDTH);
  printf("--image-height  [default: %u]\n", DEFAULT_IMAGE_HEIGHT);
  printf("--spp           [default: %u]\n", DEFAULT_SPP);
  printf("--bounces       [default: %u]\n", DEFAULT_BOUNCES);
  printf("--camera-origin [default: %.3f,%.3f,%.3f]\n",
    DEFAULT_CAMERA_ORIGIN[0],
    DEFAULT_CAMERA_ORIGIN[1],
    DEFAULT_CAMERA_ORIGIN[2]
  );
  printf("--camera-target [default: %.3f,%.3f,%.3f]\n",
    DEFAULT_CAMERA_TARGET[0],
    DEFAULT_CAMERA_TARGET[1],
    DEFAULT_CAMERA_TARGET[2]
  );
  printf("--camera-fov    [default: %.5f]\n", DEFAULT_CAMERA_FOV);
  exit(EXIT_FAILURE);
}

static void gatling_parse_args(int argc, const char* argv[], program_options* options)
{
  if (argc < 3) {
    gatling_print_usage_and_exit();
  }

  options->input_file = argv[1];
  options->output_file = argv[2];
  options->image_width = DEFAULT_IMAGE_WIDTH;
  options->image_height = DEFAULT_IMAGE_HEIGHT;
  options->spp = DEFAULT_SPP;
  options->bounces = DEFAULT_BOUNCES;
  memcpy(&options->camera_origin, &DEFAULT_CAMERA_ORIGIN, 12);
  memcpy(&options->camera_target, &DEFAULT_CAMERA_TARGET, 12);
  options->camera_fov = DEFAULT_CAMERA_FOV;

  for (int i = 3; i < argc; ++i)
  {
    const char* arg = argv[i];

    char* value = strpbrk(arg, "=");

    if (value == NULL) {
      gatling_print_usage_and_exit();
    }

    value++;

    bool fail = true;

    if (strstr(arg, "--image-width=") == arg)
    {
      char* endptr = NULL;
      options->image_width = strtol(value, &endptr, 10);
      fail = (endptr == value);
    }
    else if (strstr(arg, "--image-height=") == arg)
    {
      char* endptr = NULL;
      options->image_height = strtol(value, &endptr, 10);
      fail = (endptr == value);
    }
    else if (strstr(arg, "--spp=") == arg)
    {
      char* endptr = NULL;
      options->spp = strtol(value, &endptr, 10);
      fail = (endptr == value);
    }
    else if (strstr(arg, "--bounces=") == arg)
    {
      char* endptr = NULL;
      options->bounces = strtol(value, &endptr, 10);
      fail = (endptr == value);
    }
    else if (strstr(arg, "--camera-origin=") == arg)
    {
      const int scan_res = sscanf(
        value,
        "%f,%f,%f",
        &options->camera_origin[0],
        &options->camera_origin[1],
        &options->camera_origin[2]
      );
      fail = (scan_res != 3);
    }
    else if (strstr(arg, "--camera-target=") == arg)
    {
      const int scan_res = sscanf(
        value,
        "%f,%f,%f",
        &options->camera_target[0],
        &options->camera_target[1],
        &options->camera_target[2]
      );
      fail = (scan_res != 3);
    }
    else if (strstr(arg, "--camera-fov=") == arg)
    {
      char* endptr = NULL;
      options->camera_fov = strtof(value, &endptr);
      fail = (endptr == value);
    }

    if (fail) {
      gatling_print_usage_and_exit();
    }
  }
}

static uint64_t gatling_align_buffer(
  uint64_t offset_alignment,
  uint64_t buffer_size,
  uint64_t* total_size)
{
  const uint64_t offset =
    ((*total_size) + offset_alignment - 1) / offset_alignment
    * offset_alignment;

  (*total_size) = offset + buffer_size;

  return offset;
}

int main(int argc, const char* argv[])
{
  program_options options;
  gatling_parse_args(argc, argv, &options);

  /* Set up instance and device. */
  CgpuResult c_result = cgpu_initialize(
    "gatling",
    GATLING_VERSION_MAJOR,
    GATLING_VERSION_MINOR,
    GATLING_VERSION_PATCH
  );
  if (c_result != CGPU_OK) {
    gatling_fail("Unable to initialize cgpu.");
  }

  uint32_t device_count;
  c_result = cgpu_get_device_count(&device_count);
  if (c_result != CGPU_OK || device_count == 0) {
    gatling_fail("Unable to find device.");
  }

  cgpu_device device;
  c_result = cgpu_create_device(0, &device);
  if (c_result != CGPU_OK) {
    gatling_fail("Unable to create device.");
  }

  cgpu_physical_device_limits device_limits;
  c_result = cgpu_get_physical_device_limits(device, &device_limits);
  gatling_cgpu_ensure(c_result);

  /* Map scene file for copying. */
  gatling_file* scene_file;
  const bool ok = gatling_file_open(options.input_file, GATLING_FILE_USAGE_READ, &scene_file);
  if (!ok) {
    gatling_fail("Unable to read scene file.");
  }

  uint64_t scene_data_size = gatling_file_size(scene_file);

  uint8_t* mapped_scene_data = (uint8_t*) gatling_mmap(
    scene_file,
    0,
    scene_data_size
  );

  if (!mapped_scene_data) {
    gatling_fail("Unable to map scene file.");
  }

  /* Create input and output buffers. */
  const struct file_header {
    uint64_t node_buf_offset;
    uint64_t node_buf_size;
    uint64_t face_buf_offset;
    uint64_t face_buf_size;
    uint64_t vertex_buf_offset;
    uint64_t vertex_buf_size;
    uint64_t material_buf_offset;
    uint64_t material_buf_size;
  } file_header = *((struct file_header*) &mapped_scene_data[0]);

  uint64_t device_buf_size = 0;
  const uint64_t offset_align = device_limits.minStorageBufferOffsetAlignment;

  const uint64_t new_node_buf_offset = gatling_align_buffer(offset_align, file_header.node_buf_size, &device_buf_size);
  const uint64_t new_face_buf_offset = gatling_align_buffer(offset_align, file_header.face_buf_size, &device_buf_size);
  const uint64_t new_vertex_buf_offset = gatling_align_buffer(offset_align, file_header.vertex_buf_size, &device_buf_size);
  const uint64_t new_material_buf_offset = gatling_align_buffer(offset_align, file_header.material_buf_size, &device_buf_size);

  const uint64_t output_buffer_size = options.image_width * options.image_height * sizeof(float) * 4;
  const uint64_t staging_buffer_size = output_buffer_size > device_buf_size ? output_buffer_size : device_buf_size;

  cgpu_buffer input_buffer;
  cgpu_buffer staging_buffer;
  cgpu_buffer output_buffer;
  cgpu_buffer timestamp_buffer;

  c_result = cgpu_create_buffer(
    device,
    CGPU_BUFFER_USAGE_FLAG_STORAGE_BUFFER |
      CGPU_BUFFER_USAGE_FLAG_TRANSFER_DST,
    CGPU_MEMORY_PROPERTY_FLAG_DEVICE_LOCAL,
    device_buf_size,
    &input_buffer
  );
  gatling_cgpu_ensure(c_result);

  c_result = cgpu_create_buffer(
    device,
    CGPU_BUFFER_USAGE_FLAG_TRANSFER_SRC |
      CGPU_BUFFER_USAGE_FLAG_TRANSFER_DST,
    CGPU_MEMORY_PROPERTY_FLAG_HOST_VISIBLE |
    CGPU_MEMORY_PROPERTY_FLAG_HOST_COHERENT |
    CGPU_MEMORY_PROPERTY_FLAG_HOST_CACHED,
    staging_buffer_size,
    &staging_buffer
  );
  gatling_cgpu_ensure(c_result);

  c_result = cgpu_create_buffer(
    device,
    CGPU_BUFFER_USAGE_FLAG_STORAGE_BUFFER |
      CGPU_BUFFER_USAGE_FLAG_TRANSFER_SRC,
    CGPU_MEMORY_PROPERTY_FLAG_DEVICE_LOCAL,
    output_buffer_size,
    &output_buffer
  );
  gatling_cgpu_ensure(c_result);

  c_result = cgpu_create_buffer(
    device,
    CGPU_BUFFER_USAGE_FLAG_TRANSFER_DST,
    CGPU_MEMORY_PROPERTY_FLAG_HOST_VISIBLE |
      CGPU_MEMORY_PROPERTY_FLAG_HOST_COHERENT |
      CGPU_MEMORY_PROPERTY_FLAG_HOST_CACHED,
    32 * sizeof(uint64_t),
    &timestamp_buffer
  );
  gatling_cgpu_ensure(c_result);

  uint8_t* mapped_staging_mem;
  c_result = cgpu_map_buffer(
    device,
    staging_buffer,
    0,
    device_buf_size,
    (void*)&mapped_staging_mem
  );
  gatling_cgpu_ensure(c_result);

  memcpy(&mapped_staging_mem[new_node_buf_offset],
         &mapped_scene_data[file_header.node_buf_offset], file_header.node_buf_size);
  memcpy(&mapped_staging_mem[new_face_buf_offset],
         &mapped_scene_data[file_header.face_buf_offset], file_header.face_buf_size);
  memcpy(&mapped_staging_mem[new_vertex_buf_offset],
         &mapped_scene_data[file_header.vertex_buf_offset], file_header.vertex_buf_size);
  memcpy(&mapped_staging_mem[new_material_buf_offset],
         &mapped_scene_data[file_header.material_buf_offset], file_header.material_buf_size);

  c_result = cgpu_unmap_buffer(
    device,
    staging_buffer
  );
  gatling_cgpu_ensure(c_result);

  cgpu_command_buffer command_buffer;
  c_result = cgpu_create_command_buffer(device, &command_buffer);
  gatling_cgpu_ensure(c_result);

  gatling_munmap(scene_file, mapped_scene_data);

  gatling_file_close(scene_file);

  /* Set up pipeline. */
  cgpu_pipeline pipeline;
  {
    /* Map and create shader. */
    char dir_path[1024];
    gatling_get_parent_directory(argv[0], dir_path);

    char shader_path[2048];
    snprintf(shader_path, 2048, "%s/shaders/main.comp.spv", dir_path);

    gatling_file* file;
    if (!gatling_file_open(shader_path, GATLING_FILE_USAGE_READ, &file)) {
      gatling_fail("Unable to open shader file.");
    }

    const uint64_t file_size = gatling_file_size(file);

    uint32_t* data = (uint32_t*) gatling_mmap(file, 0, file_size);

    if (!data) {
      gatling_fail("Unable to map shader file.");
    }

    cgpu_shader shader;
    c_result = cgpu_create_shader(
      device,
      file_size,
      data,
      &shader
    );

    gatling_munmap(file, data);

    gatling_file_close(file);

    if (c_result != CGPU_OK) {
      gatling_fail("Unable to create shader.");
    }

    /* Set up pipeline. */
    const uint32_t shader_resources_buffer_count = 5;
    cgpu_shader_resource_buffer shader_resources_buffers[] = {
      { 0,       output_buffer,                       0,               CGPU_WHOLE_SIZE },
      { 1,        input_buffer,     new_node_buf_offset,     file_header.node_buf_size },
      { 2,        input_buffer,     new_face_buf_offset,     file_header.face_buf_size },
      { 3,        input_buffer,   new_vertex_buf_offset,   file_header.vertex_buf_size },
      { 4,        input_buffer, new_material_buf_offset, file_header.material_buf_size },
    };

    const uint32_t node_size = 80;
    const uint32_t node_count = file_header.node_buf_size / node_size;
    const uint32_t traversal_stack_size = (log(node_count) / log(8)) * 2;

    const cgpu_specialization_constant speccs[] = {
      { .constant_id =  0, .p_data = (void*) &device_limits.subgroupSize, .size = 4 },
      { .constant_id =  1, .p_data = (void*) &device_limits.subgroupSize, .size = 4 },
      { .constant_id =  2, .p_data = (void*) &options.image_width,        .size = 4 },
      { .constant_id =  3, .p_data = (void*) &options.image_height,       .size = 4 },
      { .constant_id =  4, .p_data = (void*) &options.spp,                .size = 4 },
      { .constant_id =  5, .p_data = (void*) &options.bounces,            .size = 4 },
      { .constant_id =  6, .p_data = (void*) &traversal_stack_size,       .size = 4 },
      { .constant_id =  7, .p_data = (void*) &options.camera_origin[0],   .size = 4 },
      { .constant_id =  8, .p_data = (void*) &options.camera_origin[1],   .size = 4 },
      { .constant_id =  9, .p_data = (void*) &options.camera_origin[2],   .size = 4 },
      { .constant_id = 10, .p_data = (void*) &options.camera_target[0],   .size = 4 },
      { .constant_id = 11, .p_data = (void*) &options.camera_target[1],   .size = 4 },
      { .constant_id = 12, .p_data = (void*) &options.camera_target[2],   .size = 4 },
      { .constant_id = 13, .p_data = (void*) &options.camera_fov,         .size = 4 }
    };
    const uint32_t specc_count = 14;

    c_result = cgpu_create_pipeline(
      device,
      shader_resources_buffer_count,
      shader_resources_buffers,
      0,
      NULL,
      shader,
      "main",
      specc_count,
      speccs,
      0,
      &pipeline
    );
    gatling_cgpu_ensure(c_result);

    c_result = cgpu_destroy_shader(device, shader);
    gatling_cgpu_ensure(c_result);
  }

  c_result = cgpu_begin_command_buffer(command_buffer);
  gatling_cgpu_ensure(c_result);

  /* Write start timestamp. */
  c_result = cgpu_cmd_reset_timestamps(
    command_buffer,
    0,
    32
  );
  gatling_cgpu_ensure(c_result);

  c_result = cgpu_cmd_write_timestamp(command_buffer, 0);
  gatling_cgpu_ensure(c_result);

  /* Copy staging buffer to input buffer. */
  c_result = cgpu_cmd_copy_buffer(
    command_buffer,
    staging_buffer,
    0,
    input_buffer,
    0,
    device_buf_size
  );
  gatling_cgpu_ensure(c_result);

  c_result = cgpu_cmd_pipeline_barrier(
    command_buffer,
    0, NULL,
    1, &(cgpu_buffer_memory_barrier) {
      .src_access_flags = CGPU_MEMORY_ACCESS_FLAG_TRANSFER_WRITE,
      .dst_access_flags = CGPU_MEMORY_ACCESS_FLAG_SHADER_READ,
      .buffer = input_buffer,
      .offset = 0,
      .size = CGPU_WHOLE_SIZE
    },
    0, NULL
  );
  gatling_cgpu_ensure(c_result);

  c_result = cgpu_cmd_bind_pipeline(command_buffer, pipeline);
  gatling_cgpu_ensure(c_result);

  /* Trace rays. */
  c_result = cgpu_cmd_dispatch(
    command_buffer,
    (options.image_width / device_limits.subgroupSize) + 1,
    (options.image_height / device_limits.subgroupSize) + 1,
    1
  );
  gatling_cgpu_ensure(c_result);

  /* Copy output buffer to staging buffer. */
  c_result = cgpu_cmd_pipeline_barrier(
    command_buffer,
    0, NULL,
    1, &(cgpu_buffer_memory_barrier) {
      .src_access_flags = CGPU_MEMORY_ACCESS_FLAG_SHADER_WRITE,
      .dst_access_flags = CGPU_MEMORY_ACCESS_FLAG_TRANSFER_READ,
      .buffer = output_buffer,
      .offset = 0,
      .size = CGPU_WHOLE_SIZE
    },
    0, NULL
  );
  gatling_cgpu_ensure(c_result);

  c_result = cgpu_cmd_copy_buffer(
    command_buffer,
    output_buffer,
    0,
    staging_buffer,
    0,
    output_buffer_size
  );
  gatling_cgpu_ensure(c_result);

  /* Write end timestamp and copy timestamps. */
  c_result = cgpu_cmd_write_timestamp(command_buffer, 1);
  gatling_cgpu_ensure(c_result);

  c_result = cgpu_cmd_copy_timestamps(
    command_buffer,
    timestamp_buffer,
    0,
    2,
    true
  );
  gatling_cgpu_ensure(c_result);

  /* End and submit command buffer. */
  c_result = cgpu_end_command_buffer(command_buffer);
  gatling_cgpu_ensure(c_result);

  cgpu_fence fence;
  c_result = cgpu_create_fence(device, &fence);
  gatling_cgpu_ensure(c_result);

  c_result = cgpu_reset_fence(device, fence);
  gatling_cgpu_ensure(c_result);

  printf("Rendering...\n");

  c_result = cgpu_submit_command_buffer(
    device,
    command_buffer,
    fence
  );
  gatling_cgpu_ensure(c_result);

  c_result = cgpu_wait_for_fence(device, fence);
  gatling_cgpu_ensure(c_result);

  /* Read timestamps. */
  uint64_t* timestamps;

  c_result = cgpu_map_buffer(
    device,
    timestamp_buffer,
    0,
    CGPU_WHOLE_SIZE,
    (void**) &timestamps
  );
  gatling_cgpu_ensure(c_result);

  const uint64_t timestamp_start = timestamps[0];
  const uint64_t timestamp_end = timestamps[1];

  c_result = cgpu_unmap_buffer(device, timestamp_buffer);
  gatling_cgpu_ensure(c_result);

  const float elapsed_nanoseconds  = (float) (timestamp_end - timestamp_start) * device_limits.timestampPeriod;
  const float elapsed_microseconds = elapsed_nanoseconds / 1000.0f;
  const float elapsed_milliseconds = elapsed_microseconds / 1000.0f;
  printf("Total rendering time: %.2fms\n", elapsed_milliseconds);

  /* Read data from gpu and save image. */
  float* image_data = malloc(output_buffer_size);

  c_result = cgpu_map_buffer(
    device,
    staging_buffer,
    0,
    output_buffer_size,
    (void**) &mapped_staging_mem
  );
  gatling_cgpu_ensure(c_result);

  memcpy(
    image_data,
    mapped_staging_mem,
    output_buffer_size
  );

  c_result = cgpu_unmap_buffer(
    device,
    staging_buffer
  );
  gatling_cgpu_ensure(c_result);

  gatling_save_img(
    image_data,
    output_buffer_size / 4,
    &options
  );

  free(image_data);

  /* Clean up. */
  c_result = cgpu_destroy_fence(device, fence);
  gatling_cgpu_ensure(c_result);
  c_result = cgpu_destroy_command_buffer(device, command_buffer);
  gatling_cgpu_ensure(c_result);
  c_result = cgpu_destroy_pipeline(device, pipeline);
  gatling_cgpu_ensure(c_result);
  c_result = cgpu_destroy_buffer(device, input_buffer);
  gatling_cgpu_ensure(c_result);
  c_result = cgpu_destroy_buffer(device, staging_buffer);
  gatling_cgpu_ensure(c_result);
  c_result = cgpu_destroy_buffer(device, output_buffer);
  gatling_cgpu_ensure(c_result);
  c_result = cgpu_destroy_buffer(device, timestamp_buffer);
  gatling_cgpu_ensure(c_result);
  c_result = cgpu_destroy_device(device);
  gatling_cgpu_ensure(c_result);
  c_result = cgpu_destroy();
  gatling_cgpu_ensure(c_result);

  return EXIT_SUCCESS;
}
