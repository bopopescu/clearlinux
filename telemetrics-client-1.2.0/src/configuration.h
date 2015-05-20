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

#pragma once
#include <stdbool.h>

enum config_keys {
        CONF_MIN = 0,
        CONF_SERVER_ADDR,
        CONF_SOCKET_PATH,
        CONF_SPOOL_DIR,
        CONF_RECORD_EXPIRY,
        CONF_SPOOL_MAX_SIZE,
        CONF_SPOOL_PROCESS_TIME,
        //CONF_SEND_RATE,
        CONF_MAX
};

typedef struct configuration {
        char *values[CONF_MAX];
        bool initialised;
        char *config_file;
} configuration;

/* Sets the configuration file to be used later */
void set_config_file(char *filename);

/* Parses the ini format config file */
bool read_config_from_file(char *filename, struct configuration *config);

/* Causes the daemon to read the configuration file */
void reload_config(void);

/* Getters for the configuration values */

/* Gets the server address to send the telemetry records */
const char *server_addr_config(void);

/* Gets the path for the unix domain socket */
const char *socket_path_config(void);

/* Gets the path for the spool directory */
const char *spool_dir_config(void);

/* Gets the record expiry time */
long record_expiry_config(void);

/*
 * Gets the maximum size of the spool. Once the spool reaches the maximum size,
 * new records are dropped
 */
long spool_max_size_config(void);

/* Get the time ainterval for processing records in the spool */
int spool_process_time_config(void);

/* vi: set ts=8 sw=8 sts=4 et tw=80 cino=(0: */
