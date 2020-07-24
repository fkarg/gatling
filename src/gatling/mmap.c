#include "mmap.h"

#include <assert.h>
#include <string.h>

#if defined (_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#endif

#define MAX_MAPPED_MEM_RANGES 16

typedef struct gatling_mapped_posix_range {
  void*    addr;
  uint64_t byte_count;
} gatling_mapped_posix_range;

typedef struct gatling_file {
  GatlingFileUsage usage;
  uint64_t         byte_count;
#if defined(_WIN32)
  HANDLE           file_handle;
  HANDLE           mapping_handle;
#else
  int              file_descriptor;
  gatling_mapped_posix_range mapped_ranges[MAX_MAPPED_MEM_RANGES];
#endif
} gatling_file;

#if defined (_WIN32)

bool gatling_file_create(const char* path, uint64_t byte_count, gatling_file** file)
{
  const DWORD creation_disposition = CREATE_ALWAYS;
  const DWORD desired_access = GENERIC_READ | GENERIC_WRITE;
  const DWORD share_mode = FILE_SHARE_WRITE;
  const LPSECURITY_ATTRIBUTES* security_attributes = NULL;
  const DWORD flags_and_attributes = 0;
  const HANDLE file_template = NULL;

  const HANDLE file_handle = CreateFileA(
    path,
    desired_access,
    share_mode,
    security_attributes,
    creation_disposition,
    flags_and_attributes,
    file_template
  );

  if (file_handle == INVALID_HANDLE_VALUE) {
    return false;
  }

  const DWORD protection_flags = PAGE_READWRITE;
  const DWORD maximum_size_low = byte_count & 0x00000000FFFFFFFF;
  const DWORD maximum_size_high = byte_count >> 32;

  /* "If an application specifies a size for the file mapping object that is
   * larger than the size of the actual named file on disk and if the page
   * protection allows write access, then the file on disk is increased to
   * match the specified size of the file mapping object." (MSDN, 2020-04-09) */
  const HANDLE mapping_handle = CreateFileMappingA(
    file_handle,
    security_attributes,
    protection_flags,
    maximum_size_high,
    maximum_size_low,
    NULL
  );

  if (!mapping_handle)
  {
    CloseHandle(file_handle);
    return false;
  }

  (*file) = malloc(sizeof(gatling_file));
  (*file)->usage = GATLING_FILE_USAGE_WRITE;
  (*file)->file_handle = file_handle;
  (*file)->mapping_handle = mapping_handle;
  (*file)->byte_count = byte_count;
}

bool gatling_file_open(const char* path, GatlingFileUsage usage, gatling_file** file)
{
  DWORD desired_access;
  DWORD share_mode;
  DWORD protection_flags;
  if (usage == GATLING_FILE_USAGE_READ) {
    desired_access = GENERIC_READ;
    share_mode = FILE_SHARE_READ;
    protection_flags = PAGE_READONLY;
  }
  else if (usage == GATLING_FILE_USAGE_WRITE) {
    desired_access = GENERIC_WRITE;
    share_mode = FILE_SHARE_WRITE;
    protection_flags = PAGE_READWRITE;
  }
  else {
    return false;
  }

  const LPSECURITY_ATTRIBUTES* security_attributes = NULL;
  const DWORD creation_disposition = OPEN_EXISTING;
  const DWORD flags_and_attributes = 0;
  const HANDLE file_template = NULL;

  const HANDLE file_handle = CreateFileA(
    path,
    desired_access,
    share_mode,
    security_attributes,
    creation_disposition,
    flags_and_attributes,
    file_template
  );

  if (file_handle == INVALID_HANDLE_VALUE) {
    return false;
  }

  const DWORD maximum_size_high = 0;
  const DWORD maximum_size_low = 0;

  const HANDLE mapping_handle = CreateFileMappingA(
    file_handle,
    security_attributes,
    protection_flags,
    maximum_size_high,
    maximum_size_low,
    NULL
  );

  if (!mapping_handle)
  {
    CloseHandle(file_handle);
    return false;
  }

  uint64_t byte_count;
  if (!GetFileSizeEx(file_handle, &byte_count)) {
    return false;
  }

  (*file) = malloc(sizeof(gatling_file));
  (*file)->usage = usage;
  (*file)->file_handle = file_handle;
  (*file)->mapping_handle = mapping_handle;
  (*file)->byte_count = byte_count;
}

uint64_t gatling_file_size(gatling_file* file)
{
  return file->byte_count;
}

bool gatling_file_close(gatling_file* file)
{
  const bool closed_mapping = CloseHandle(file->mapping_handle);
  const bool closed_file = CloseHandle(file->file_handle);

  return closed_mapping && closed_file;
}

void* gatling_mmap(
  gatling_file* file,
  uint64_t byte_offset,
  uint64_t byte_count)
{
  if (byte_count == 0) {
    return false;
  }

  DWORD desired_access;
  if (file->usage == GATLING_FILE_USAGE_WRITE) {
    desired_access = FILE_MAP_WRITE;
  }
  else if (file->usage == GATLING_FILE_USAGE_READ) {
    desired_access = FILE_MAP_READ;
  }
  else {
    return false;
  }

  const DWORD file_offset_low = byte_offset & 0x00000000FFFFFFFF;
  const DWORD file_offset_high = byte_offset >> 32;

  LPVOID mapped_addr = MapViewOfFile(
    file->mapping_handle,
    desired_access,
    file_offset_high,
    file_offset_low,
    byte_count
  );

  return mapped_addr;
}

bool gatling_munmap(gatling_file* file, void* addr)
{
  return UnmapViewOfFile(addr);
}

#else

bool gatling_file_create(const char* path, uint64_t byte_count, gatling_file** file)
{
  const int open_flags = O_RDWR | O_CREAT | O_TRUNC;
  const mode_t permission_flags = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

  const int file_descriptor = open(path, open_flags, permission_flags);

  if (file_descriptor < 0) {
    return false;
  }

  const bool trunc_error = ftruncate(file_descriptor, byte_count);

  if (trunc_error) {
    return false;
  }

  (*file) = malloc(sizeof(gatling_file));
  (*file)->usage = GATLING_FILE_USAGE_WRITE;
  (*file)->file_descriptor = file_descriptor;
  memset((*file)->mapped_ranges, 0, MAX_MAPPED_MEM_RANGES * sizeof(gatling_mapped_posix_range));

  return true;
}

bool gatling_file_open(const char* path, GatlingFileUsage usage, gatling_file** file)
{
  int open_flags = 0;

  if (usage == GATLING_FILE_USAGE_WRITE) {
    open_flags = O_RDWR;
  }
  else if (usage == GATLING_FILE_USAGE_READ) {
    open_flags = O_RDONLY;
  }
  else {
    return false;
  }

  const int file_descriptor = open(path, open_flags);

  if (file_descriptor < 0) {
    return false;
  }

  struct stat file_stats;
  if (fstat(file_descriptor, &file_stats))
  {
    close(file_descriptor);
    return false;
  }

  (*file) = malloc(sizeof(gatling_file));
  (*file)->usage = usage;
  (*file)->file_descriptor = file_descriptor;
  (*file)->byte_count = file_stats.st_size;
  memset((*file)->mapped_ranges, 0, MAX_MAPPED_MEM_RANGES * sizeof(gatling_mapped_posix_range));

  return true;
}

uint64_t gatling_file_size(gatling_file* file)
{
  return file->byte_count;
}

bool gatling_file_close(gatling_file* file)
{
  const int result = close(file->file_descriptor);
#ifndef NDEBUG
  /* Make sure all ranges have been unmapped. */
  for (uint32_t i = 0; i < MAX_MAPPED_MEM_RANGES; ++i) {
    assert(!file->mapped_ranges[i].addr);
  }
#endif
  free(file);
  return !result;
}

void* gatling_mmap(
  gatling_file* file,
  uint64_t byte_offset,
  uint64_t byte_count)
{
  if (byte_count == 0) {
    return false;
  }

  /* Try to find an empty mapped range data struct. */
  gatling_mapped_posix_range* range = NULL;
  for (uint32_t i = 0; i < MAX_MAPPED_MEM_RANGES; ++i)
  {
    if (!file->mapped_ranges[i].addr)
    {
      range = &file->mapped_ranges[i];
      break;
    }
  }
  if (!range) {
    return false;
  }

  /* Map the memory. */
  int protection_flags = PROT_READ;

  if (file->usage == GATLING_FILE_USAGE_WRITE) {
    protection_flags |= PROT_WRITE;
  }

  const int visibility_flags = MAP_SHARED;
  void* addr = NULL;

  void* mapped_addr = mmap(
    addr,
    byte_count,
    protection_flags,
    visibility_flags,
    file->file_descriptor,
    byte_offset
  );

  if (mapped_addr == MAP_FAILED) {
    return NULL;
  }

  range->addr = mapped_addr;
  range->byte_count = byte_count;

  return mapped_addr;
}

bool gatling_munmap(gatling_file* file, void* addr)
{
  for (uint32_t i = 0; i < MAX_MAPPED_MEM_RANGES; ++i)
  {
    gatling_mapped_posix_range* range = &file->mapped_ranges[i];
    if (range->addr != addr) {
      continue;
    }
    range->addr = NULL;
    return !munmap(addr, range->byte_count);
  }
  return false;
}

#endif