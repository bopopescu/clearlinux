/*
 * This program is part of the Clear Linux Project
 *
 * Copyright 2015 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms and conditions of the GNU Lesser General Public License, as
 * published by the Free Software Foundation; either version 2.1 of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 */

#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <grp.h>
#include <inttypes.h>
#include <limits.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib.h>

#include <libelf.h>
#include <elfutils/libdwfl.h>

#include "config.h"
#include "log.h"
#include "telemetry.h"

static Dwfl *d_core = NULL;

/*
 * NOTE: if CRC32 validation should be enforced, change the first character to
 * '+' instead, which is the libdwfl default. Since the automatic debuginfo from
 * CLR usually reflects the latest release (within a 10-minute window), we will
 * certainly get crash reports from older releases, so try our best to get the
 * needed symbols or DWARF data from older packages instead of failing due to
 * validation problems...
 */
static char *debuginfo_path = "-/usr/lib/debug";

static unsigned int frame_counter = 0;
static gchar *proc_name = NULL;
static pid_t core_for_pid = 0;

static uint32_t severity = 2;
static uint32_t version = 1;
static char clr_class[30] = "org.clearlinux/crash/clr";
static char unknown_class[30] = "org.clearlinux/crash/unknown";

static const Dwfl_Callbacks cb =
{
        .find_elf = dwfl_build_id_find_elf,
        .find_debuginfo = dwfl_standard_find_debuginfo,
        .debuginfo_path = &debuginfo_path,
};

static inline void tm_elf_err(const char *msg)
{
        telem_log(LOG_ERR, "%s: %s\n", msg, elf_errmsg(-1));
}

static inline void tm_dwfl_err(const char *msg)
{
        telem_log(LOG_ERR, "%s: %s\n", msg, dwfl_errmsg(-1));
}

static void drop_privs(void)
{
        uid_t euid;

        euid = geteuid();
        if (euid != 0) {
                telem_log(LOG_DEBUG, "Not root; skipping privilege drop\n");
                return;
        }

        struct passwd *pw;

        pw = getpwnam("telemetry");
        if (!pw) {
                telem_log(LOG_ERR, "telemetry user not found\n");
                exit(EXIT_FAILURE);
        }

        // The order is important here:
        // change supplemental groups, our gid, and then our uid
        if (initgroups(pw->pw_name, pw->pw_gid) != 0) {
                telem_perror("Failed to set supplemental group list");
                exit(EXIT_FAILURE);
        }
        if (setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) != 0) {
                telem_perror("Failed to set GID");
                exit(EXIT_FAILURE);
        }
        if (setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid) != 0) {
                telem_perror("Failed to set UID");
                exit(EXIT_FAILURE);
        }

        assert(getuid() == pw->pw_uid);
        assert(geteuid() == pw->pw_uid);
        assert(getgid() == pw->pw_gid);
        assert(getegid() == pw->pw_gid);
}

static int temp_core_file(void)
{
        int tmp;
        ssize_t ret;

        char core[PATH_MAX] = "/tmp/corefile-XXXXXX";

        if ((tmp = mkstemp(core)) < 0) {
                telem_perror("Failed to create temp core file");
                return -1;
        }

#ifndef DEBUG
        if (unlink(core) < 0) {
                telem_perror("Failed to unlink temp core file");
                return -1;
        }
#endif

        while (true) {
                // Use Linux-specific splice(2) here;
                // simplifies copying data from pipe->file
                ret = splice(STDIN_FILENO, NULL, tmp, NULL, INT_MAX,
                             SPLICE_F_MORE | SPLICE_F_MOVE);

                if (ret > 0) {
                        // More data to read
                        continue;
                } else if (ret == 0) {
                        // End of data
                        break;
                } else if (ret < 0) {
                        telem_perror("Failed to splice data to core file");
                        return -1;
                }
        }

        return tmp;
}

/* This callback is invoked for every frame in a thread. From a Dwfl_Frame, we
 * are able to extract the program counter (PC), and from that, the procedure
 * name via a Dwfl_Module.
 */
static int frame_cb(Dwfl_Frame *frame, void *userdata)
{
        Dwarf_Addr pc;
        Dwarf_Addr pc_adjusted;
        Dwfl_Module *module;
        const char *procname;
        const char *modname;
        bool activation;
        GString **bt = (GString **)userdata;

        if (!dwfl_frame_pc(frame, &pc, &activation)) {
                tm_dwfl_err("Failed to find program counter for current frame");
                return DWARF_CB_ABORT;
        }

        // The return address may be beyond the calling address, putting the
        // current PC in a different context. Subtracting 1 from PC in this
        // case generally puts it back in the same context, thus fixing the
        // virtual unwind for this frame. See the DWARF standard for details.
        if (!activation) {
                pc_adjusted = pc - 1;
        } else {
                pc_adjusted = pc;
        }

        module = dwfl_addrmodule(d_core, pc_adjusted);

        if (!module) {
                // TODO: decide if it's worth creating a record in this
                // situation, since there are likely no symbols available for a
                // backtrace...
                telem_log(LOG_ERR,
                          "Failed to find module from dwfl_addrmodule"
                          " (process '%s', PID %u)\n",
                          proc_name, (unsigned int)core_for_pid);
                return DWARF_CB_ABORT;
        }

        modname = dwfl_module_info(module, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL);
        procname = dwfl_module_addrname(module, pc_adjusted);

        if (procname && modname) {
                g_string_append_printf(*bt, "#%u %s() - [%s]\n",
                                       frame_counter++, procname, modname);
        } else if (modname) {
                g_string_append_printf(*bt, "#%u ??? - [%s]\n",
                                       frame_counter++, modname);
        } else {
                // TODO: decide on "no symbol" representation
                g_string_append_printf(*bt, "#%u (no symbols)\n",
                                       frame_counter++);
        }

        return DWARF_CB_OK;
}

static int thread_cb(Dwfl_Thread *thread, void *userdata)
{
        int ret;
        GString **bt = (GString **)userdata;
        pid_t tid;

        tid = dwfl_thread_tid(thread);

        g_string_append_printf(*bt, "\nBacktrace (TID %u):\n",
                               (unsigned int)tid);

        ret = dwfl_thread_getframes(thread, frame_cb, userdata);

        switch (ret) {
                case -1:
                        tm_dwfl_err("Error while iterating through frames for"
                                    " current thread");
                        return DWARF_CB_ABORT;
                case DWARF_CB_ABORT:
                        return DWARF_CB_ABORT;
                case DWARF_CB_OK:
                        break;
                default:
                        telem_log(LOG_ERR, "Unrecognized return code\n");
                        return DWARF_CB_ABORT;
        }

        // New threads (if any), will require a fresh frame counter
        frame_counter = 0;

#if 0
        g_string_append_printf(*bt, "\nRegisters (TID %u):\nTODO\n", current,
                               (unsigned int)tid);
#endif

        return DWARF_CB_OK;
}

static bool send_data(GString **backtrace, char *class)
{
        struct telem_ref *handle = NULL;

        handle = tm_create_record(severity, class, version);

        if (!handle) {
                telem_log(LOG_ERR, "Failed to create record\n");
                return false;
        }

        gchar *bt = g_string_free(*backtrace, FALSE);

        if (!tm_set_payload(handle, (char *)bt)) {
                telem_log(LOG_ERR, "Failed to set payload\n");
                g_free(bt);
                return false;
        }

        g_free(bt);

        if (!tm_send_record(handle)) {
                telem_log(LOG_ERR, "Failed to send record\n");
                return false;
        }

        return true;
}

static bool filter_binaries(char *fullpath)
{
        // Anything outside of /usr/, or in /usr/local/, we consider third-party
        if (strncmp(fullpath, "!usr!", 5) != 0) {
                return false;
        } else if (strncmp(fullpath, "!usr!local!", 11) == 0) {
                return false;
        }

        return true;
}

static gchar *config_file = NULL;
static gchar *core_file = NULL;
static gchar *proc_path = NULL;
static gboolean version_p = FALSE;

static GOptionEntry options[] = {
        { "config-file", 'f', 0, G_OPTION_ARG_FILENAME, &config_file,
          "Path to configuration file (not implemented yet)", NULL },
        { "core-file", 'c', 0, G_OPTION_ARG_FILENAME, &core_file,
          "Path to core file to process", NULL },
        { "process-name", 'p', 0, G_OPTION_ARG_STRING, &proc_name,
          "Name of process for crash report (required)", NULL },
        { "process-path", 'E', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING,
          &proc_path, "Absolute path of crashed process, with ! delimiters"
          " (internal option)", NULL },
        { "version", 'V', 0, G_OPTION_ARG_NONE, &version_p,
          "Print the program version", NULL },
        { NULL }
};

static void free_glib_strings(void)
{
        if (core_file) {
                g_free(core_file);
        }
        if (proc_name) {
                g_free(proc_name);
        }
        if (proc_path) {
                g_free(proc_path);
        }
}

int main(int argc, char **argv)
{
        Elf *e_core = NULL;
        int core_modules = 0;
        int ret = EXIT_FAILURE;
        int core_fd = STDIN_FILENO;
        GString *backtrace = NULL;
        GError *error = NULL;
        GOptionContext *context;

        // Since this program handles core files, make sure it does not produce
        // core files itself, or else it will be invoked endlessly...
        prctl(PR_SET_DUMPABLE, 0);

        drop_privs();

        context = g_option_context_new("- collect data from core files");
        g_option_context_add_main_entries(context, options, NULL);
        g_option_context_set_translate_func(context, NULL, NULL, NULL);
        if (!g_option_context_parse(context, &argc, &argv, &error)) {
                g_print("Failed to parse options: %s\n", error->message);
                exit(EXIT_FAILURE);
        }

        if (version_p) {
                g_print(PACKAGE_VERSION "\n");
                exit(EXIT_SUCCESS);
        }

        if (!proc_name) {
                g_print("Missing required -p option. See --help output\n");
                exit(EXIT_FAILURE);
        }

        if (proc_path && !filter_binaries(proc_path)) {
                telem_log(LOG_NOTICE, "Ignoring core (third-party binary)\n");

                backtrace = g_string_new("Crash from third party\n");

                if (!send_data(&backtrace, unknown_class)) {
                        goto fail;
                }
                goto success;
        }

        struct stat sb;

        if (fstat(STDIN_FILENO, &sb) < 0) {
                telem_perror("Failed to stat stdin");
                goto fail;
        }

        /* Support core files on the filesystem, or over a pipe */
        if (S_ISREG(sb.st_mode)) {
                // Can read STDIN_FILENO directly, so fall through
        } else if (S_ISFIFO(sb.st_mode)) {
                // elf_begin() requires a seekable file, so dump core contents
                // to a temporary file and use it instead of stdin.
                core_fd = temp_core_file();

                if (core_fd == -1) {
                        goto fail;
                }
        } else if (core_file) {
                core_fd = open(core_file, O_RDONLY);
                if (core_fd == -1) {
                        telem_perror("Failed to open input core file");
                        goto fail;
                }
        } else {
                g_print("Cannot process core file. Use the -c option, or pass"
                        " the core file on stdin.\n");
                goto fail;
        }

        elf_version(EV_CURRENT);

        if (!(e_core = elf_begin(core_fd, ELF_C_READ, NULL))) {
                tm_elf_err("Failed to get file descriptor for ELF core file");
                goto fail;
        }

        if (!(d_core = dwfl_begin(&cb))) {
                tm_dwfl_err("Failed to start new libdwfl session");
                goto fail;
        }

        core_modules = dwfl_core_file_report(d_core, e_core, NULL);
        if (core_modules == -1) {
                tm_dwfl_err("Failed to report modules for ELF core file");
                goto fail;
        }

        if (dwfl_report_end(d_core, NULL, NULL) != 0) {
                tm_dwfl_err("Failed to finish reporting modules");
                goto fail;
        }

        if ((core_for_pid = dwfl_core_file_attach(d_core, e_core)) < 0) {
                tm_dwfl_err("Failed to prepare libdwfl session for thread"
                            " iteration");
                goto fail;
        }

        backtrace = g_string_new(NULL);
        g_string_printf(backtrace, "Crash Report ('%s', PID %u):\n", proc_name,
                        (unsigned int)core_for_pid);

        if (dwfl_getthreads(d_core, thread_cb, &backtrace) != DWARF_CB_OK) {
                goto fail;
        }

        if (!send_data(&backtrace, clr_class)) {
                goto fail;
        }

success:
        ret = EXIT_SUCCESS;
fail:
        free_glib_strings();

        if (context) {
                g_option_context_free(context);
        }

        if (backtrace) {
                g_string_free(backtrace, TRUE);
        }

        if (d_core) {
                dwfl_end(d_core);
        }

        if (e_core) {
                elf_end(e_core);
        }

        if (core_fd >= 0 && core_fd != STDIN_FILENO) {
                close(core_fd);
        }

        return ret;
}

/* vi: set ts=8 sw=8 sts=4 et tw=80 cino=(0: */
