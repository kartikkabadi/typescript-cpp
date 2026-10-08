// w32compat.h — POSIX-shaped API over Win32 with Go-on-Windows semantics.
//
// On GOOS=windows the Go stdlib implements os.*, os.ReadDir, filepath.Abs,
// etc. over Win32 calls. The C++ port's files (osvfs, nativepath, osutil,
// the runner mains) are written against POSIX signatures; this header
// provides those signatures with the behavior Go's own Windows
// implementations have: Win32-error-derived errno values, FILE_ATTRIBUTE_*-
// derived st_mode bits, GetFinalPathNameByHandle realpath, FindFirstFile
// readdir, CreateProcess spawn.
//
// Include this INSTEAD of the POSIX headers on _WIN32. It defines the global
// names the ported code calls (::stat, ::lstat, ::open, ::readdir, ...) so
// call sites stay byte-identical to the Linux build.
#pragma once
#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// Suppress the CRT's non-standard POSIX names (open, read, write, close,
// dup, dup2, lseek, isatty, unlink, chmod, access, mkdir, rmdir, chdir,
// getcwd, getpid, pipe, filelength, ...): their int/unsigned signatures
// would create ambiguous overloads with the POSIX decls below.
#ifndef _CRT_DECLARE_NONSTDC_NAMES
#define _CRT_DECLARE_NONSTDC_NAMES 0
#endif
#include <windows.h>

// windows.h maps string-taking APIs onto A/W-suffixed macros; those macros
// also rewrite our own same-named methods (tsc::System::GetCurrentDirectory
// -> GetCurrentDirectoryA) producing cross-TU symbol mismatches — the
// defining .cpp never sees windows.h. All compat code calls the explicit
// *W entry points, so drop the aliases that collide.
#undef GetCurrentDirectory
#undef SetCurrentDirectory
#undef GetEnvironmentVariable
#undef SetEnvironmentVariable
#undef ExpandEnvironmentStrings

#include <errno.h>
#include <io.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <string>
#include <string_view>
#include <vector>

// We do NOT include <sys/stat.h>/<sys/types.h>: MSVC's sys/stat.h typedefs
// `struct stat` (=_stat64i32) and #defines stat/fstat to the _stat64 family,
// which would collide with the POSIX-shaped struct stat + ::stat/::fstat
// this header provides. Undef any such macros that leaked in.
#ifdef stat
#undef stat
#endif
#ifdef fstat
#undef fstat
#endif
#ifdef lstat
#undef lstat
#endif
#ifdef _stat64
#undef _stat64
#endif
#ifdef _stat64i32
#undef _stat64i32
#endif

// --- scalar types --------------------------------------------------------

#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef intptr_t ssize_t;
#endif
typedef int pid_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef unsigned int mode_t;
typedef unsigned int dev_t;
typedef short nlink_t;

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// --- errno model ---------------------------------------------------------
// Go on Windows: syscall.Errno holds raw Win32 codes for ENOENT
// (ERROR_FILE_NOT_FOUND) and ENOTDIR (ERROR_PATH_NOT_FOUND) plus invented
// APPLICATION_ERROR values for the rest, and Errno.Is() maps win32 codes to
// fs.ErrNotExist/ErrExist/ErrPermission. The C++ port's errnoError switch
// uses POSIX constants, so our wrappers map Win32 -> POSIX errno (preserving
// the Is() semantics) and record the raw Win32 code in TLS for faithful
// error text via w32::errnoText (FormatMessage — what Errno.Error() does).

#ifndef ENOTEMPTY
#define ENOTEMPTY 41
#endif
#ifndef ENAMETOOLONG_POSIX_OK
#endif

namespace w32 {

// Translate a Win32 error to the POSIX errno the ported code switches on,
// mirroring Go's Errno.Is(ErrNotExist/ErrExist/ErrPermission/ErrInvalid).
int errnoFromWin32(unsigned long win32err);
// Set errno + remember raw win32 code for errnoText. Returns -1 always.
int setErrFromWin32(unsigned long win32err);
// Same, with an explicit POSIX errno rather than errnoFromWin32's mapping
// (used when Go's code path prescribes the errno, e.g. kill -> ESRCH).
int setErrnoPair(int posixErr, unsigned long win32err);
// Raw win32 code behind the last failed compat call (0 if none recorded).
unsigned long lastWin32Error();
// Go syscall.Errno.Error(): FormatMessage text for the recorded code,
// strerror fallback for CRT-set errnos.
std::string errnoText(int e);
// FormatMessage text for a raw Win32 error code.
std::string win32Text(unsigned long win32err);

// UTF-8 <-> UTF-16 conversion (Go UTF16PtrFromString semantics: invalid
// sequences replaced with U+FFFD).
std::wstring widen(std::string_view s);
std::string narrow(std::wstring_view w);
// Convert a UTF-8 path (slash or backslash separated) to a Win32 path:
// normalizes '/' -> '\\' and applies the \\?\ long-path prefix when needed,
// matching Go's fixLongPath.
std::wstring winPath(std::string_view path);
// Fixup applied to paths returned by Win32 (GetFinalPathNameByHandle etc.):
// strip \\?\ prefix, UNC -> \\server\share, matching Go's postprocessing.
std::string fromWin32Path(std::wstring_view w);

// CreatePipe wrapper with Go/CRT semantics: both ends non-inheritable.
int makePipe(int fds[2]);
// Set/clear HANDLE_FLAG_INHERIT on the OS handle behind a CRT fd.
bool setFdInheritable(int fd, bool inheritable);
// Register an OS handle as a CRT fd (binary mode). -1 on failure.
int fdFromHandle(HANDLE h);
// OS handle behind a CRT fd.
HANDLE handleFromFd(int fd);

} // namespace w32

// --- errno constants (identity with MSVC errno.h preserved) --------------
// We do NOT redefine the errno constants: our wrappers set errno to POSIX
// values (ENOENT=2 etc. as MSVC defines them), and only w32::errnoText reads
// the raw win32 code. This keeps `errno == ENOENT` checks correct under both
// compat and CRT code paths.

// --- file status ---------------------------------------------------------
// struct stat mirrors the fields the port reads (st_mode/st_size/st_mtim/
// st_atim/st_ctim) plus Windows extras st_attr (FILE_ATTRIBUTE_*) and
// st_reparse_tag used by isReparsePoint-style checks.
struct w32_stat {
	dev_t st_dev;
	unsigned long long st_ino;
	mode_t st_mode;
	nlink_t st_nlink;
	uid_t st_uid;
	gid_t st_gid;
	dev_t st_rdev;
	long long st_size;
	struct timespec st_atim;
	struct timespec st_mtim;
	struct timespec st_ctim;
	// Windows extras (not read by mode-generic code):
	unsigned long st_attr;        // WIN32_FILE_ATTRIBUTE_DATA.dwFileAttributes
	unsigned long st_reparse_tag; // dwReserved0 when REPARSE_POINT
};

// `stat` maps onto our w32_stat: `struct stat`/`stat()`/`::stat` in ported
// code resolve to this struct + our stat() (MSVC's sys/stat.h is never
// included here — its `struct stat` and inline stat/fstat would clash).
#define stat w32_stat

#define S_IFMT 0170000
#define S_IFIFO 0010000
#define S_IFCHR 0020000
#define S_IFDIR 0040000
#define S_IFBLK 0060000
#define S_IFREG 0100000
#define S_IFLNK 0120000
#define S_IFSOCK 0140000
#define S_ISUID 0004000
#define S_ISGID 0002000
#define S_ISVTX 0001000

#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m) (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

// S_IIRREGULAR — marker bit (outside S_IFMT/0777 space) set on st_mode
// when Go's fileStat.mode() would OR in os.ModeIrregular: non-symlink
// reparse points (junctions, mount points, cloud placeholders...). It
// never collides with permission bits (<= 0777) or S_IFMT (0170000).
#define S_IIRREGULAR 0x10000

#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100
#define S_IRGRP 0040
#define S_IWGRP 0020
#define S_IXGRP 0010
#define S_IROTH 0004
#define S_IWOTH 0002
#define S_IXOTH 0001

// --- open flags ----------------------------------------------------------
// Values are our own; open() translates to Win32 dispositions/attributes.
#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_RDWR 0x0002
#define O_ACCMODE 0x0003
#define O_APPEND 0x0008
#define O_CREAT 0x0100
#define O_TRUNC 0x0200
#define O_EXCL 0x0400
#define O_BINARY 0x8000 // always binary; Go does no CRLF translation
#define O_TEXT 0x4000   // accepted, ignored (binary enforced)
#define O_NOINHERIT 0x20000
#define O_CLOEXEC 0x80000
#define O_NOFOLLOW 0x100000
#define O_DIRECTORY 0x200000
#define O_PATH 0x400000
#define O_SEQUENTIAL 0x800000
#define O_RANDOM 0x1000000
#define O_TEMPORARY 0x2000000
#define O_SHORT_LIVED 0x4000000
#define O_NONBLOCK 0x8000000 // unsupported on regular files; no-op

int open(const char* path, int oflag, ...);
int creat(const char* path, int mode);
int close(int fd);
ssize_t read(int fd, void* buf, size_t n);
ssize_t write(int fd, const void* buf, size_t n);
long long lseek(int fd, long long off, int whence);
int dup(int fd);
int dup2(int fd, int fd2);
int pipe(int fds[2]);
int isatty(int fd);
int fstat(int fd, struct stat* st);
int fsync(int fd);
int ftruncate(int fd, long long size);
namespace w32 { long long fileLength(int fd); }

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

// --- stat/lstat (Go os/stat_windows.go semantics) ------------------------
// stat follows name-surrogate reparse points (symlink/junction); lstat
// reports the link itself.
int stat(const char* path, struct stat* st);
int lstat(const char* path, struct stat* st);
int access(const char* path, int mode);
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
int chmod(const char* path, int mode);
int mkdir(const char* path, int mode);
int rmdir(const char* path);
int unlink(const char* path);
int rename(const char* oldp, const char* newp);
int symlink(const char* target, const char* linkpath);
ssize_t readlink(const char* path, char* buf, size_t bufsize);
char* realpath(const char* path, char* resolved);
// realpath as (native path, errno) — the nativepath.Realpath shape.
std::pair<std::string, int> realpath(const std::string& path);
int utimensat(int dirfd, const char* path, const struct timespec times[2],
              int flags);
#define AT_FDCWD -100
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_SYMLINK_FOLLOW 0x400
#define AT_REMOVEDIR 0x200
#define UTIME_NOW ((1l << 30) - 1l)
#define UTIME_OMIT ((1l << 30) - 2l)

// --- directory reading (Go os.ReadDir / readdirnames) --------------------
struct dirent {
	unsigned long long d_ino;
	unsigned short d_reclen;
	unsigned char d_type;
	char d_name[768]; // UTF-8; up to 255 UTF-16 units -> 765 UTF-8 bytes
};
#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12
#define DT_WHT 14

typedef struct DIR DIR;
DIR* opendir(const char* name);
DIR* fdopendir(int fd);
struct dirent* readdir(DIR* dirp);
void rewinddir(DIR* dirp);
int closedir(DIR* dirp);
long telldir(DIR* dirp);
void seekdir(DIR* dirp, long loc);

// --- cwd -----------------------------------------------------------------
// getcwd returns the process cwd in '/'-separated UTF-8 form? No — Go's
// os.Getwd returns the system form (backslashes on Windows); callers feed
// it through tspath::normalizePath. We return the UTF-8 GetCurrentDirectory
// result verbatim (e.g. "C:\work").
char* getcwd(char* buf, int size);
// getcwd as a std::string (native separators, "" on error).
std::string getcwdString();
int chdir(const char* path);
int rmdir_(const char*); // avoid MSVC macro oddities

// --- process -------------------------------------------------------------
pid_t getpid(void);
pid_t getppid(void);
uid_t getuid(void);
gid_t getgid(void);
unsigned int sleep(unsigned int seconds);
int usleep(unsigned long usec);
int nanosleep(const struct timespec* req, struct timespec* rem);
int clock_gettime(int clockid, struct timespec* ts);
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID 3

// --- signals -------------------------------------------------------------
// CRT signal() supports SIGINT/SIGTERM/SIGABRT/SIGFPE/SIGILL/SIGSEGV only.
// We interpose so POSIX-only signals used by the port (SIGALRM for the
// runner timeouts, SIGKILL/SIGPIPE sent to children) still work: unsupported
// signals get a handler-table entry driven by w32::raiseImpl (which alarm()
// and kill() funnel through).
#ifndef SIGHUP
#define SIGHUP 1
#endif
#ifndef SIGQUIT
#define SIGQUIT 3
#endif
#ifndef SIGKILL
#define SIGKILL 9
#endif
#ifndef SIGALRM
#define SIGALRM 14
#endif
#ifndef SIGUSR1
#define SIGUSR1 16
#endif
#ifndef SIGUSR2
#define SIGUSR2 17
#endif
#ifndef SIGCHLD
#define SIGCHLD 18
#endif
#ifndef SIGCONT
#define SIGCONT 19
#endif
#ifndef SIGTSTP
#define SIGTSTP 20
#endif
#ifndef SIGPIPE
#define SIGPIPE 13 // Go windows has no SIGPIPE delivery; value unused
#endif
#ifndef SIGUSR10
#endif
#ifndef SIGWINCH
#define SIGWINCH 28
#endif
#ifndef SIGBREAK
#define SIGBREAK 21
#endif
#ifndef SIGTRAP
#define SIGTRAP 5
#endif

namespace w32 {
using SigHandler = void (*)(int);
SigHandler signalImpl(int sig, SigHandler handler);
int raiseImpl(int sig);
// Go has no alarm(); the ported runners use alarm()+SIGALRM as a test
// timeout. Implemented with a timer thread that raises SIGALRM.
unsigned int alarmImpl(unsigned int seconds);
// strsignal text matching Go's signal.String() names for diagnostics.
const char* strsignalImpl(int sig);
} // namespace w32

typedef unsigned long long sigset_t;
struct sigaction {
	void (*sa_handler)(int);
	void (*sa_sigaction)(int, void*, void*);
	sigset_t sa_mask;
	int sa_flags;
	void (*sa_restorer)(void);
};
int sigaction(int sig, const struct sigaction* act, struct sigaction* oldact);
int sigemptyset(sigset_t* set);
int sigfillset(sigset_t* set);
int sigaddset(sigset_t* set, int sig);
int sigdelset(sigset_t* set, int sig);
int sigismember(const sigset_t* set, int sig);
int sigprocmask(int how, const sigset_t* set, sigset_t* oldset);
int sigpending(sigset_t* set);
#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2
#define SA_RESTART 0x1
#define SA_NOCLDSTOP 0x2
#define SA_NODEFER 0x4
#define SA_RESETHAND 0x8
#define SA_SIGINFO 0x10

#define signal(sig, handler) w32::signalImpl((sig), (handler))
#define raise(sig) w32::raiseImpl((sig))
#define alarm(sec) w32::alarmImpl((sec))
#define strsignal(sig) w32::strsignalImpl((sig))

// --- ioctl ---------------------------------------------------------------
struct winsize {
	unsigned short ws_row;
	unsigned short ws_col;
	unsigned short ws_xpixel;
	unsigned short ws_ypixel;
};
int ioctl(int fd, unsigned long request, void* argp);
#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414
#define TIOCINQ 0x541B
#define FIONREAD 0x541B

// --- fcntl (minimal: only what survives in windows-compiled code) --------
int fcntl(int fd, int cmd, ...);
#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define F_DUPFD_CLOEXEC 0x1000
#define FD_CLOEXEC 1

// --- env / misc ----------------------------------------------------------
char* strdup_(const char* s); // not used; CRT _strdup exists
int setenv(const char* name, const char* value, int overwrite);
int unsetenv(const char* name);

// STD{IN,OUT,ERR}_FILENO exist via CRT (0,1,2).
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif

// waitpid/kill are implemented in w32proc.cpp but declared here so call
// sites keep their POSIX spelling.
#define WNOHANG 1
#define WUNTRACED 2
int w32_waitpid(pid_t pid, int* status, int options);
int w32_kill(pid_t pid, int sig);
// Status encoding matches glibc wait.h: normal exit -> (code<<8);
// signaled -> low 7 bits = signal number (+0x80 when core dumped).
#define WIFEXITED(s) (((s) & 0x7f) == 0)
#define WEXITSTATUS(s) (((s) >> 8) & 0xff)
#define WIFSIGNALED(s) (((s) & 0x7f) != 0)
#define WTERMSIG(s) ((s) & 0x7f)
#define WIFSTOPPED(s) 0
#define WSTOPSIG(s) 0
#define WCOREDUMP(s) (((s) & 0x80) != 0)
pid_t waitpid(pid_t pid, int* status, int options);
int kill(pid_t pid, int sig);
int killpg(pid_t pgid, int sig);

// execvp is never called on Windows (spawn sites are ifdef'd); declare to
// keep stray references compiling.
int execvp(const char* file, char* const argv[]);

namespace w32 {
// ---- process spawn/wait (CreateProcess port of fork+execvp) --------------
struct SpawnStdio {
	int stdinFd = -2;  // -2 = inherit parent's std handle
	int stdoutFd = -2;
	int stderrFd = -2;
	bool mergeStderrToStdout = false; // Go Cmd.Stderr = Cmd.Stdout
};
// argv[0] is the program name; it is resolved like Go's LookPath unless
// 'resolved' is true. Returns child pid (>0) whose HANDLE is registered for
// waitpid, or -1 with errno set.
pid_t spawnvp(const std::vector<std::string>& argv, const SpawnStdio& stdio,
              const std::string& cwd = {},
              const std::vector<std::string>& env = {});
// Path to the running executable (GetModuleFileNameW), '/'-normalized UTF-8.
std::string selfExePath();
// osutil.Executable on windows: GetModuleFileName UTF-8.
std::string executablePath();
// Go os.Process.Signal semantics: only Kill supported -> TerminateProcess.
// isProcessAlive mirrors isprocessalive_windows.go (SYNCHRONIZE + wait 0).
bool isProcessAlive(pid_t pid);
// Quote argv into one CreateProcess cmdline (Go appendEscapeArg).
std::string makeCmdLine(const std::vector<std::string>& argv);
// LookPath port for windows (LookExtensions + PATHEXT + findExecutable).
std::pair<std::string, int> lookPath(const std::string& file,
                                     const std::vector<std::string>& pathext);
// os.RemoveAll on windows: clears READONLY before delete, recurses dirs.
int removeAll(const std::string& path);
// Console init: enable ENABLE_VIRTUAL_TERMINAL_PROCESSING on std handles
// (enablevtprocessing_windows.go).
void enableVirtualTerminalProcessing();
// Put CRT stdio fds 0/1/2 in binary mode (Go does no CRLF translation).
void setBinaryStdio();
} // namespace w32

#endif // _WIN32
