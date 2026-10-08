// fileclock — std::chrono::file_clock::to_sys/from_sys shims.
//
// libstdc++/libc++ implement C++20 clock_cast helpers on
// std::chrono::file_clock; MSVC's STL exposes _File_time_clock without
// to_sys/from_sys (its file_time_type ticks are 100ns since the FILETIME
// epoch 1601-01-01 UTC). These wrappers keep call sites portable.

#pragma once

#include <chrono>
#include <filesystem>

namespace tsc {

inline std::chrono::system_clock::time_point fileClockToSys(
    const std::filesystem::file_time_type& tp) {
#ifdef _WIN32
	return std::chrono::system_clock::time_point(
	    std::chrono::duration_cast<std::chrono::system_clock::duration>(
	        tp.time_since_epoch()) - std::chrono::seconds{11644473600});
#else
	// libc++ (macOS) uses __int128 as file_clock's rep; the returned
	// sys_time needs an explicit duration_cast to system_clock's
	// long-long representation.
	return std::chrono::time_point_cast<std::chrono::system_clock::duration>(
	    std::chrono::file_clock::to_sys(tp));
#endif
}

inline std::filesystem::file_time_type fileClockFromSys(
    const std::chrono::system_clock::time_point& tp) {
#ifdef _WIN32
	return std::filesystem::file_time_type(
	    std::chrono::duration_cast<std::filesystem::file_time_type::duration>(
	        tp.time_since_epoch()) + std::chrono::seconds{11644473600});
#else
	return std::chrono::time_point_cast<
	    std::filesystem::file_time_type::duration>(
	    std::chrono::file_clock::from_sys(tp));
#endif
}

} // namespace tsc
