/*

   nsjail - CPU affinity
   -----------------------------------------

   Copyright 2017 Google Inc. All Rights Reserved.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

*/

#include "cpu.h"

#include <inttypes.h>
#include <sched.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <memory>

#include "logs.h"
#include "util.h"

namespace cpu {

static std::string listCpusInSet(cpu_set_t* mask) {
	std::string ret = "[ ";
	for (size_t i = 0; i < CPU_SETSIZE; i++) {
		if (CPU_ISSET(i, mask)) {
			ret.append(std::to_string(i));
			ret.append(" ");
		}
	}
	ret.append("]");
	return ret;
}

static size_t getNthOnlineCpu(cpu_set_t* mask, size_t n) {
	for (size_t i = 0, j = 0; i < CPU_SETSIZE; i++) {
		if (CPU_ISSET(i, mask)) {
			if (j == n) {
				return i;
			}
			j++;
		}
	}
	LOG_F("No CPU #%zu found, yet there should be %zu left", n, (size_t)CPU_COUNT(mask));
	return 0;
}

static void setRandomCpu(cpu_set_t* original_mask, cpu_set_t* new_mask) {
	size_t cpus_left = CPU_COUNT(original_mask);
	if (cpus_left == 0) {
		LOG_F("There are no more CPUs left to use, and there should be at least 1 left");
	}

	size_t n = util::rnd64() % cpus_left;
	n = getNthOnlineCpu(original_mask, n);

	LOG_D(
	    "Setting allowed CPU#:%zu from the set of %s", n, listCpusInSet(original_mask).c_str());

	CPU_SET(n, new_mask);
	CPU_CLR(n, original_mask);
}

bool initCpu(nsjconf_t* nsjconf) {
	if (nsjconf->max_cpus == 0) {
		LOG_D("No max_cpus limit set");
		return true;
	}

	std::unique_ptr<cpu_set_t> original_mask(new cpu_set_t);
	if (!original_mask) {
		PLOG_W("Failure allocating cpu_set_t");
		return false;
	}
	if (sched_getaffinity(0, CPU_ALLOC_SIZE(CPU_SETSIZE), original_mask.get()) == -1) {
		PLOG_W("sched_getaffinity(0, mask_size=%zu)", (size_t)CPU_ALLOC_SIZE(CPU_SETSIZE));
		return false;
	}
	size_t available_cpus = CPU_COUNT(original_mask.get());

	LOG_D("Original CPU set: %s, with %zu allowed CPUs",
	    listCpusInSet(original_mask.get()).c_str(), available_cpus);

	if (nsjconf->max_cpus > available_cpus) {
		LOG_W(
		    "Number of requested CPUs is bigger than number of available CPUs (%zu > %zu)",
		    nsjconf->max_cpus, available_cpus);
		return true;
	}
	if (nsjconf->max_cpus == available_cpus) {
		LOG_D("All CPUs requested (%zu of %zu)", nsjconf->max_cpus, available_cpus);
		return true;
	}

	std::unique_ptr<cpu_set_t> new_mask(new cpu_set_t);
	if (!new_mask) {
		PLOG_W("Failure allocating cpu_set_t");
		return false;
	}
	CPU_ZERO(new_mask.get());

	for (size_t i = 0; i < nsjconf->max_cpus; i++) {
		setRandomCpu(original_mask.get(), new_mask.get());
	}

	LOG_D("Setting new CPU mask: %s with %zu allowed CPUs",
	    listCpusInSet(new_mask.get()).c_str(), (size_t)CPU_COUNT(new_mask.get()));

	if (sched_setaffinity(0, CPU_ALLOC_SIZE(CPU_SETSIZE), new_mask.get()) == -1) {
		PLOG_W("sched_setaffinity(0, mask_size=%zu, max_cpus=%zu) failed",
		    (size_t)CPU_ALLOC_SIZE(CPU_SETSIZE), nsjconf->max_cpus);
		return false;
	}

	return true;
}

}  // namespace cpu
