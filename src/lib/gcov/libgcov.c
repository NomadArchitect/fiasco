/* SPDX-License-Identifier: GPL-2.0-only OR License-Ref-kk-custom */
/*
 * Copyright (C) 2019-2021, 2023 Kernkonzept GmbH.
 * Author(s): Timo Nicolai <tnicolai@kernkonzept.com>
 *            Steffen Liebergeld <steffen.liebergeld@kernkonzept.com>
 *            Marcus Haehnel <marcus.haehnel@kernkonzept.com>
 *
 * This file is based on Linux v4.14.73 kernel/gcov/gcov_4_7.c
 */
/*
 *  This code provides functions to handle gcc's profiling data format
 *  introduced with gcc 4.7.
 *
 *  This file is based heavily on gcc_3_4.c file.
 *
 *  For a better understanding, refer to gcc source:
 *  gcc/gcov-io.h
 *  libgcc/libgcov.c
 *
 *  Uses gcc-internal data definitions.
 */

// Define if the output should be in runlength encoded base64
#include <stdarg.h>

#ifdef L4API_l4f
  #include <l4/sys/vcon.h> //For non-bootstrap version
#else
  #ifndef L4API_
    #include <simpleio.h> //For the kernel
  #else
    #include <stddef.h> //NULL For bootstrap
  #endif              // L4API_
#endif              // L4API_l4f

typedef __SIZE_TYPE__ size_t;
typedef int pid_t;
#include "gcov.h"
#include "output-be.h"
#include "data-be.h"

const char b64[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// length of out must be enough to hold base64 string, e.g. for 10 chars -> 16!
long
print_base64(const char *s, int l, char *out)
{
  char *pos = out;

  if (!s)
    return 0;

  const char *end = s + l;
  while (end - s >= 3)
    {
      // As long as we have new data for four base64 bytes
      *pos++ = b64[s[0] >> 2];
      *pos++ = b64[(s[0] & 0x3) << 4 | (s[1] >> 4)];
      *pos++ = b64[(s[1] & 0xf) << 2 | (s[2] >> 6)];
      *pos++ = b64[s[2] & 0x3f];
      s += 3;
    }

  if (end - s)
    {
      *pos++ = b64[s[0] >> 2];
      if (end - s == 1)
        {
          *pos++ = b64[(s[0] & 0x03) << 4];
          *pos++ = '=';
        }
      else
        {
          *pos++ = b64[(s[0] & 0x03) << 4 | s[1] >> 4];
          *pos++ = b64[(s[1] & 0x0f) << 2];
        }
      *pos++ = '=';
    }
  return pos - out;
}

extern int
printf(const char *format, ...) __attribute__((weak));

void
vconprintn(char const *s, int len)
{
  if (printf)
    {
      printf("%.*s", len, s);
    }
  else
    {
#ifdef L4API_l4f
      const char *err = "Failed to write properly\n";
      if (l4_vcon_write(L4_BASE_LOG_CAP, s, len) < len)
        l4_vcon_write(L4_BASE_LOG_CAP, err, __builtin_strlen(err));
#endif
    }
}

void
vconprint(char const *s)
{
  vconprintn(s, __builtin_strlen(s));
}

/*
 * Determine whether a counter is active. Doesn't change at run-time.
 */
static int
counter_active(struct gcov_info *info, unsigned int type)
{
  return info->merge[type] ? 1 : 0;
}

/**
 * store_gcov_u64 - store 64 bit number in gcov format to buffer
 * @v: value to be stored
 *
 * Number format defined by gcc: numbers are recorded in the 32 bit
 * unsigned binary form of the endianness of the machine generating the
 * file. 64 bit numbers are stored as two 32 bit numbers, the low part
 * first.
 */
static void
store_gcov_u64(u64 v)
{
  store_gcov_u32(v & 0xffffffffUL);
  store_gcov_u32(v >> 32);
}

/**
 * convert_to_gcda - convert profiling data set to gcda file format
 * @info: profiling data set to be converted
 */
void
convert_to_gcda(struct gcov_info *info)
{
  struct gcov_fn_info *fi_ptr;
  struct gcov_ctr_info *ci_ptr;
  unsigned int fi_idx;
  unsigned int ct_idx;
  unsigned int cv_idx;

  /* File header. */
  store_gcov_u32(GCOV_DATA_MAGIC);
  store_gcov_u32(info->version);
  store_gcov_u32(info->stamp);

#if (__GNUC__ >= 12)
  /* Use zero as checksum of the compilation unit. */
  store_gcov_u32(0);
#endif

  for (fi_idx = 0; fi_idx < info->n_functions; fi_idx++)
    {
      fi_ptr = info->functions[fi_idx];

      /* Function record. */
      store_gcov_u32(GCOV_TAG_FUNCTION);
      store_gcov_u32(GCOV_TAG_FUNCTION_LENGTH * GCOV_UNIT_SIZE);
      store_gcov_u32(fi_ptr->ident);
      store_gcov_u32(fi_ptr->lineno_checksum);
      store_gcov_u32(fi_ptr->cfg_checksum);

      ci_ptr = fi_ptr->ctrs;

      for (ct_idx = 0; ct_idx < GCOV_COUNTERS; ct_idx++)
        {
          if (!counter_active(info, ct_idx))
            continue;

          /* Counter record. */
          store_gcov_u32(GCOV_TAG_FOR_COUNTER(ct_idx));
          store_gcov_u32(ci_ptr->num * 2 * GCOV_UNIT_SIZE);

          for (cv_idx = 0; cv_idx < ci_ptr->num; cv_idx++)
            store_gcov_u64(ci_ptr->values[cv_idx]);
          ci_ptr++;
        }
    }
}

struct gcov_master
{
  struct gcov_info *list_start;
  struct gcov_info *cur;
  unsigned dumped;
};

void
__gcov_exit(void);
void
__gcov_init(struct gcov_info *info);
void
__gcov_merge_add(gcov_type *counters, unsigned int n_counters);
int
__gcov_execve(const char *filename, char *const argv[], char *const envp[]);
pid_t
__gcov_fork(void);
int
__gcov_execvp(const char *file, char *const argv[]);
int
__gcov_execv(const char *file, char *const argv[]);
void
gcov_print(void);
int
gcov_exit(void);

struct gcov_master __gcov_master = {NULL, NULL, 0};

void
gcov_print()
{
  if (__gcov_master.dumped)
    return;

  output_gcov_data(__gcov_master.list_start);

  __gcov_master.dumped = 1;
}

int __attribute__((destructor)) gcov_exit()
{
  gcov_print();
  return 0;
}

void
__gcov_exit()
{
  // This is called multiple times, we don't want that.
}

void
__gcov_init(struct gcov_info *info)
{
  if (!info || !info->version || !info->n_functions)
    return;

  if (!__gcov_master.list_start)
    __gcov_master.list_start = info;
  else
    __gcov_master.cur->next = info;

  __gcov_master.cur    = info;
  __gcov_master.dumped = 0;
  info->next           = NULL;
}

void
__gcov_merge_add(gcov_type *counters, unsigned int n_counters)
{
  (void)counters;
  (void)n_counters;
  vconprint("GCOV: ERROR __gcov_merge_add called unexpectedly\n");
}

pid_t
__gcov_fork(void)
{
  vconprint("GCOV: ERROR __gcov_fork called unexpectedly\n");
  return 0;
}

int
__gcov_execv(const char *file, char *const argv[])
{
  (void)file;
  (void)argv;
  vconprint("GCOV: ERROR __gcov_execv called unexpectedly\n");
  return 0;
}

int
__gcov_execve(const char *file, char *const argv[], char *const envp[])
{
  (void)file;
  (void)argv;
  (void)envp;
  vconprint("GCOV: ERROR __gcov_execve called unexpectedly\n");
  return 0;
}

int
__gcov_execvp(const char *file, char *const argv[])
{
  (void)file;
  (void)argv;
  vconprint("GCOV: ERROR __gcov_execvp called unexpectedly\n");
  return 0;
}
