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
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/utsname.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <limits.h>
#include <inttypes.h>

#include "common.h"
#include "configuration.h"
#include "log.h"
#include "telemetry.h"

/**
 * Return a file descriptor to either site's version file
 * or the distributions version file, in that order of
 * preference.
 *
 * @return A file descriptor.
 *
 */
static int version_file(void)
{
        int fd;

        fd = open(TM_SITE_VERSION_FILE, O_RDONLY);
        if (fd < 0) {
                fd = open(TM_DIST_VERSION_FILE, O_RDONLY);
        }

        return fd;
}

/**
 * Helper function for the set_*_header functions that actually sets
 * the header and increments the header size. These headers become attributes
 * on an HTTP_POST transaction.
 *
 * @param dest The header string, which will be "prefix: dest\n"
 * @param prefix Identifies the header.
 * @param value The value of this particular header.
 * @param header_size Is incremented if header is successfully initialized.
 *
 * @return True if successful, False if not.
 *
 */
static bool set_header(char **dest, const char *prefix, char *value, size_t *header_size)
{
        int rc;

        rc = asprintf(dest, "%s: %s\n", prefix, value);

        if (rc < 0) {
                telem_log(LOG_CRIT, "Out of memory\n");
                return false;
        } else {
                *header_size += (size_t)rc;
                return true;
        }

}

/**
 * Sets the severity header.  Severity is clamped to a ranage of 1-4.
 *
 * @param t_ref Reference to a Telemetry record.
 * @param severity Severity of issue this telemetry record pertains to.
 *     Severity is clamped to a range of 1-4.
 *
 * @return True if successful, False if not.
 *
 */
static bool set_severity_header(struct telem_ref *t_ref, uint32_t severity)
{
        char *buf = NULL;
        bool status = false;
        int rc = 0;

        /* clamp severity to 1-4 */

        if (severity > 4) {
                severity = 4;
        }

        if (severity < 1) {
                severity = 1;
        }

        rc = asprintf (&buf, "%" PRIu32, severity);

        if (rc > 0) {
                status = set_header(&(t_ref->record->headers[TM_SEVERITY]),
                                    TM_SEVERITY_STR, buf, &(t_ref->record->header_size));
                free(buf);
        }

        return status;
}

/**
 * Sets the classification header.
 *
 * @param t_ref Reference to a Telemetry record.
 * @param classification Classification header is confirmed to
 *     to at least contain two / characters, and should be of the format
 *     <string>/<string>/<string>.
 *
 * @return True if successful, False if not.
 *
 */
static bool set_classification_header(struct telem_ref *t_ref, char *classification)
{
        size_t i, j;
        int slashes = 0;

        j = strlen(classification);

        for (i = 0; i <= (j - 1); i++) {
                if (classification[i] == '/') {
                        slashes++;
                }
        }

        if (slashes != 2) {
                telem_log(LOG_ERR, "Classification string should have two /s.");
                return false;
        }

        return set_header(&(t_ref->record->headers[TM_CLASSIFICATION]),
                          TM_CLASSIFICATION_STR, classification,
                          &(t_ref->record->header_size));

}

/**
 * Sets the record_format header.The record_format is a constant that
 * defined in common.h, and identifies the format/structure of the
 * record. The record_format is mainly for use by the back-end record
 * parsing.
 *
 * @param t_ref Reference to a Telemetry record.
 *
 * @return True if successful, False if not.
 *
 */
static bool set_record_format_header(struct telem_ref *t_ref)
{
        char *buf = NULL;
        int rc;
        bool status = false;

        rc = asprintf (&buf, "%" PRIu32, RECORD_FORMAT_VERSION);

        if (rc > 0) {
                status = set_header(&(t_ref->record->headers[TM_RECORD_VERSION]),
                                    TM_RECORD_VERSION_STR, buf,
                                    &(t_ref->record->header_size));

                free(buf);
        }

        return status;
}

/**
 * Sets the architecture header. Specifically the 'machine' field
 * of struct utsname, or 'unknown' if that field cannot be read.
 *
 * @param t_ref Telemetry Record reference obtained from tm_create_record.
 *
 * @return True if successful, False if not.
 *
 */
static bool set_arch_header(struct telem_ref *t_ref)
{
        struct utsname uts_buf;
        char *buf = NULL;
        int rc = 0;
        bool status = false;

        rc = uname(&uts_buf);

        if (rc < 0) {
                rc = asprintf(&buf, "unknown");
        } else {
                rc = asprintf(&buf, "%s", uts_buf.machine);
        }

        if (rc > 0) {
                status = set_header(&(t_ref->record->headers[TM_ARCH]), TM_ARCH_STR,
                                    buf, &(t_ref->record->header_size));

                free(buf);
        }

        return status;
}

/**
 * Sets the system name header. Specifically, what comes after 'ID=' in the
 * os-release file that lives in either /etc or the stateless dist-provided
 * location.
 *
 * @param t_ref Telemetry Record reference obtained from tm_create_record.
 *
 * @return True if successful, False if not.
 *
 */
static bool set_system_name_header(struct telem_ref *t_ref)
{
        FILE *fs = NULL;
        int fd;
        char buf[SMALL_LINE_BUF] = { 0 };
        char name[SMALL_LINE_BUF] = { 0 };

        fd = version_file();
        if (fd == -1) {
                telem_log(LOG_WARNING, "Cannot find os-release file\n");
                sprintf(name, "unknown");
        } else {
                fs = fdopen(fd, "r");
                while (fgets(buf, SMALL_LINE_BUF, fs)) {
                        if (sscanf(buf, "ID=%s", name) < 1) {
                                continue;
                        } else {
                                break;
                        }
                }

                if (strlen(name) == 0) {
                        telem_log(LOG_WARNING,
                                  "Cannot find os-release field: ID\n");
                        sprintf(name, "unknown");
                }

                fclose(fs);
        }

        return set_header(&(t_ref->record->headers[TM_SYSTEM_NAME]),
                          TM_SYSTEM_NAME_STR, name,
                          &(t_ref->record->header_size));

}

/**
 * Sets the header reporting the system build number for the OS.  This
 * comes from os-release on Clear Linux. On non-Clear systems this field
 * is probably not defined in the os-release file, in which case we report 0.
 *
 * @param t_ref Telemetry Record reference obtained from tm_create_record.
 *
 * @return True if successful, False if not.
 *
 */
static bool set_system_build_header(struct telem_ref *t_ref)
{
        FILE *fs = NULL;
        int fd;
        char buf[SMALL_LINE_BUF] = { 0 };
        char version[40] = { 0 };

        fd = version_file();
        if (fd == -1) {
                telem_log(LOG_WARNING, "Cannot find build version file\n");
                sprintf(version, "0");
        } else {
                fs = fdopen(fd, "r");
                while (fgets(buf, SMALL_LINE_BUF, fs)) {
                        if (sscanf(buf, "VERSION_ID=%s", version) < 1) {
                                continue;
                        } else {
                                break;
                        }
                }

                if (strlen(version) == 0) {
                        telem_log(LOG_WARNING, "Cannot find build version number\n");
                        sprintf(version, "0");
                }

                fclose(fs);
        }

        return set_header(&(t_ref->record->headers[TM_SYSTEM_BUILD]),
                          TM_SYSTEM_BUILD_STR, version, &(t_ref->record->header_size));

}

/**
 * Sets the header reporting the machine_id. Note, the machine_id is really
 * managed by the telemetrics daemon (telemd), and populated by the daemon
 * as it delivers the records. Consequently, the header is simply set to a
 * dummy value of 0xFFFFFFF.
 *
 * @param t_ref Telemetry Record reference obtained from tm_create_record.
 *
 * @return True if successful, False if not.
 *
 */
static bool set_machine_id_header(struct telem_ref *t_ref)
{
        uint64_t new_id = 0;
        int rc = 0;
        char *buf = NULL;
        bool status = false;

        new_id = 0xFFFFFFFF;

        rc = asprintf(&buf, "%zX", new_id);

        if (rc > 0) {
                status = set_header(&(t_ref->record->headers[TM_MACHINE_ID]),
                                    TM_MACHINE_ID_STR, buf, &(t_ref->record->header_size));

                free(buf);
        }

        return status;
}

/**
 * Sets the header reporting the timestamp for the creation of the telemetry
 * record.
 *
 * @param t_ref Telemetry Record reference obtained from tm_create_record.
 *
 * @return True if successful, False if not.
 *
 */
static bool set_timestamp_header(struct telem_ref *t_ref)
{

        time_t *t = NULL;
        char *buf;
        int rc = 0;
        bool status = false;

        rc = asprintf(&buf, "%zd", time(t));

        if (rc > 0) {
                status = set_header(&(t_ref->record->headers[TM_TIMESTAMP]),
                                    TM_TIMESTAMP_STR, buf, &(t_ref->record->header_size));
                free(buf);
        }

        return status;
}

/**
 * A healper function for set_host_type_header that reads the first line
 * out of a file and chomps the newline if necessary. If the file does
 * not exist the function returns "no_<key>_file". If the file exists but
 * contains only whitespace we return "blank". Otherwise the values read
 * are returned.  It is the caller's responsibility to free this memory later.
 *
 * @param source The file to be inspected.
 * @param key Shorthand notation for dmi value sought after. e.g.,
 * 'sv', 'pv', 'pvr'.
 *
 * @return String (char *) containing reported value.
 *
 */
static char *get_dmi_value(const char *source, const char *key)
{
        char *buf = NULL;
        char *line = NULL;
        FILE *dmifile;
        size_t size = 0;
        size_t new_size;

        buf = (char *)malloc(sizeof(char) * SMALL_LINE_BUF);

        if (buf == NULL) {
                telem_log(LOG_CRIT, "Out of memory\n");
                return NULL;
        }

        bzero(buf, SMALL_LINE_BUF);
        dmifile = fopen(source, "r");

        if (dmifile != NULL) {
                line = fgets(buf, SMALL_LINE_BUF - 1, dmifile);
                fclose (dmifile);

                /* zap the newline... */
                if (line != NULL) {
                        size = strlen(buf);
                        buf[size - 1] = '\0';
                }

                new_size = strlen(buf);

                /* The file existed but was empty... */
                if (new_size == 0) {

                        if (asprintf(&buf, "blank") < 0) {
                                telem_log(LOG_CRIT, "Out of memory\n");
                        }
                } else {
                        /* ...or it contained all spaces */
                        int j;
                        for (j = 0; j < new_size; ++j) {
                                if (buf[j] == ' ') {
                                        continue;
                                } else {
                                        break;
                                }
                        }
                        if (j == new_size) {
                                if (asprintf(&buf, "blank") < 0) {
                                        telem_log(LOG_CRIT, "Out of memory\n");
                                }
                        }
                }
        } else {
                telem_log(LOG_NOTICE, "Dmi file %s does not exist\n", source);

                if (asprintf(&buf, "no_%s_file", key) < 0) {
                        telem_log(LOG_CRIT, "Out of memory\n");
                }
        }

        return buf;
}

/**
 * Sets the hosttype header, which is a tuple of three values looked for
 * in the dmi filesystem.  System Vendor (sys_vendor), Product Name
 * (product_name), and Product Version (product_version). The values are
 * separated by pipes.  e.g., <sv|pn|pvr>
 *
 * @param t_ref Telemetry Record reference obtained from tm_create_record.
 *
 * @return True if successful, False if not.
 *
 */
static bool set_host_type_header(struct telem_ref *t_ref)
{
        char *buf = NULL;
        char *sv  = NULL;
        char *pn  = NULL;
        char *pvr = NULL;
        int rc = 0;
        bool status = false;

        sv  = get_dmi_value("/sys/class/dmi/id/sys_vendor", "sv");
        pn  = get_dmi_value("/sys/class/dmi/id/product_name", "pn");
        pvr = get_dmi_value("/sys/class/dmi/id/product_version", "pvr");

        rc = asprintf(&buf, "%s|%s|%s", sv, pn, pvr);

        if (rc > 0) {
                status = set_header(&(t_ref->record->headers[TM_HOST_TYPE]),
                                    TM_HOST_TYPE_STR, buf, &(t_ref->record->header_size));
                free(buf);
        }

        /* These should never be NULL by now, just being paranoid */
        if (sv != NULL) {
                free(sv);
        }

        if (pn != NULL) {
                free(pn);
        }

        if (pvr != NULL) {
                free(pvr);
        }

        return status;
}

/**
 * Sets the kernel version header, as reported by uname. If we cannnot
 * access the results of the uname system call, the function will gracefully
 * fail and report "unknown".
 *
 * @param t_ref Telemetry Record reference obtained from tm_create_record.
 *
 * @return True if successful, False if not.
 *
 */
static bool set_kernel_version_header(struct telem_ref *t_ref)
{
        struct utsname uts_buf;
        char *buf = NULL;
        int rc = 0;
        bool status = false;

        rc = uname(&uts_buf);

        if (rc < 0) {
                rc = asprintf(&buf, "unknown");
        } else {
                rc = asprintf(&buf, "%s", uts_buf.release);
        }

        if (rc > 0) {
                status = set_header(
                        &(t_ref->record->headers[TM_KERNEL_VERSION]),
                        TM_KERNEL_VERSION_STR, buf,
                        &(t_ref->record->header_size));
                free(buf);
        }

        return status;
}

/**
 * Sets the payload format header, which is a probe-specific version number
 * for the format of the payload. This is a hint to the back-end that parses records.
 *
 * @param t_ref Telemetry Record reference obtained from tm_create_record.
 * @param payload_version Version number provided by probe.
 *
 * @return True if successful, False if not.
 *
 */
static bool set_payload_format_header(struct telem_ref *t_ref, uint32_t payload_version)
{
        char *buf = NULL;
        int rc;
        bool status = false;

        rc = asprintf(&buf, "%" PRIu32, payload_version);

        if (rc > 0) {
                status = set_header(
                        &(t_ref->record->headers[TM_PAYLOAD_VERSION]),
                        TM_PAYLOAD_VERSION_STR, buf,
                        &(t_ref->record->header_size));
                free(buf);
        }

        return status;
}

void tm_set_config_file(char *c_file)
{
        set_config_file(c_file);
}

/**
 * Helper function for tm_create_record().  Allocate all of the headers
 * for a new telemetrics record. The parameters are passed through from
 * tm_create_record to this function.
 *
 * @param t_ref Telemetry Record reference obtained from tm_create_record.
 * @param severity Severity field value. Accepted values are in the range 1-4,
 *     with 1 being the lowest severity, and 4 being the highest severity.
 * @param classification Classification field value. It should have the form
 *     DOMAIN/PROBENAME/REST: DOMAIN is the reverse domain to use as a namespace
 *     for the probe (e.g. org.clearlinux); PROBENAME is the name of the probe;
 *     and REST is an arbitrary value that the probe should use to classify the
 *     record.
 * @param payload_version Payload format version. The only supported value right
 *     now is 1, which indicates that the payload is a freely-formatted
 *     (unstructured) string. Values greater than 1 are reserved for future use.
 *
 *
 * @return True if successful, False if not.
 *
 */
bool allocate_header(struct telem_ref *t_ref, uint32_t severity,
                     char *classification, uint32_t payload_version)
{

        int k, i = 0;

        /* The order we create the headers matters */

        if (!set_record_format_header(t_ref)) {
                goto free_and_fail;
        }

        i++;

        if (!set_classification_header(t_ref, classification)) {
                goto free_and_fail;
        }

        i++;

        if (!set_severity_header(t_ref, severity)) {
                goto free_and_fail;
        }

        i++;

        if (!set_machine_id_header(t_ref)) {
                goto free_and_fail;
        }

        i++;

        if (!set_timestamp_header(t_ref)) {
                goto free_and_fail;
        }

        i++;

        if (!set_arch_header(t_ref)) {
                goto free_and_fail;
        }

        i++;

        if (!set_host_type_header(t_ref)) {
                goto free_and_fail;
        }

        i++;

        if (!set_system_build_header(t_ref)) {
                goto free_and_fail;
        }

        i++;

        if (!set_kernel_version_header(t_ref)) {
                goto free_and_fail;
        }

        i++;

        if (!set_payload_format_header(t_ref, payload_version)) {
                goto free_and_fail;
        }

        i++;

        if (!set_system_name_header(t_ref)) {
                goto free_and_fail;
        }

        i++; /* Not necessary, but including for future expansion */

        return true;

free_and_fail:
        for (k = 0; k < i; k++) {
                free(t_ref->record->headers[k]);
        }
        return false;

}

struct telem_ref *tm_create_record(uint32_t severity,
                                   char *classification, uint32_t payload_version)
{
        struct telem_ref *t_ref = NULL;

        t_ref = (struct telem_ref *)malloc (sizeof(struct telem_ref));
        if (!t_ref) {
                telem_log(LOG_CRIT, "Out of memory\n");
                return NULL;
        }

        t_ref->record = (struct telem_record *)malloc (sizeof(struct telem_record));
        if (!t_ref->record) {
                telem_log(LOG_CRIT, "Out of memory\n");
                return NULL;
        }

        // Need to initialize header size, since it is only incremented elsewhere
        t_ref->record->header_size = 0;

        /* Set up the headers */
        /* FIXME: May need to return error codes so the caller can be
         * notified of a failure to allcoate the header and abort.*/

        if (allocate_header(t_ref, severity, classification, payload_version)) {
                return t_ref;
        } else {
                free(t_ref->record);
                free(t_ref);
                return NULL;
        }
}

// TODO: Consider simply setting a pointer to point to data provide in payload instead of copying it?
bool tm_set_payload(struct telem_ref *t_ref, char *payload)
{
        size_t payload_len;

        payload_len = strlen((char *)payload);

        t_ref->record->payload = (char *)malloc(sizeof(char) * payload_len + 1);

        if (!t_ref->record->payload) {
                telem_log(LOG_CRIT, "Out of memory\n");
                return false;
        }

        memset(t_ref->record->payload, 0, sizeof(char) * payload_len + 1);

        strncpy((char *)(t_ref->record->payload), (char *)payload, payload_len);

        t_ref->record->payload_size = payload_len;

        return true;
}

/**
 * Write nbytes from buf to fd. Used to send records to telemd.
 *
 * @param fd Socket fd obtained from tm_get_socket.
 * @param buf Data to be written to the socket.
 * @param nbytes Number of bytes to write out of buf.
 *
 * @return True if successful, False if not.
 *
 */
static bool tm_write_socket(int fd, char *buf, size_t nbytes)
{
        size_t nbytes_out = 0;

        while (nbytes_out != nbytes) {
                ssize_t b;
                b = write(fd, buf + nbytes_out, nbytes - nbytes_out);

                if (b == -1 && errno != EAGAIN) {
                        telem_perror("Write to daemon socket with system error.");
                        return false;
                }
                nbytes_out += (size_t)b;
        }

        return true;
}

/**
 * Obtain a file descriptor for a unix domain socket.
 *
 * @return A file descriptor, or -1 on failure.
 *
 */
static int tm_get_socket(void)
{
        int sfd = -1;
        struct sockaddr_un addr;

        sfd = socket(AF_UNIX, SOCK_STREAM, 0);

        if (sfd == -1) {
                telem_perror("Attempt to allocate socket fd failed.");
                return -1;
        }

        /* Construct server address, and make the connection */
        memset(&addr, 0, sizeof(struct sockaddr_un));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path_config(), sizeof(addr.sun_path) - 1);

        if (connect(sfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_un)) == -1) {
                telem_perror("Could not connect to daemon socket.");
                close(sfd);
                return -1;
        }

        return sfd;
}

bool tm_send_record(struct telem_ref *t_ref)
{
        int i;
        size_t record_size = 0;
        size_t total_size = 0;
        int sfd;
        bool status = false;
        char *data = NULL;
        sfd = tm_get_socket();
        size_t offset = 0;

        if (sfd < 0) {
                telem_log(LOG_ERR, "Failed to get socket fd\n");
                return false;
        }

        total_size = t_ref->record->header_size + t_ref->record->payload_size;
        telem_log(LOG_DEBUG, "Header size : %zu\n", t_ref->record->header_size);
        telem_log(LOG_DEBUG, "Payload size : %zu\n", t_ref->record->payload_size);

        telem_log(LOG_DEBUG, "Total size : %zu\n", total_size);

        /*
         * Allocating buffer for what we intend to send.  Buffer layout is:
         * <uint32_t total_size><uint32_t header_size><headers + Payload>
         * <null-byte>
         * The additional char at the end ensures null termination
         */
        record_size = (2 * sizeof(uint32_t)) + total_size + 1;

        data = malloc(record_size);
        if (!data) {
                telem_log(LOG_CRIT, "Out of memory\n");
                return false;
        }

        memset(data, 0, record_size);

        memcpy(data, &total_size, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        memcpy(data + offset, &t_ref->record->header_size, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        size_t len = 0;
        for (i = 0; i < NUM_HEADERS; i++) {
                len = strlen(t_ref->record->headers[i]);
                memcpy(data + offset, t_ref->record->headers[i], len);
                offset += len;
        }

        memcpy(data + offset, t_ref->record->payload, t_ref->record->payload_size);

        telem_log(LOG_DEBUG, "Data to be sent :\n\n%s\n", data + 2 * sizeof(uint32_t));
        if (tm_write_socket(sfd, data, record_size)) {
                telem_log(LOG_INFO, "Successfully sent record over the socket\n");
                status = true;
        } else {
                telem_log(LOG_ERR, "Error while writing data to socket\n");
        }

        close(sfd);
        free(data);

        return status;
}

void tm_free_record(struct telem_ref *t_ref)
{

        int k;

        if (t_ref == NULL) {
                return;
        }

        if (t_ref->record == NULL) {
                return;
        }

        for (k = 0; k < NUM_HEADERS; k++) {
                free(t_ref->record->headers[k]);
        }

        free(t_ref->record->payload);
        free(t_ref->record);
        free(t_ref);
}

/* vi: set ts=8 sw=8 sts=4 et tw=80 cino=(0: */
