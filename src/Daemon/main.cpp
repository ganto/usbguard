//
// Copyright (C) 2015 Red Hat, Inc.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// Authors: Daniel Kopecek <dkopecek@redhat.com>
//
#include <build-config.h>

#include <Typedefs.hpp>

#include "Logger.hpp"
#include "Daemon.hpp"
#include "Exception.hpp"
#include "Common/Utility.hpp"

#include <iostream>
#include <getopt.h>

#if defined(HAVE_SECCOMP)
# include <seccomp.h>
# include <errno.h>
# include <sys/resource.h>
# include <sys/socket.h>
# include <linux/netlink.h>
# include <sys/mman.h>
# if defined(HAVE_LIBCAPNG)
#  include <sys/prctl.h>
# endif
static void setupSeccompWhitelist(void);
#else
# warning "seccomp API not available. USBGuard daemon won't be able to setup a syscall whitelist!"
#endif
#if defined(HAVE_LIBCAPNG)
# include <cap-ng.h>
static void setupCapabilities(void);
#else
# warning "libcap-ng API not available. USBGuard daemon won't be able to drop capabilities!"
#endif

using namespace usbguard;

const char * const G_optstring = "dskl:p:c:hWC";

void printUsage(std::ostream& stream, const char *arg0)
{
  stream << std::endl;
  stream << "Usage: " << filenameFromPath(String(arg0), true) << " [OPTIONS]" << std::endl;
  stream << std::endl;
  stream << "  -d         Enable debugging messages in the log." << std::endl;
  stream << "  -s         Log to syslog." << std::endl;
  stream << "  -k         Log to console." << std::endl;
  stream << "  -l <path>  Log to a file at `path'." << std::endl;
  stream << "  -p <path>  Write PID to a file at `path'." << std::endl;
  stream << "  -c <path>  Load configuration from a file at `path'." << std::endl;
  stream << "             (default: /etc/usbguard/usbguard-daemon.conf)" << std::endl;
  stream << "  -C         Drop capabilities to limit privileges of the process." << std::endl;
  stream << "  -W         Use a seccomp whitelist to limit available syscalls to the process." << std::endl;
  stream << "  -h         Show this usage screen." << std::endl;
  stream << std::endl;
}

int main(int argc, char *argv[])
{
  const char * const arg0 = argv[0];
  bool debug_mode = false;
  bool log_syslog = false;
  bool log_console = false;
  bool log_file = false;
  bool use_seccomp_whitelist = false;
  bool drop_capabilities = false;
  String log_file_path;
  String pid_file;
  String conf_file = "/etc/usbguard/usbguard-daemon.conf";
  int opt;

  while ((opt = getopt(argc, argv, G_optstring)) != -1) {
    switch(opt)
      {
      case 'd':
	debug_mode = true;
	break;
      case 's':
	log_syslog = true;
	break;
      case 'k':
	log_console = true;
	break;
      case 'l':
	log_file = true;
	log_file_path = String(optarg);
	break;
      case 'p':
	pid_file = String(optarg);
	break;
      case 'c':
	conf_file = String(optarg);
	break;
      case 'W':
	use_seccomp_whitelist = true;
	break;
      case 'C':
	drop_capabilities = true;
	break;
      case 'h':
	printUsage(std::cout, arg0);
	return EXIT_SUCCESS;
      case '?':
	printUsage(std::cerr, arg0);
	/* FALLTROUGH */
      default:
	return EXIT_FAILURE;
      }
  }

  /* Initialize logging */
  USBGUARD_LOGGER.setEnabled(true, (debug_mode ?
                                    LogStream::Level::Trace
                                    :
                                    LogStream::Level::Warning));

  USBGUARD_LOGGER.setOutputConsole(log_console);
  USBGUARD_LOGGER.setOutputSyslog(log_syslog, "usbguard-daemon");
  USBGUARD_LOGGER.setOutputFile(log_file, log_file_path);

  /* Setup seccomp whitelist & drop capabilities */
  if (use_seccomp_whitelist) {
#if defined(HAVE_SECCOMP)
    setupSeccompWhitelist();
#else
    return EXIT_FAILURE;
#endif
  }

  if (drop_capabilities) {
#if defined(HAVE_LIBCAPNG)
    setupCapabilities();
#else
    return EXIT_FAILURE;
#endif
  }

  /* Start the daemon */
  int ret = EXIT_FAILURE;
  try {
    usbguard::Daemon daemon;
    if (!conf_file.empty()) {
      daemon.loadConfiguration(conf_file);
    }
    daemon.run();
    ret = EXIT_SUCCESS;
  }
  catch(const usbguard::Exception& ex) {
    USBGUARD_LOG(Error) << "ERROR: " << ex.message();
  }
  catch(const std::exception& ex) {
    USBGUARD_LOG(Error) << "EXCEPTION: " << ex.what();
  }
  catch(...) {
    USBGUARD_LOG(Error) << "EXCEPTION: Unknown exception caught while starting the process";
  }

  return ret;
}

#if defined(HAVE_SECCOMP)
 static void setupSeccompWhitelist(void)
 {

   /* TODO: Use SCMP_ACT_TRAP. Switch to EACCES for 1.x releases */
   scmp_filter_ctx ctx = seccomp_init(/*SCMP_ACT_ERRNO(EACCES)*/SCMP_ACT_TRAP);

   if (!ctx) {

     throw std::runtime_error("Cannot initialize seccomp filter context");
   }

   int ret = 0;

   /* files, dirs */
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(write), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(close), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(read), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(open), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(openat), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fstat), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(lstat), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fcntl), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(unlink), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(ftruncate), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(chown), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(chmod), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(lseek), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getdents), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(stat), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(readlink), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(access), 0);

   /* memory */
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(brk), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(mmap), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(munmap), 0);

   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(mprotect), 1,
			   SCMP_A2(SCMP_CMP_EQ, PROT_NONE));
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(mprotect), 1,
			   SCMP_A2(SCMP_CMP_EQ, PROT_READ));
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(mprotect), 1,
			   SCMP_A2(SCMP_CMP_EQ, PROT_WRITE));
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(mprotect), 1,
			   SCMP_A2(SCMP_CMP_EQ, PROT_READ|PROT_WRITE));

   /* clock */
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(clock_gettime), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(clock_getres), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(gettid), 0);

   /* epoll */
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(epoll_create1), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(epoll_wait), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(epoll_ctl), 0);

   /* signals */
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(rt_sigaction), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(rt_sigreturn), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(rt_sigprocmask), 0);

   /* process, thread */
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(clone), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(futex), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(exit_group), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(set_robust_list), 0);

   /* STRACE:
    *  getrlimit(RLIMIT_NOFILE, {rlim_cur=1024, rlim_max=4*1024}) = 0
    */
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getrlimit), 1,
			   SCMP_A0(SCMP_CMP_EQ, RLIMIT_NOFILE));

   /* pipes, eventfd */
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(pipe), 0);

   /* STRACE:
    *  eventfd2(0, 0)
    */
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(eventfd2), 2,
			   SCMP_A0(SCMP_CMP_EQ, 0),
			   SCMP_A1(SCMP_CMP_EQ, 0));

   /* socket */
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(socket), 1,
			   SCMP_A0(SCMP_CMP_EQ, PF_LOCAL),
			   SCMP_A1(SCMP_CMP_MASKED_EQ, SOCK_STREAM, SOCK_STREAM));

   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(socket), 1,
			   SCMP_A0(SCMP_CMP_EQ, PF_NETLINK),
			   SCMP_A2(SCMP_CMP_EQ, NETLINK_KOBJECT_UEVENT));

   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(bind), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(accept), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(listen), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(setsockopt), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(shutdown), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(recvmsg), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(recvfrom), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(sendto), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(select), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(connect), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getsockname), 0);

#if defined(HAVE_LIBCAPNG)
   /* capabilities */
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(capget), 0);
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(capset), 0);
   /* allow to drop capabilities using prctl */
   ret |= seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(prctl), 1,
			   SCMP_A0(SCMP_CMP_EQ, PR_CAPBSET_DROP));
#endif /* HAVE_LIBCAPNG */

   /* before main() only */
   //seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(statfs), 0);
   //seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(arch_prctl), 0);
   //seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(set_tid_address), 0);
   //seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(execve), 0);

   if (ret != 0) {

     throw std::runtime_error("Cannot initialize seccomp whitelist");
   }

   if (seccomp_load(ctx) != 0) {

     throw std::runtime_error("Cannot load seccomp whitelist into the kernel");
   }

   seccomp_release(ctx);
   return;
 }
#endif

#if defined(HAVE_LIBCAPNG)
 static void setupCapabilities(void)
 {

   capng_clear(CAPNG_SELECT_BOTH);
   capng_updatev(CAPNG_ADD, (capng_type_t)(CAPNG_EFFECTIVE|CAPNG_PERMITTED),
		 CAP_CHOWN, CAP_FOWNER,-1);
   capng_apply(CAPNG_SELECT_BOTH);
   return;
 }
#endif
