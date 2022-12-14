// SPDX-License-Identifier: Apache-2.0
#include "llvm/llvm.h"
#include <stdio.h>
#include <string.h> // strcmp
#include <err.h>

typedef bool(*linkerfn_t)(int argc, char*const* argv, bool can_exit_early);

static const char* prog; // main program name
CoLLVMOS host_os;

extern int clang_main(int argc, char*const* argv); // llvm/driver.cc
extern int main_build(int argc, char*const* argv); // build.c

static linkerfn_t nullable ld_impl(CoLLVMOS os);
static const char* ld_impl_name(linkerfn_t nullable f);


static void usage(FILE* f) {
  linkerfn_t ldf = ld_impl(host_os);
  char host_ld[128];
  if (ldf) {
    snprintf(host_ld, sizeof(host_ld),
      "  ld [args ...]        Linker for host system (%s)\n", ld_impl_name(ldf));
  } else {
    host_ld[0] = 0;
  }

  fprintf(f,
    "Compis, your friendly neighborhood programming language\n"
    "Usage: %s <command> [args ...]\n"
    "Commands:\n"
    "  build [args ...]     Compis compiler\n"
    "Extra commands:\n"
    "  cc [args ...]        Clang (C & C++ compiler)\n"
    "  as [args ...]        LLVM assembler (same as cc -cc1as)\n"
    "  ar [args ...]        Object archiver\n"
    "%s" // ld for host, if any
    "  ld-coff [args ...]   Linker for COFF\n"
    "  ld-elf [args ...]    Linker for ELF\n"
    "  ld-macho [args ...]  Linker for Mach-O\n"
    "  ld-wasm [args ...]   Linker for WebAssembly\n"
    "",
    prog,
    host_ld);
}

static int ar_main(int argc, char*const* argv) {
  // TODO: accept --target triple (where we really only parse the os)

  // const char* osname = (strlen(argv[0]) > 2) ? &argv[0][3] : "";
  // CoLLVMOS os = OSDarwin; // TODO: ifdef ... select host
  // if (strlen(osname) > 0) {
  //   os = OSTypeParse(osname);
  //   if (os == OSUnknown) {
  //     fprintf(stderr,
  //       "%s: unknown archiver %s;"
  //       " expected one of -ar, -ar.darwin, -ar.freebsd, -ar.ios, -ar.linux,"
  //       " -ar.macosx, -ar.openbsd, -ar.win32\n",
  //       parentprog, argv[0]);
  //     return 1;
  //   }
  // }
  // if (argc < 3) {
  //   log("usage: %s %s <archive> <file> ...", parentprog, argv[0]);
  //   return 1;
  // }

  const char* archivefile = argv[1];
  char*const* filesv = &argv[2];
  u32         filesc = argc-2;
  CoLLVMOS    os = host_os;
  char* errmsg = "?";

  bool ok = llvm_write_archive(archivefile, filesv, filesc, os, &errmsg);
  if (!ok) {
    warnx("ld: %s", errmsg);
    LLVMDisposeMessage(errmsg);
    return 1;
  }
  return 0;
}


static const char* ld_impl_name(linkerfn_t nullable f) {
  if (f == LLDLinkMachO) return "Mach-O";
  if (f == LLDLinkELF)   return "ELF";
  if (f == LLDLinkWasm)  return "WebAssembly";
  if (f == LLDLinkCOFF)  return "COFF";
  return "?";
}


static linkerfn_t nullable ld_impl(CoLLVMOS os) {
  switch (os) {
    case CoLLVMOS_Darwin:
    case CoLLVMOS_MacOSX:
    case CoLLVMOS_IOS:
    case CoLLVMOS_TvOS:
    case CoLLVMOS_WatchOS:
      return LLDLinkMachO;

    case CoLLVMOS_Win32:
      return LLDLinkCOFF;

    case CoLLVMOS_WASI:
    case CoLLVMOS_Emscripten:
      return LLDLinkWasm;

    // assume the rest uses ELF (this is probably not correct)
    case CoLLVMOS_Ananas:
    case CoLLVMOS_CloudABI:
    case CoLLVMOS_DragonFly:
    case CoLLVMOS_FreeBSD:
    case CoLLVMOS_Fuchsia:
    case CoLLVMOS_KFreeBSD:
    case CoLLVMOS_Linux:
    case CoLLVMOS_Lv2:
    case CoLLVMOS_NetBSD:
    case CoLLVMOS_OpenBSD:
    case CoLLVMOS_Solaris:
    case CoLLVMOS_Haiku:
    case CoLLVMOS_Minix:
    case CoLLVMOS_RTEMS:
    case CoLLVMOS_NaCl:
    case CoLLVMOS_AIX:
    case CoLLVMOS_CUDA:
    case CoLLVMOS_NVCL:
    case CoLLVMOS_AMDHSA:
    case CoLLVMOS_PS4:
    case CoLLVMOS_ELFIAMCU:
    case CoLLVMOS_Mesa3D:
    case CoLLVMOS_Contiki:
    case CoLLVMOS_AMDPAL:
    case CoLLVMOS_HermitCore:
    case CoLLVMOS_Hurd:
      return LLDLinkELF;

    default:
      return NULL;
  }
}


static int ld_main(int argc, char* argv[]) {
  linkerfn_t impl = ld_impl(host_os);
  if (!impl) {
    log("%s ld: unsupported host OS %s", prog, CoLLVMOS_name(host_os));
    return 1;
  }
  return !impl(argc, argv, true);
}


int main(int argc, char* argv[]) {
  prog = argv[0];

  const char* progname = strrchr(prog, '/');
  progname = progname ? progname + 1 : prog;
  bool is_multicall = strcmp(progname, "co") != 0;
  const char* cmd = is_multicall ? progname : argv[1] ? argv[1] : "";
  usize cmdlen = strlen(cmd);

  err_t err = llvm_init();
  if (err)
    errx(1, "llvm_init: %s", err_str(err));

  host_os = LLVMGetHostOS();

  #define ISCMD(s) (cmdlen == strlen(s) && memcmp(cmd, (s), cmdlen) == 0)

  // clang "cc" may spawn itself in a new process
  if (ISCMD("-cc1") || ISCMD("-cc1as"))
    return clang_main(argc, argv);

  if ISCMD("as") {
    argv[1] = "-cc1as";
    return clang_main(argc, argv);
  }

  // shave away "prog" from argv when not a multicall
  if (!is_multicall) {
    argc--;
    argv++;
  }

  // primary commands
  if ISCMD("build") return main_build(argc, argv);

  // llvm-based commands
  if ISCMD("cc")       return clang_main(argc, argv);
  if ISCMD("ar")       return ar_main(argc, argv);
  if ISCMD("ld")       return ld_main(argc, argv);
  if ISCMD("ld-macho") return LLDLinkMachO(argc, argv, true) ? 0 : 1;
  if ISCMD("ld-elf")   return LLDLinkELF(argc, argv, true) ? 0 : 1;
  if ISCMD("ld-coff")  return LLDLinkCOFF(argc, argv, true) ? 0 : 1;
  if ISCMD("ld-wasm")  return LLDLinkWasm(argc, argv, true) ? 0 : 1;

  if (cmdlen == 0) {
    log("%s: missing command (try %s -h)", prog, prog);
    return 1;
  }

  if (strstr(cmd, "-h") || strstr(cmd, "help")) {
    usage(stdout);
    return 0;
  }

  log("%s: unknown command \"%s\"", prog, cmd);
  return 1;
}
