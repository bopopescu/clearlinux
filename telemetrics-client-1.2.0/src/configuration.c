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

#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include <errno.h>
#include <limits.h>
#include <dirent.h>
#include <string.h>

#include "configuration.h"
#include "util.h"
#include "log.h"

static char *config_file = NULL;
static char *default_config_file = DATADIR "/defaults/telemetrics/telemetrics.conf";
static char *etc_config_file = "/etc/telemetrics/telemetrics.conf";
static GKeyFile *keyfile = NULL;
static bool cmd_line_cfg = false;

/* Conf strings expected in the conf file */
const char *config_key_str[] = { NULL, "server", "socket_path", "spool_dir",
                                 "record_expiry", "spool_max_size",
                                 "spool_process_time", NULL };

static struct configuration config = { { 0 }, false, NULL };

void set_config_file(char *filename)
{
        config_file = strdup(filename);
        cmd_line_cfg = true;
}

bool read_config_from_file(char *config_file, struct configuration *config)
{
        GError *error = NULL;
        GKeyFileFlags flags = G_KEY_FILE_NONE;
        int i;

        if (keyfile != NULL) {
                g_key_file_free(keyfile);
        }

        keyfile = g_key_file_new();

        if (!g_key_file_load_from_file(keyfile, (gchar *)config_file, flags, &error)) {
                telem_log(LOG_ERR, "%s\n", error->message);
                return false;
        } else {
                for (i = CONF_MIN + 1; i < CONF_MAX; i++) {
                        config->values[i] = g_key_file_get_string(keyfile,
                                                                  "settings", config_key_str[i], NULL);
                        if (!config->values[i]) {
                                telem_log(LOG_WARNING, "Config key %s not found"
                                          " in configuration file\n",
                                          config_key_str[i]);
                                return false;
                        }
                }
        }

        config->initialised = true;
        return true;
}

void initialise_config(void)
{
        if (config.initialised) {
                return;
        }

        /* No config file provided on command line */
        if (!config_file) {
                if (file_exists(etc_config_file)) {
                        config_file = etc_config_file;
                } else {
                        if (file_exists(default_config_file)) {
                                config_file = default_config_file;
                        } else {
                                /* If there is no default config, exit with failure */
                                telem_log(LOG_ERR, "No configuration file found,"
                                          "exiting\n");
                                exit(EXIT_FAILURE);
                        }
                }
        }
        if (!read_config_from_file(config_file, &config)) {
                /* Error while parsing file  */
                telem_log(LOG_ERR, "Error while parsing configuration file\n");
                exit(EXIT_FAILURE);
        }
}

void reload_config(void)
{
        config.initialised = false;

        if (!cmd_line_cfg) {
                config_file = NULL;
        }
        initialise_config();
}

__attribute__((destructor))
void free_configuration(void)
{
        int i;

        telem_log(LOG_DEBUG, "Destructor called\n");

        if (!config.initialised) {
                return;
        }

        for (i = CONF_MIN + 1; i < CONF_MAX; i++) {
                free(config.values[i]);
        }

        g_key_file_free(keyfile);

        if (cmd_line_cfg) {
                free(config_file);
        }
}

const char *server_addr_config()
{
        initialise_config();
        return (const char *)config.values[CONF_SERVER_ADDR];
}

const char *socket_path_config()
{
        initialise_config();
        return (const char *)config.values[CONF_SOCKET_PATH];
}

const char *spool_dir_config()
{
        initialise_config();
        return (const char *)config.values[CONF_SPOOL_DIR];
}

long int record_expiry_config()
{
        long val;
        char *endptr;

        initialise_config();
        val = strtol(config.values[CONF_RECORD_EXPIRY], &endptr, 10);

        if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN))
            || (errno != 0 && val == 0)) {
                telem_log(LOG_ERR, "Invalid value for spool size\n");
                exit(EXIT_FAILURE);
        }

        if (endptr == config.values[CONF_RECORD_EXPIRY]) {
                telem_log(LOG_ERR, "Spool max size should be a numeric value\n");
                exit(EXIT_FAILURE);
        }
        return val;
}

long int spool_max_size_config()
{
        long val;
        char *endptr;

        initialise_config();
        val = strtol(config.values[CONF_SPOOL_MAX_SIZE], &endptr, 10);

        if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN))
            || (errno != 0 && val == 0)) {
                telem_log(LOG_ERR, "Invalid value for spool size\n");
                exit(EXIT_FAILURE);
        }

        if (endptr == config.values[CONF_SPOOL_MAX_SIZE]) {
                telem_log(LOG_ERR, "Spool max size should be a numeric value\n");
                exit(EXIT_FAILURE);
        }
        return val;
}

int spool_process_time_config()
{
        long val;
        char *endptr;

        initialise_config();
        val = strtol(config.values[CONF_SPOOL_PROCESS_TIME], &endptr, 10);

        //  check value is in integer range and is positive
        if ((errno != 0  && val == 0) || (val >= INT_MAX || val <= 0)) {
                telem_log(LOG_ERR, "Invalid value for spool process time\n");
                exit(EXIT_FAILURE);
        }

        if (endptr == config.values[CONF_SPOOL_PROCESS_TIME]) {
                telem_log(LOG_ERR, "Spool process time should"
                          "be a numeric value\n");
                exit(EXIT_FAILURE);
        }
        return (int)val;
}

/* vi: set ts=8 sw=8 sts=4 et tw=80 cino=(0: */
