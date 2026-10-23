#ifndef CDBG_PLATFORM_TYPES_HPP
#define CDBG_PLATFORM_TYPES_HPP
#include <sys/types.h>
namespace cdbg::engine {
#if defined(__linux__)
using process_id_t = pid_t;
#elif defined(_WIN32)
using process_id_t = unsigned long;
#endif
} // namespace cdbg::engine
#endif // !CDBG_PLATFORM_TYPES_HPP
