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
#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "telemetry.h"

static struct telem_ref *ref = NULL;

void create_setup(void)
{
        /* We've already called tm_create_record() */
        if (ref) {
                return;
        }
        ref = tm_create_record(1, "t/t/t", 2000);
}

START_TEST(record_create_non_null)
{
        ck_assert_msg(ref != NULL, "Returned unintialized reference");
        ck_assert_msg(ref->record != NULL, "Returned unintialized record in reference");
}
END_TEST

START_TEST(record_create_header_size)
{
        ck_assert_msg(ref->record->header_size > 0, "Found zero-length header");
}
END_TEST

START_TEST(record_create_severity)
{
        char *result;
        if (asprintf(&result, "%s: %u\n", TM_SEVERITY_STR, 1) < 0) {
                return;
        }
        ck_assert_str_eq(ref->record->headers[TM_SEVERITY], result);
        free(result);
}
END_TEST

START_TEST(record_create_classification)
{
        char *result;
        if (asprintf(&result, "%s: %s\n", TM_CLASSIFICATION_STR, "t/t/t") < 0) {
                return;
        }
        ck_assert_str_eq(ref->record->headers[TM_CLASSIFICATION], result);
        free(result);
}
END_TEST

START_TEST(record_create_version)
{
        char *result;
        if (asprintf(&result, "%s: %u\n", TM_PAYLOAD_VERSION_STR, 2000) < 0) {
                return;
        }
        ck_assert_str_eq(ref->record->headers[TM_PAYLOAD_VERSION], result);
        free(result);
}
END_TEST

void create_teardown(void)
{
        if (ref) {
                if (ref->record) {
                        free(ref->record);
                }
                free(ref);
        }
}

Suite *lib_suite(void)
{
        Suite *s = suite_create("libtelemetry");

        TCase *t = tcase_create("record creation");
        tcase_add_checked_fixture(t, create_setup, create_teardown);
        tcase_add_test(t, record_create_non_null);
        tcase_add_test(t, record_create_header_size);
        tcase_add_test(t, record_create_severity);
        tcase_add_test(t, record_create_classification);
        tcase_add_test(t, record_create_version);

        suite_add_tcase(s, t);

        // add more TCases here

        return s;
}

int main(void)
{
        Suite *s;
        SRunner *sr;

        s = lib_suite();
        sr = srunner_create(s);

        srunner_set_log(sr, NULL);
        srunner_set_tap(sr, "-");

        srunner_run_all(sr, CK_SILENT);

        srunner_free(sr);

        return 0;
}

/* vi: set ts=8 sw=8 sts=4 et tw=80 cino=(0: */
