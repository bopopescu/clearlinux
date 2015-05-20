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
#include <stdlib.h>
#include <curl/curl.h>
#include <unistd.h>
#include <limits.h>
#include <assert.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

#include "telemdaemon.h"
#include "common.h"
#include "util.h"
#include "log.h"
#include "configuration.h"

void initialize_daemon(TelemDaemon *daemon)
{
        client_list_head head;
        LIST_INIT(&head);
        daemon->nfds = 0;
        daemon->pollfds = NULL;
        daemon->client_head = head;
}

client *add_client(client_list_head *client_head, int fd)
{
        client *cl;

        cl = (client *)malloc(sizeof(client));
        if (cl) {
                cl->fd = fd;
                cl->offset = 0;
                cl->buf = NULL;

                LIST_INSERT_HEAD(client_head, cl, client_ptrs);
        }
        return cl;
}

void remove_client(client_list_head *client_head, client *cl)
{
        assert(cl);
        LIST_REMOVE(cl, client_ptrs);
        if (cl->buf) {
                free(cl->buf);
        }
        if (cl->fd >= 0) {
                close(cl->fd);
        }
        free(cl);
        cl = NULL;
}

bool is_client_list_empty(client_list_head *client_head)
{
        return (client_head->lh_first == NULL);
}

void terminate_client(TelemDaemon *daemon, client *cl, nfds_t index)
{
        int fd = cl->fd;

        /* Remove fd from the pollfds array */
        del_pollfd(daemon, index);

        /* Remove client from the client list */
        remove_client(&(daemon->client_head), cl);
        telem_log(LOG_INFO, "Client removed:  %d\n", fd);
}

bool handle_client(TelemDaemon *daemon, nfds_t ind, client *cl)
{
        /* For now  read data from fd */
        ssize_t len;
        size_t record_size = 0;
        bool processed = false;

        if (!cl->buf) {
                cl->buf = malloc(RECORD_SIZE_LEN);
                cl->size = RECORD_SIZE_LEN;
        }
        if (!cl->buf) {
                telem_log(LOG_ERR, "Unable to allocate memory, exiting\n");
                exit(EXIT_FAILURE);
        }

        if (!recv(cl->fd, cl->buf, RECORD_SIZE_LEN, MSG_PEEK | MSG_DONTWAIT)) {
                /*Connection closed by client*/
                goto end_client;
        }
        do {
                len = recv(cl->fd, cl->buf + cl->offset, cl->size - cl->offset, 0);
                if (len < 0) {
                        telem_perror("Failed to receive data from the socket");
                        goto end_client;
                } else if (len == 0) {
                        telem_log(LOG_DEBUG, "End of transmission for %d\n",
                                  cl->fd);
                        goto end_client;
                }

                cl->offset += (size_t)len;
                if (cl->offset < RECORD_SIZE_LEN) {
                        continue;
                }
                if (cl->size == RECORD_SIZE_LEN) {
                        record_size = *(uint32_t *)(cl->buf);
                        telem_log(LOG_DEBUG, "Total size of record: %zu\n", record_size);
                        if (record_size == 0) {     //record_size < RECORD_MIN_SIZE || record_size > RECORD_MAX_SIZE
                                goto end_client;
                        }
                        // We just processed the record size field, so the remaining format
                        // is (header size field + record body + terminating '\0' byte)
                        cl->size = sizeof(uint32_t) + record_size + 1;
                        cl->buf = realloc(cl->buf, cl->size);
                        memset(cl->buf, 0, cl->size);
                        cl->offset = 0;
                        if (!cl->buf) {
                                telem_log(LOG_ERR, "Unable to allocate memory, exiting\n");
                                exit(EXIT_FAILURE);
                        }
                }
                if (cl->offset != cl->size) {
                        /* full record not received yet */
                        continue;
                }
                if (cl->size != RECORD_SIZE_LEN) {
                        /* entire record has been received */
                        process_record(cl);
                        /* TODO: cleanup or terminate? */
                        cl->offset = 0;
                        cl->size = RECORD_SIZE_LEN;
                        processed = true;
                        telem_log(LOG_DEBUG, "Record processed for client %d\n", cl->fd);
                        break;
                }
        } while (len > 0);

end_client:
        telem_log(LOG_DEBUG, "Processed client %d: %s\n", cl->fd,
                  processed ? "true" : "false");
        terminate_client(daemon, cl, ind);
        return processed;
}

void machine_id_replace(char **machine_header)
{
        uint64_t machine_id;

        machine_id = get_machine_id();

        int ret = asprintf(machine_header, "%s: %" PRIu64, TM_MACHINE_ID_STR, machine_id);
        if (ret == -1) {
                telem_log(LOG_ERR, "Failed to write machine id header\n");
                exit(EXIT_FAILURE);
        }
}

bool process_record(client *cl)
{
        int i = 0;
        char *headers[NUM_HEADERS];
        bool ret = false;
        char *tok = NULL;
        size_t header_size = 0;
        size_t message_size = 0;
        char *temp_headers = NULL;
        char *msg;
        int k;

        telem_log(LOG_DEBUG, "Total size: %zu\n", cl->size);
        header_size = *(uint32_t *)cl->buf;
        message_size = cl->size - header_size;
        assert(message_size > 0);      //TODO:Check for min and max limits
        msg = (char *)cl->buf + sizeof(uint32_t);

        /* Copying the headers as strtok modifies the orginal buffer */
        temp_headers = strndup(msg, header_size);
        tok = strtok(temp_headers, "\n");

        for (i = 0; i < NUM_HEADERS; i++) {
                telem_log(LOG_DEBUG, "Token: %s\n", tok);
                const char *header_name = get_header_name(i);

                if (get_header(tok, header_name, &headers[i], strlen(header_name))) {
                        if (strcmp(header_name, TM_MACHINE_ID_STR) == 0) {
                                machine_id_replace(&headers[i]);
                        }

                        tok = strtok(NULL, "\n");
                } else {
                        telem_log(LOG_ERR, "process_record: Incorrect headers in record");
                        goto end;
                }
        }

        /* TODO : check if the body is within the limits. */

        /* Send the record as https post */
        ret = post_record_ptr(headers, msg + header_size, true);
end:
        free(temp_headers);
        for (k = 0; k < i; k++)
                free(headers[k]);
        return ret;
}

bool post_record_http(char *headers[], char *body, bool spool)
{
        CURL *curl;
        int res = 0;
        int i;
        char *content = "Content-Type: application/text";
        struct curl_slist *custom_headers = NULL;
        char errorbuf[CURL_ERROR_SIZE];
        long http_response;

        curl = curl_easy_init();
        if (!curl) {
                telem_log(LOG_ERR, "curl_easy_init(): Unable to start libcurl"
                          " easy session, exiting\n");
                exit(EXIT_FAILURE);
                /* TODO: check if memory needs to be released */
        }

        // Errors for any curl_easy_* functions will store nice error messages
        // in errorbuf, so send log messages with errorbuf contents
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorbuf);

        curl_easy_setopt(curl, CURLOPT_URL, server_addr_config());
        curl_easy_setopt(curl, CURLOPT_POST, 1);
#ifdef DEBUG
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
#endif

        for (i = 0; i < NUM_HEADERS; i++) {
                custom_headers = curl_slist_append(custom_headers, headers[i]);
        }
        custom_headers = curl_slist_append(custom_headers, content);

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, custom_headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, strlen(body));

        telem_log(LOG_DEBUG, "Executing curl operation...\n");
        res = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_response);

        if (res) {
                telem_log(LOG_ERR, "Failed sending record: %s\n", errorbuf);
                if (spool) {
                        spool_record(headers, body);
                }
        } else if (http_response != 201) {
                /*  201 means the record was  successfully created
                 *  http response other than 201 means error.
                 */
                telem_log(LOG_ERR, "Encountered error %ld on the server\n",
                          http_response);
                // We treat HTTP error codes the same as libcurl errors
                res = 1;
                if (spool) {
                        spool_record(headers, body);
                }
        } else {
                telem_log(LOG_INFO, "Record sent successfully\n");
        }

        curl_easy_cleanup(curl);
        return res ? false : true;
}

void spool_record(char *headers[], char *body)
{
        const char *spool_dir_path;
        int ret;
        struct stat buf;
        long spool_size, max_spool_size;
        char record_path[PATH_MAX];
        char *tmpbuf = NULL;
        FILE *tmpfile = NULL;
        int tmpfd;
        int i;

        spool_dir_path = spool_dir_config();
        assert(strlen(spool_dir_path) < PATH_MAX - 7);

        memset(record_path, 0, PATH_MAX);
        strncpy(record_path, spool_dir_path, PATH_MAX - 1);
        record_path[PATH_MAX - 1] = '\0';

        ret = stat(record_path, &buf);

        if (ret || !S_ISDIR(buf.st_mode)) {
                telem_log(LOG_ERR, "Spool directory not valid, dropping record\n");
                return;
        }

        // Check if the spool dir is writable
        if (access(record_path, W_OK) != 0) {
                telem_log(LOG_ERR, "Spool dir is not writable\n");
                return;
        }

        //check if the size is greater than 1 MB/spool max size
        max_spool_size = spool_max_size_config();
        spool_size = get_directory_size(record_path);

        telem_log(LOG_DEBUG, "Total size of spool dir: %ld\n", spool_size);
        if (spool_size == -1) {
                telem_log(LOG_ERR, "Error getting spool directory size\n");
                return;
        } else if (spool_size >= (max_spool_size * 1024)) {
                telem_log(LOG_ERR, "Spool dir full, dropping record\n");
                return;
        }

        // create file with record
        ret = asprintf(&tmpbuf, "%s/XXXXXX", record_path);
        if (ret == -1) {
                telem_log(LOG_ERR, "Failed to allocate memory for record name, aborting\n");
                exit(EXIT_FAILURE);
        }

        tmpfd = mkstemp(tmpbuf);
        if (tmpfd == -1) {
                telem_perror("Error while creating temp file");
                goto spool_err;
        }

        //fp = fopen(spool_dir_path, const char *mode);
        tmpfile = fdopen(tmpfd, "a");
        if (!tmpfile) {
                telem_perror("Error opening temp file");
                close(tmpfd);
                if (unlink(tmpbuf)) {
                        telem_perror("Error deleting temp file");
                }
                goto spool_err;
        }

        // write headers
        for (i = 0; i < NUM_HEADERS; i++) {
                fprintf(tmpfile, "%s\n", headers[i]);
        }

        //write body
        fprintf(tmpfile, "%s\n", body);

        fflush(tmpfile);
        fclose(tmpfile);

spool_err:
        free(tmpbuf);
        return;
}

void add_pollfd(TelemDaemon *daemon, int fd, short events)
{
        assert(daemon);
        assert(fd >= 0);

        if (!daemon->pollfds) {
                daemon->pollfds = (struct pollfd *)malloc(sizeof(struct pollfd));
                if (!daemon->pollfds) {
                        telem_log(LOG_ERR, "Unable to allocate memory for"
                                  " pollfds array, exiting\n");
                        exit(EXIT_FAILURE);
                }
                daemon->current_alloc = sizeof(struct pollfd);
        } else {
                /* Reallocate here */
                if (!reallocate((void **)&(daemon->pollfds),
                                &(daemon->current_alloc),
                                ((daemon->nfds + 1) * sizeof(struct pollfd)))) {
                        telem_log(LOG_ERR, "Unable to realloc, exiting\n");
                        exit(EXIT_FAILURE);
                }
        }

        daemon->pollfds[daemon->nfds].fd = fd;
        daemon->pollfds[daemon->nfds].events = events;
        daemon->pollfds[daemon->nfds].revents = 0;
        daemon->nfds++;
}

void del_pollfd(TelemDaemon *daemon, nfds_t i)
{
        assert(daemon);
        assert(i >= 0 && i < daemon->nfds);

        if (i < daemon->nfds - 1) {
                memmove(&(daemon->pollfds[i]), &(daemon->pollfds[i + 1]),
                        (sizeof(struct pollfd) * (daemon->nfds - i - 1)));
        }
        daemon->nfds--;
}

uint64_t get_machine_id()
{
        FILE *id_file = NULL;
        uint64_t machine_id = 0;
        int ret;

        char *machine_id_file_name = TM_MACHINE_ID_FILE;

        id_file = fopen(machine_id_file_name, "r");
        if (id_file == NULL) {
                telem_log(LOG_ERR, "Could not open machine id file\n");
                return 0;
        }

        ret = fscanf(id_file, "%" PRIu64, &machine_id);
        if (ret != 1) {
                telem_perror("Could not read machine id from file");
                fclose(id_file);
                return 0;
        }
        fclose(id_file);
        return machine_id;
}

int machine_id_write(uint64_t random_id)
{
        FILE *fp;
        int ret;
        int result = -1;

        fp = fopen(TM_MACHINE_ID_FILE, "w");
        if (fp == NULL) {
                telem_perror("Could not open machine id file");
                goto file_error;
        }

        ret = fprintf(fp, "%" PRIu64, random_id);
        if (ret < 0) {
                telem_perror("Unable to write to machine id file");
                goto file_error;
        }
        result = 0;
        fflush(fp);
file_error:
        if (fp != NULL) {
                fclose(fp);
        }

        return result;
}

int generate_machine_id(void)
{
        FILE *frandom = NULL;
        struct stat stat_buf;
        int ret;
        uint64_t random_id;
        int result = -1;

        ret = stat("/dev/urandom", &stat_buf);
        if (ret == -1) {
                telem_log(LOG_ERR, "Unable to stat urandom device\n");
                goto rand_err;
        }

        if (!S_ISCHR(stat_buf.st_mode)) {
                telem_log(LOG_ERR, "/dev/urandom is not a character device file\n");
                goto rand_err;
        }

        /* TODO : check for major and minor numbers to be extra sure ??
           ((stat_buffer.st_rdev == makedev(1, 8)) || (stat_buffer.st_rdev == makedev(1, 9))*/

        frandom = fopen("/dev/urandom", "r");
        if (frandom == NULL) {
                telem_perror("Error opening random file");
                goto rand_err;
        }

        if (fread((void *)&random_id, sizeof(random_id), 1, frandom) == 0) {
                telem_perror("error while reading random device");
                goto rand_err;
        }

        result = machine_id_write(random_id);
rand_err:
        if (frandom != NULL) {
                fclose(frandom);
        }
        return result;
}

int update_machine_id()
{
        int result = 0;
        struct stat buf;
        int ret;

        char *machine_id_filename = TM_MACHINE_ID_FILE;
        ret = stat(machine_id_filename, &buf);

        if (ret == -1) {
                if (errno == ENOENT) {
                        telem_log(LOG_INFO, "Machine id file does not exist\n");
                        result = generate_machine_id();
                } else {
                        telem_log(LOG_ERR, "Unable to stat machine id file\n");
                        result = -1;
                }
        } else {
                time_t current_time = time(NULL);

                if ((current_time - buf.st_mtime) > TM_MACHINE_ID_EXPIRY) {
                        telem_log(LOG_INFO, "Machine id file has expired\n");
                        result = generate_machine_id();
                }
        }
        return result;
}

/* vi: set ts=8 sw=8 sts=4 et tw=80 cino=(0: */
