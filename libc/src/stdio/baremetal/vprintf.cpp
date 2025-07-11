//===-- Implementation of vprintf -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdio/vprintf.h"
#include "src/__support/OSUtil/io.h"
#include "src/__support/arg_list.h"
#include "src/__support/macros/config.h"
#include "src/stdio/printf_core/core_structs.h"
#include "src/stdio/printf_core/printf_main.h"
#include "src/stdio/printf_core/writer.h"

#include <stdarg.h>
#include <stddef.h>

namespace LIBC_NAMESPACE_DECL {

namespace {

LIBC_INLINE int stdout_write_hook(cpp::string_view new_str, void *) {
  write_to_stdout(new_str);
  return printf_core::WRITE_OK;
}

} // namespace

LLVM_LIBC_FUNCTION(int, vprintf,
                   (const char *__restrict format, va_list vlist)) {
  internal::ArgList args(vlist); // This holder class allows for easier copying
                                 // and pointer semantics, as well as handling
                                 // destruction automatically.
  static constexpr size_t BUFF_SIZE = 1024;
  char buffer[BUFF_SIZE];

  printf_core::WriteBuffer<printf_core::WriteMode::FLUSH_TO_STREAM> wb(
      buffer, BUFF_SIZE, &stdout_write_hook, nullptr);
  printf_core::Writer<printf_core::WriteMode::FLUSH_TO_STREAM> writer(wb);

  int retval = printf_core::printf_main(&writer, format, args);

  int flushval = wb.overflow_write("");
  if (flushval != printf_core::WRITE_OK)
    retval = flushval;

  return retval;
}

} // namespace LIBC_NAMESPACE_DECL
