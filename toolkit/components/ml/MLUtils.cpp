/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MLUtils.h"

#include <algorithm>
#include <cmath>
#include "prsystem.h"
#include "mozilla/Casting.h"
#include <sys/types.h>
#include "nsSystemInfo.h"

#if defined(XP_WIN)
#  include <sysinfoapi.h>
#endif
#if defined(XP_MACOSX)
#  include <sys/sysctl.h>
#  include <mach/mach.h>
#endif
#if defined(XP_LINUX)
#  include <sys/sysinfo.h>
#endif
#include "mozilla/SSE.h"

namespace mozilla::ml {

NS_IMPL_ISUPPORTS(MLUtils, nsIMLUtils)

NS_IMETHODIMP MLUtils::GetTotalPhysicalMemory(uint64_t* _retval) {
  *_retval = PR_GetPhysicalMemorySize();
  return NS_OK;
}

NS_IMETHODIMP MLUtils::GetAvailablePhysicalMemory(uint64_t* _retval) {
#if defined(XP_WIN)
  MEMORYSTATUSEX memStatus = {sizeof(memStatus)};

  if (GlobalMemoryStatusEx(&memStatus)) {
    *_retval = memStatus.ullAvailPhys;
  } else {
    return NS_ERROR_FAILURE;
  }
#endif

#if defined(XP_MACOSX)
  mach_port_t host_port = mach_host_self();
  vm_size_t page_size;
  vm_statistics64_data_t vm_stats;
  mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;

  if (host_page_size(host_port, &page_size) != KERN_SUCCESS ||
      host_statistics64(host_port, HOST_VM_INFO64,
                        reinterpret_cast<host_info64_t>(&vm_stats),
                        &count) != KERN_SUCCESS) {
    return NS_ERROR_FAILURE;
  }

  *_retval =
      static_cast<uint64_t>(vm_stats.free_count + vm_stats.inactive_count) *
      page_size;
#endif

#if defined(XP_LINUX)
  struct sysinfo memInfo{0};
  if (sysinfo(&memInfo) != 0) {
    return NS_ERROR_FAILURE;
  }
  *_retval = memInfo.freeram * memInfo.mem_unit;
#endif

  return NS_OK;
}

NS_IMETHODIMP MLUtils::GetOptimalCPUConcurrency(uint8_t* _retval) {
  ProcessInfo processInfo = {};
  if (!NS_SUCCEEDED(CollectProcessInfo(processInfo))) {
    return NS_ERROR_FAILURE;
  }

#if defined(ANDROID)
  // On android, "big" and "medium" cpus can be used.
  uint8_t cpuCount = processInfo.cpuPCount + processInfo.cpuMCount;
#else
#  ifdef __aarch64__
  // On aarch64 (like macBooks) we want to avoid efficient cores and stick with
  // "big" cpus.
  uint8_t cpuCount = processInfo.cpuPCount;
#  else
  // on x86_64 we're always using the number of physical cores.
  uint8_t cpuCount = processInfo.cpuCores;
#  endif
#endif

  *_retval = cpuCount;

  return NS_OK;
}

NS_IMETHODIMP MLUtils::CanUseLlamaCpp(bool* _retval) {
#ifdef __x86_64__
  *_retval = mozilla::supports_avx2();
#elif defined(__aarch64__)
  *_retval = true;
#else
  *_retval = false;
#endif

  return NS_OK;
}

}  // namespace mozilla::ml
