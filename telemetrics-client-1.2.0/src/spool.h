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

/* Spooling should run every TM_SPOOL_RUN_MAX atleast */
#define TM_SPOOL_RUN_MAX (60 /*min*/ * 60 /*sec*/)

/* Spooling should not run more often than TM_SPOOL_RUN_MIN */
#define TM_SPOOL_RUN_MIN (2 /*min*/ * 60 /*sec*/)

/* Maximum records that can be sent in a single spool run loop*/
#define TM_SPOOL_MAX_SEND_RECORDS 10

/* Maximum records that can be processed in a single spool run loop*/
#define TM_SPOOL_MAX_PROCESS_RECORDS 20

/**
 * Run the spool record loop periodically
 */
void spool_records_loop(void);

/**
 * Process the spooled record
 *
 * @param spool_dir Path of the spool directory
 * @param name File name of the spooled record
 * @param records_processed Number of records processed till now
 * @param records_sent Number of records sent to the backend
 */
void process_spooled_record(const char *spool_dir, char *name,
                            int *records_processed, int *records_sent);

/**
 * Send the spooled record to the backend
 *
 * @param record_path Path of the spooled record
 * @param post_succeeded bool indicating if the previous post was successful
 * @param sz Size of the file in bytes
 */
void transmit_spooled_record(char *record_path, bool *post_succeeded, long sz);

/**
 * Comparison function used for qsort
 *
 * @param entrya Pointer to the spool record file
 * @param entryb Pointer to the spool record file
 * @path path The path to the spool directory
 *
 * @return Returns the value of the comparison
 */
int spool_record_compare(const void *entrya, const void *entryb, void *path);

/* vi: set ts=8 sw=8 sts=4 et tw=80 cino=(0: */
