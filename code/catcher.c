// =============================================================================
/*******************************************************************************
 * System Call Catcher
 *
 * Forks and runs itself as parent and the benchmark with the manager as the
 * child Child is stopped every time it enters or exits a system call, where we
 * can examine or change the registers of the child Information on all the system
 * calls is stored inside the table.
 *
 * Current buggs/issues:
 * - Does not work on multithreaded programs yet.
 * - I have not implemented `mmap` and `break` outcall handling.
 *
 * @author Luka Duranovic
 * @date   Monday, November 22, 2021
 ******************************************************************************/
// =============================================================================



// =============================================================================
// INCLUDES

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <sys/user.h>
// =============================================================================



// =============================================================================
// SYSTEM CALL TABLE

/**
 * We are tracking what to do with each system call.
 * Possible values:
 *   - `-1` means string.
 *   - `0` means not a string or a struct.
 *   - `>1` is a struct, and the value represents the size of the struct.
 * Values with names need to be replaced with sizes.
 * Information found on "https://filippo.io/linux-syscall-table/".
 *
 * NOTE: Have to figure out what to do with `void` pointers.
 * TODO: Values with names need to be replaced with sizes.
 */
static const int table[314][6] = {

	// 1. `read`
	{0,-1,0,0,0,0},

	// 2. `write`
	{0,-1,0,0,0,0},

	// 3. `open`
	{-1,0,0,0,0,0},

	// 4. `close`
	{0,0,0,0,0,0},
	{-1,-1,0,0,0,0},//        5. stat
	{0,-1,0,0,0,0},//         6. fstat
	{-1,-1,0,0,0,0},//        7. lstat
	{1/*sizeof(pollfd__user)*/,0,0,0,0,0},//poll
	{0,0,0,0,0,0},//lseek
	{0,0,0,0,0,0},//mmap
	{0,0,0,0,0,0},//mprotect
	{0,0,0,0,0,0},//munmap
	{0,0,0,0,0,0},//brk
	{0,1,1/*sizeof(sigaction__user),sizeof(sigaction__user)*/,0,0,0},//rt_sigaction
	{0,1,1/*sizeof(sigset_t__user),sizeof(sigset_t__user)*/,0,0,0},//rt_sigprocmask
	{0,0,0,0,0,0},//rt_sigreturn
	{0,0,0,0,0,0},//ioctl
	{0,-1,0,0,0,0},//pread64
	{0,-1,0,0,0,0},//pwrite64
	{0,1/*sizeof(iovec__user)*/,0,0,0,0},//readv
	{0,1/*sizeof(iovec__user)*/,0,0,0,0},//writev
	{-1,0,0,0,0,0},//access
	{1/*int__user*/,0,0,0,0,0},//pipe
	{0,1/*sizeof(fd_set__user)*/,1/*same*/,1/*same*/,1/*sizeof(timeval_user)*/,0},//select
	{0,0,0,0,0,0},//sched_yield
	{0,0,0,0,0,0},//mremap
	{0,0,0,0,0,0},//msync
	{0,0,1/*sizeof(unsigned char__user)*/,0,0,0},//mincore
	{0,0,0,0,0,0},//madvise
	{0,0,0,0,0,0},//shmget
	{0,1/*sizeof(char__user)*/,0,0,0,0},//shmat
	{0,0,1/*sizeof(shmid_ds__user)*/,0,0,0},//shmctl
	{0,0,0,0,0,0},//dup
	{0,0,0,0,0,0},//dup2
	{0,0,0,0,0,0},//pause
	{1/*sizeof(timespec__user)*/,1/*same*/,0,0,0,0},//nanosleep
	{0,1/*sizeof(itimerval__user)*/,0,0,0,0},//getitimer
	{0,0,0,0,0,0},//alarm
	{0,1/*sizeof(itimerval__user)*/,1/*same*/,0,0,0},//setitimer
	{0,0,0,0,0,0},//getpid
	{0,0,1/*sizeof(loff_t__user)*/,0,0,0},//sendfile
	{0,0,0,0,0,0},//socket
	{0,1/*sizeof(sockaddr__user*)*/,0,0,0,0},//connect
	{0,1/*sizeof(sockaddr__user)*/,1/*sizeof(int__user*/,0,0,0},//accept
	{0,1/*sizeof(void__user)*/,0,0,1/*sizeof(sockaddr_user)*/,0},//sendto
	{0,1/*sizeof(void__user)*/,0,0,1/*sizeof(sockaddr__user)*/,1/*sizeof(int__user)*/},//recvfrom
	{0,1/*sizeof(msghdr__user)*/,0,0,0,0},//sendmsg
	{0,1/*sizeof(msghdr__user)*/,0,0,0,0},//recvmsg
	{0,0,0,0,0,0},//shutdown
	{0,1/*sizeof(sockaddr__user)*/,0,0,0,0},//bind
	{0,0,0,0,0,0},//listen
	{0,1/*sizeof(sockaddr__user)*/,1/*sizeof(int__user)*/,0,0,0},//getsockname
	{0,1/*sizeof(sockaddr__user)*/,1/*sizeof(int__user)*/,0,0,0},//getpeername
	{0,0,0,1/*sizeof(int__user)*/,0,0},//socketpair
	{0,0,0,-1,0,0},//setsockopt
	{0,0,0,-1,1/*sizeof(int_user)*/,0},//getsockopt
	{0,0,0,1/*int__user*/,1/*int__user*/,0},//clone
	{0,0,0,0,0,0},//fork
	{0,0,0,0,0,0},//vfork
	{-1,1/*help*/,1/*help*/,0,0,0},//execv
	{0,0,0,0,0,0},//exit
	{0,1/*int__user*/,0,1/*rusage__user*/,0,0},//wait4
	{0,0,0,0,0,0},//kill
	{1/*new_utsname__user*/,0,0,0,0,0},//uname
	{0,0,0,0,0,0},//semget
	{0,1/*sembuf__user*/,0,0,0,0},//semop
	{0,0,0,0,0,0},//semctl
	{-1,0,0,0,0,0},//shmdt
	{0,0,0,0,0,0},//msgget
	{0,1/*msgbuf__user*/,0,0,0,0},//msgsnd
	{0,1/*msgbuf__user*/,0,0,0,0},//msgrcv
	{0,0,1/*msqid_ds__user*/,0,0,0},//msgctl
	{0,0,0,0,0,0},//fcntl
	{0,0,0,0,0,0},//flock
	{0,0,0,0,0,0},//fsync
	{0,0,0,0,0,0},//fdatasync
	{-1,0,0,0,0,0},//truncate
	{0,0,0,0,0,0},//ftruncate
	{0,1/*linux_dirent__user*/,0,0,0,0},//getdents
	{-1,0,0,0,0,0},//getcwd
	{-1,0,0,0,0,0},//chdir
	{0,0,0,0,0,0},//fchdir
	{-1,-1,0,0,0,0},//rename
	{-1,0,0,0,0,0},//mkdir
	{-1,0,0,0,0,0},//rmdir
	{-1,0,0,0,0,0},//creat
	{-1,-1,0,0,0,0},//link
	{-1,0,0,0,0,0},//unlink
	{-1,-1,0,0,0,0},//symlink
	{-1,-1,0,0,0,0},//readlink
	{-1,0,0,0,0,0},//chmod
	{0,0,0,0,0,0},//fchmod
	{-1,0,0,0,0,0},//chown
	{0,0,0,0,0,0},//fchown
	{-1,0,0,0,0,0},//lchown
	{0,0,0,0,0,0},//umask
	{1/*timeval__user*/,1/*timexone__user*/,0,0,0,0},//gettimeofday
	{0,1/*rlimit__user*/,0,0,0,0},//getrlimit
	{0,1/*rusage__user*/,0,0,0,0},//getrusage
	{1/*sysinfo__user*/,0,0,0,0,0},//sysinfo
	{1/*tms__user*/,0,0,0,0,0},//times
	{0,0,0,0,0,0},//ptrace
	{0,0,0,0,0,0},//getuid
	{0,0,-1,0,0,0},//syslog
	{0,0,0,0,0,0},//getgid
	{0,0,0,0,0,0},//setuid
	{0,0,0,0,0,0},//setgid
	{0,0,0,0,0,0},//geteuid
	{0,0,0,0,0,0},//getegid
	{0,0,0,0,0,0},//setpgid
	{0,0,0,0,0,0},//getppid
	{0,0,0,0,0,0},//getpgrp
	{0,0,0,0,0,0},//setsid
	{0,0,0,0,0,0},//setreuid
	{0,0,0,0,0,0},//setregid
	{0,1/*gid_t__user*/,0,0,0,0},//getgroups
	{0,1/*gid_t__user*/,0,0,0,0},//setgroups
	{0,0,0,0,0,0},//setresuid
	{1/*uid_t__user*/,1/*same*/,1/*same*/,0,0,0},//getresuid
	{0,0,0,0,0,0},//setresgid
	{1/*gid_t__user*/,1/*same*/,1/*same*/,0,0,0},//getresgid
	{0,0,0,0,0,0},//getpgid
	{0,0,0,0,0,0},//setfsuid
	{0,0,0,0,0,0},//setfsgid
	{0,0,0,0,0,0},//getsid
	{0,0,0,0,0,0},//capget
	{0,0,0,0,0,0},//capset
	{1/*sigset_t__user*/,0,0,0,0,0},//rt_sigpending
	{1/*sigset_t__user*/,1/*siginfo_r__user*/,1/*timespec__user*/,0,0,0},//sigtimedwait
	{0,0,1/*siginfo_r__user*/,0,0,0},//trsigqueueinfo
	{1/*sigset_t__user*/,0,0,0,0,0},//rt_sigsuspend
	{1/*stack_t__user*/,1/*stack_t__user*/,0,0,0,0},//sigaltstack
	{-1,1/*utimbuf__user*/,0,0,0,0},//utime
	{-1,0,0,0,0,0},//mknod
	{-1,0,0,0,0,0},//uselib
	{0,0,0,0,0,0},//personality
	{0,1/*ustat__user*/,0,0,0,0},//ustat
	{-1,1/*statfs__user*/,0,0,0,0},//statfs
	{0,1/*statfs_user*/,0,0,0,0},//fstatfs
	{0,0,0,0,0,0},//sysfs
	{0,0,0,0,0,0},//getpriority
	{0,0,0,0,0,0},//setpriority
	{0,1/*sched_param__user*/,0,0,0,0},//sched_getparam
	{0,1/*sched_param__user*/,0,0,0,0},//sched_getparam
	{0,0,1/*sched_param__user*/,0,0,0},//sched_setscheduler
	{0,0,0,0,0,0},//sched_getscheduler
	{0,0,0,0,0,0},//sched_get_priority_max
	{0,0,0,0,0,0},//sched_get_priority_min
	{0,1/*timespec__user*/,0,0,0,0},//sched_rr_get_interval
	{0,0,0,0,0,0},//mlock
	{0,0,0,0,0,0},//munlock
	{0,0,0,0,0,0},//mlockall
	{0,0,0,0,0,0},//munlockall
	{0,0,0,0,0,0},//vhangup
	{0,1/*help*/,0,0,0,0},//modify_ldt
	{-1,-1,0,0,0,0},//pivot_root
	{1/*__sysctl_args__user*/,0,0,0,0,0},//_sysctl
	{0,0,0,0,0,0},//prctl
	{1/*task_struct*/,0,1/*unsigned long__user*/,0,0,0},//arch_prctl
	{1/*timex__user*/,0,0,0,0,0},//adjtimex
	{0,1/*rlimit__user*/,0,0,0,0},//setrlimit
	{-1,0,0,0,0,0},//chroot
	{0,0,0,0,0,0},//sync
	{-1,0,0,0,0,0},//acct
	{1/*timeval__user*/,1/*timezone__user*/,0,0,0,0},//settimeofday
	{-1,-1,-1,0,1/*help*/,0},//mount
	{-1,0,0,0,0,0},//umount2
	{-1,0,0,0,0,0},//swapon
	{-1,0,0,0,0,0},//swapoff
	{0,0,0,0,0,1/*help*/},//reboot
	{-1,0,0,0,0,0},//sethostname
	{-1,0,0,0,0,0},//setdomainname
	{0,0,0,0,0,0},//iopl
	{0,0,0,0,0,0},//ioperm
	{0,0,0,0,0,0},//create_module NI
	{1/*help*/,0,-1,0,0,0},//init_module
	{-1,0,0,0,0,0},//delete_module
	{0,0,0,0,0,0},//get_kernel_syms NI
	{0,0,0,0,0,0},//query_module NI
	{0,-1,0,1/*help*/,0,0},//quotactl
	{0,0,0,0,0,0},//nfsservctl NI
	{0,0,0,0,0,0},//getpmsg NI
	{0,0,0,0,0,0},//afs_syscall NI
	{0,0,0,0,0,0},//putpmsg NI
	{0,0,0,0,0,0},//tuxcall NI
	{0,0,0,0,0,0},//security NI
	{0,0,0,0,0,0},//gettid
	{0,0,0,0,0,0},//readahead
	{-1,-1,1/*help*/,0,0,0},//setxattr
	{-1,-1,1/*help*/,0,0,0},//lsetxattr
	{0,-1,1/*help*/,0,0,0},//fsetxattr
	{-1,-1,1/*help*/,0,0,0},//getxattr
	{-1,-1,1/*help*/,0,0,0},//lgetxattr
	{0,-1,1/*help*/,0,0,0},//fgetxattr
	{-1,-1,0,0,0,0},//listxattr
	{-1,-1,0,0,0,0},//llistxattr
	{0,-1,0,0,0,0},//flistxattr
	{-1,1,0,0,0,0},//removexattr
	{-1,-1,0,0,0,0},//lremovexattr
	{0,-1,0,0,0,0},//fremovexattr
	{0,0,0,0,0,0},//tkill
	{1/*time_t__user*/,0,0,0,0,0},//time
	{1/*u32__user*/,0,0,1/*timespec__user*/,1/*u32__user*/,0},//futex
	{0,0,1/*long__user*/,0,0,0},//sched_getaffinity
	{0,0,1/*long__user*/,0,0,0},//sched_setaffinity
	{1/*user_desc__user*/,0,0,0,0,0},//set_thread_area
	{0,1/*aio_context_t__user*/,0,0,0,0},//io_setup
	{0,0,0,0,0,0},//io_destroy
	{0,0,0,1/*io_event__user*/,1/*timespec__user*/,0},//io_getevents
	{0,0,1/*iocb__user*__user**/,0,0,0},//io_submit
	{0,1/*iocb__user*/,1/*io_event__user*/,0,0,0},//io_cancel
	{1/*user_desc__user*/,0,0,0,0,0},//get_thread_area
	{0,-1,0,0,0,0},//lookup_dcookie
	{0,0,0,0,0,0},//epoll_create
	{0,0,0,0,0,0},//epoll_ctl_old NI
	{0,0,0,0,0,0},//epoll_wait_old NI
	{0,0,0,0,0,0},//remap_file_pages
	{0,1/*linux_dirent64__user*/,0,0,0,0},//getdents64
	{1/*int__user*/,0,0,0,0,0},//set_tid_address
	{0,0,0,0,0,0},//restart_syscall
	{0,1/*sembuf__user*/,0,1/*timespec__user*/,0,0},//semtimedop
	{0,0,0,0,0,0},//fadvise64
	{0,1/*sigevent__user*/,1/*timer_t__user*/,0,0,0},//timer_create
	{0,0,1/*itimerspec__user*/,1/*itimerspec__user*/,0,0},//timer_settime
	{0,1/*itimerspec__user*/,0,0,0,0},//timer_gettime
	{0,0,0,0,0,0},//timer_getoverrun
	{0,0,0,0,0,0},//timer_delete
	{0,1/*timespec__user*/,0,0,0,0},//clock_settime
	{0,1/*timespec__user*/,0,0,0,0},//clock_gettime
	{0,1/*timespec__user*/,0,0,0,0},//clock_getres
	{0,0,1/*timespec__user*/,1/*timespec__user*/,0,0},//clock_nanosleep
	{0,0,0,0,0,0},//exit_group
	{0,1/*epoll_event__user*/,0,0,0,0},//epoll_wait
	{0,0,0,1/*epoll_event__user*/,0,0},//epoll_ctl
	{0,0,0,0,0,0},//tgkill
	{-1,1/*timeval__user*/,0,0,0,0},//utimes
	{0,0,0,0,0,0},//vserver NI
	{0,0,0,1/*long__user*/,0,0},//mbind
	{0,1/*long__user*/,0,0,0,0},//set_mempolicy
	{1/*int__user*/,1/*long__user*/,0,0,0,0},//get_mempolicy
	{-1,0,0,1/*mq_attr__user*/,0,0},//mq_open
	{-1,0,0,0,0,0},//mq_unlink
	{0,-1,0,0,1/*timespec__user*/,0},//mq_timesend
	{0,-1,0,1/*int__user*/,1/*timespec__user*/,0},//mq_timedreceive
	{0,1/*sigevent__user*/,0,0,0,0},//mq_notify
	{0,1/*mq_attr__user*/,1/*mq_attr__user*/,0,0,0},//mq_getsetattr
	{0,0,1/*kexec_segment__user*/,0,0,0},//kexec_load
	{0,0,1/*siginfo__user*/,0,1/*rusage__user*/,0},//waitid
	{-1,-1,1/*help*/,0,0,0},//add_key
	{-1,-1,-1,0,0,0},//request_key
	{0,0,0,0,0,0},//keyctl
	{0,0,0,0,0,0},//ioprio_get
	{0,0,0,0,0,0},//ionotify_init
	{0,-1,0,0,0,0},//ionotify_add_watch
	{0,0,0,0,0,0},//ionotify_rm_watch
	{0,0,0,0,0,0},//migrate_pages
	{0,0,1/*unsigned long__user*/,1/*unsigned long__user*/,0,0},
	{0,-1,0,0,0,0},//openat
	{0,-1,0,0,0,0},//mkdirat
	{0,-1,0,0,0,0},//mknodat
	{0,-1,0,0,0,0},//fchownat
	{0,-1,1/*timeval__user*/,0,0,0},//futimesat
	{0,-1,1/*stat__user*/,0,0,0},//newfstatat
	{0,-1,0,0,0,0},//unlinkat
	{0,-1,0,-1,0,0},//renameat
	{0,-1,0,-1,0,0},//linkat
	{-1,0,-1,0,0,0},//symlinkat
	{0,-1,-1,0,0,0},//readlinkat
	{0,-1,0,0,0,0},//fchmodat
	{0,-1,0,0,0,0},//faccesat
	{0,1/*fd_set__user*/,1/*fd_set__user*/,1/*fd_set__user*/,1/*timespec__user*/,1/*help*/},//pselect6
	{1/*pollfd__user*/,0,1/*timespec__user*/,1/*sigset_t__user*/,0,0},//ppoll
	{0,0,0,0,0,0},//unshare
	{1/*robust_list_head__user*/,0,0,0,0,0},//set_robust_list
	{0,1/*robust_list_head__user*__user**/,1/*size_t__user*/,0,0,0},//get_robust_list
	{0,1/*loff_t__user*/,0,1/*loff_t__user*/,0,0},//splice
	{0,0,0,0,0,0},//tee
	{0,0,0,0,0,0},//sync_file_range
	{0,1/*iover__user*/,0,0,0,0},//vmsplice
	{0,0,1/*help*/,1/*int__user*/,1/*int__user*/,0},//move_pages
	{0,-1,1/*timespec__user*/,0,0,0},//utimensat
	{0,1/*epoll_event__user*/,0,0,1/*sigset_t__user*/,0},//epoll_pwait
	{0,1/*sigset_t__user*/,0,0,0,0},//signalfd
	{0,0,0,0,0,0},//timerfd_create
	{0,0,0,0,0,0},//eventfd
	{0,0,0,0,0,0},//fallocate
	{0,0,1/*itimerspec__user*/,1/*itimer_spec__user*/,0,0},//timerfd_settime
	{0,1/*itimerspec__user*/,0,0,0,0},//timerfd_gettime
	{0,1/*sockaddr__user*/,1/*int__user*/,0,0,0},//accept4
	{0,1/*sigset_t__user*/,0,0,0,0},//signalfd4
	{0,0,0,0,0,0},//eventfd2
	{0,0,0,0,0,0},//epoll_create1
	{0,0,0,0,0,0},//dup3
	{1/*int__user*/,0,0,0,0,0},//pipe2
	{0,0,0,0,0,0},//inotify_init1
	{0,1/*iovec__user*/,0,0,0,0},//preadv
	{0,1/*iover__user*/,0,0,0,0},//pwritev
	{0,0,0,1/*siginfo_t__user*/,0,0},//rt_tgsigqueueinfo
	{1/*perf_event_attr__user*/,0,0,0,0,0},//perf_event_open
	{0,1/*mmsghdr__user*/,0,0,1/*timespec__user*/,0},//recvmmsg
	{0,0,0,0,0,0},//fanotify_init
	{0,0,0,0,-1,0},//fanotify_mark
	{0,0,0,0,1/*rlimit64__user*/,1/*rlimit64__user*/},//prlimit64
	{0,-1,1/*file_handle__user*/,1/*int__user*/,0,0},//name_to_handle_at
	{0,1/*file_handle__user*/,0,0,0,0},//open_by_handle_at
	{0,1/*timex__user*/,0,0,0,0},//clock_adjtime
	{0,0,0,0,0,0},//syncfs
	{0,1/*mmsghdr__user*/,0,0,0,0},//sendmmsg
	{0,0,0,0,0,0},//setns
	{1/*unsigned__user*/,1/*unsigned__user*/,1/*getcpu_cache__user*/,0,0,0},//getcpu
	{0,1/*iovec__user*/,0,1/*iovec__user*/,0,0},//process_vm_readv
	{0,1/*iovec__user*/,0,1/*iovec__user*/,0,0},//process_vm_writev
	{0,0,0,0,0,0},//kcmp
	{0,-1,0,0,0,0},//finit_module
};
// =============================================================================



// =============================================================================
// GLOBALS

// Shared space used to communicate with the child.
static int shared_fd;
static void *shared;

// Contains the address of functions in the child.
struct addr_info {

	void* walker;
	void* brk;
	void* mmap;
	void* munmap;
	void* mprotect;
	void* sigaction;

};

// Contains the data to share with the child.
struct walker_info {

	void* ptr;
	int length;

};

// Addresses of the walker and exit handler functions in the child.
static void *walk_struct_addr = NULL;
static void *brk_addr = NULL;
static void *mmap_addr = NULL;
static void *munmap_addr = NULL;
static void *mprotect_addr = NULL;
static void *sigaction_addr = NULL;
// =============================================================================



// =============================================================================
/**
 * Catches when the child sends the addresses of the walker and exit handlers.
 */
void handler (int signum) {

	if (signum == SIGUSR1) {

		// Interpret the shared space as `addr_info` struct and get all the fields.
		struct addr_info* info = (struct addr_info*) shared;
		walk_struct_addr = info->walker;
		brk_addr = info->brk;
		mmap_addr = info->mmap;
		munmap_addr = info->munmap;
		mprotect_addr = info->mprotect;
		sigaction_addr = info->sigaction;

	}

} // handler ()
// =============================================================================



// =============================================================================
/**
 * TODO: Write something meaningful here.
 */
int main (int argc, char * argv[]) {

	int status = 0;
	int counter = 0;
	int in_call = 0;
	pid_t pid;

	//used to get register values of the child function
	struct user_regs_struct regs;
	//stores the original registers when we change them
	struct user_regs_struct temp_regs;

	//catcher for when child sends over the addresses of its functions
	struct sigaction handle;
	handle.sa_handler = handler;
	sigaction(SIGUSR1, &handle, NULL);

	//forks to exec the traced program as a child process
	switch(pid = fork()){
		case -1:
			perror("fork");
			exit(1);
		case 0: //in the child process
			//runs the benchmark program using the manager
			ptrace(PTRACE_TRACEME, 0, NULL, NULL);
			putenv("LD_PRELOAD=./manager.so");
			execvp(argv[1], argv+1);
		default: //in the parent process
			//opens shared file
			shared_fd = open("shared.data", O_RDWR, S_IRWXU);
			shared = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, shared_fd, 0);

			wait(&status);
			//not 100% sure on what 1407 is, but it seems be the status set when the child system calls
			while(status == 1407) {
				//gets register values of child and puts them in regs
				ptrace(PTRACE_GETREGS, pid, NULL, &regs);

				//only stop for the entry of a syscall as ptrace_syscall stops for both
				//the in and out calls for a system call, in_call flips back forth
				if(in_call){
					printf("SystemCall %ld called with %ld, %ld, %ld\n",regs.orig_rax, regs.rdi, regs.rsi, regs.rdx);
					in_call=0;
					counter ++;

					//when the traced process is up and running if the manager signals
					//the catcher, we can intercept the kill system call and set it to a
					//different system call
					if (regs.orig_rax == 62 && walk_struct_addr != NULL && (pid_t)regs.rdi == getpid()) {
						//Catches when the child is done walking, hijacks the system call
						//to run the original call before walking
						if (regs.rsi == SIGUSR2) {
							ptrace(PTRACE_SETREGS, pid, NULL, &temp_regs);
						}

						//similar but for exit handlers, we only want to reset the
						//registers, not run the system call again
						else if (regs.rsi == SIGUSR1) {
							regs = temp_regs;
							regs.orig_rax = -1;

							ptrace(PTRACE_SETREGS, pid, NULL, &regs);
						}
					}

					//we don't care if main_orig in the tracee hasn't been called yet, we
					//track this by checking if the walker address is still NULL does not
					//capture write calls, as there is a wrapper function in the child
					//for that, see that for more information
					else if (regs.orig_rax != 1 && (table[(int) regs.orig_rax][0] != 0 || table[(int) regs.orig_rax][1] != 0 || table[(int) regs.orig_rax][2] != 0 || table[(int) regs.orig_rax][3] != 0 || table[(int) regs.orig_rax][4] != 0 || table[(int) regs.orig_rax][5] != 0) && walk_struct_addr != NULL) {

						//save the original registers to restore later
						temp_regs = regs;

						//likely does not have to be zeroed, but this is a safety precaution
						bzero(shared, 4096);
						struct walker_info* wi_ptr = (struct walker_info*) shared;
						int num_walks = 0;

						//for each arguement that needs to be walked, add it to the shared file so the child knows what to walk and how long
						if (table[(int) regs.orig_rax][0] != 1) {
							wi_ptr[num_walks].ptr = (void *)regs.rdi;
							wi_ptr[num_walks].length = table[(int) regs.orig_rax][0];
							num_walks++;
						}
						if (table[(int) regs.orig_rax][1] != 1) {
							wi_ptr[num_walks].ptr = (void *)regs.rsi;
							wi_ptr[num_walks].length = table[(int) regs.orig_rax][1];
							num_walks++;
						}
						if (table[(int) regs.orig_rax][2] != 1) {
							wi_ptr[num_walks].ptr = (void *)regs.rdx;
							wi_ptr[num_walks].length = table[(int) regs.orig_rax][2];
							num_walks++;
						}
						if (table[(int) regs.orig_rax][3] != 1) {
							wi_ptr[num_walks].ptr = (void *)regs.r10;
							wi_ptr[num_walks].length = table[(int) regs.orig_rax][3];
							num_walks++;
						}
						if (table[(int) regs.orig_rax][4] != 1) {
							wi_ptr[num_walks].ptr = (void *)regs.r8;
							wi_ptr[num_walks].length = table[(int) regs.orig_rax][4];
							num_walks++;
						}
						if (table[(int) regs.orig_rax][5] != 1) {
							wi_ptr[num_walks].ptr = (void *)regs.r9;
							wi_ptr[num_walks].length = table[(int) regs.orig_rax][5];
							num_walks++;
						}

						//set the system call to -1 so the call is nullified
						regs.orig_rax = -1;
						//set the instuction pointer to return to walker after nullified system call
						regs.rip = (unsigned long long int) walk_struct_addr;

						ptrace(PTRACE_SETREGS, pid, NULL, &regs);
					}
				}
				else {
					in_call = 1;
					//exit handlers for these system calls
					//TO DO:
					//fix this, does not work
					/*
						 if (sigaction_addr != NULL) {
						 if (regs.orig_rax == 9) {
						 printf("e\n");
						 regs.rip = (unsigned long long int) mmap_addr;
						 }
						 else if (regs.orig_rax == 10) {
						 regs.rip = (unsigned long long int) mprotect_addr;
						 }
						 else if (regs.orig_rax == 11) {
						 regs.rip = (unsigned long long int) munmap_addr;
						 }
						 else if (regs.orig_rax == 12) {
						 regs.rip = (unsigned long long int) brk_addr;
						 }
						 else if (regs.orig_rax == 12) {
						 regs.rip = (unsigned long long int) brk_addr;
						 }
						 else if (regs.orig_rax == 13) {
						 regs.rip = (unsigned long long int) sigaction_addr;
						 }
						 ptrace(PTRACE_SETREGS, pid, NULL, &regs);
						 }*/
				}

				//Restarts the child process, it will stop again on the in or out of a system call
				ptrace(PTRACE_SYSCALL, pid, NULL, NULL);
				wait(&status);
			}
			printf("Total Number of System Calls=%d\n", counter);
			return 0;
	}
} // main ()
// =============================================================================
