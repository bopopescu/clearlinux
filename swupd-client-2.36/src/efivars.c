/*
 *   Software Updater - client side
 *
 *      Copyright © 2014-2015 Intel Corporation.
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, version 2 or later of the License.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *   Authors:
 *         Christophe Guiraud <christophe.guiraud@intel.com>
 *
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <swupd.h>

#ifdef SWUPD_WITH_ESP
#warning consider what repair means in cloud host and guest contexts

static efivar_bootloader_boot_for_repair_needed_cb_t boot_for_repair_needed_cb = NULL;

void fatal_update_error(int from_version, int to_version)
{
	struct list *version_list = NULL;

	version_list = list_prepend_data(version_list, (void *)((intptr_t)to_version));
	version_list = list_prepend_data(version_list, (void *)((intptr_t)from_version));
	efivar_bootloader_set_next_boot_to_repair(repair_update_failure,
						  version_list);
	list_free_list(version_list);
}

void critical_verify_error(int version)
{
	struct list *version_list = NULL;

	version_list = list_prepend_data(version_list, (void *)((intptr_t)version));
	efivar_bootloader_set_next_boot_to_repair(repair_verify_failure,
						  version_list);
	list_free_list(version_list);
}

void critical_verify_multi_error(struct list *version_list)
{
	efivar_bootloader_set_next_boot_to_repair(repair_verify_failure,
						  version_list);
}

void clear_verify_error(void) {
	efivar_bootloader_clear_verify_error();
}

typedef struct efi_guid_t {
	uint8_t  b[16];
} efi_guid_t;

#define EFI_GUID(a, b ,c ,d0 ,d1 ,d2 ,d3 ,d4 ,d5 ,d6 ,d7) \
	((efi_guid_t) \
	{{(a) & 0xff, ((a) >> 8) & 0xff, ((a) >> 16) & 0xff, ((a) >> 24) & 0xff, \
	  (b) & 0xff, ((b) >> 8) & 0xff, \
	  (c) & 0xff, ((c) >> 8) & 0xff, \
	  (d0), (d1), (d2), (d3), (d4), (d5), (d6), (d7)}})

#define GUID_FORMAT \
	"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x"

#define EFI_VARIABLE_NON_VOLATILE	0x0000000000000001
#define EFI_VARIABLE_BOOTSERVICE_ACCESS	0x0000000000000002
#define EFI_VARIABLE_RUNTIME_ACCESS	0x0000000000000004

#define LOADER_ATTRIBUTES \
		EFI_VARIABLE_RUNTIME_ACCESS | \
		EFI_VARIABLE_BOOTSERVICE_ACCESS | \
		EFI_VARIABLE_NON_VOLATILE

#define VARS_PATH "/sys/firmware/efi/vars/"

static const efi_guid_t LOADER_GUID =
	EFI_GUID(0x4a67b082, 0x0a4c, 0x41cf, 0xb6, 0xc7, 0x44, 0x0b, 0x29, 0xbb, 0x8c, 0x4f);

typedef struct efi_variable_32_t {
	uint16_t	name[1024 / sizeof(uint16_t)];
	efi_guid_t	guid;
	uint32_t	data_sz;
	uint8_t		data[1024];
	uint32_t	status;
	uint32_t	attributes;
} __attribute__((packed)) efi_variable_32_t;


typedef struct efi_variable_64_t {
	uint16_t	name[1024 / sizeof(uint16_t)];
	efi_guid_t	guid;
	uint64_t	data_sz;
	uint8_t		data[1024];
	uint64_t	status;
	uint32_t	attributes;
} __attribute__((packed)) efi_variable_64_t;


static int kernel_arch_64_bit(bool *is_64bit)
{
	char version[PATH_MAX];
	FILE *file;
	char *tmp1, *tmp2;
	const char *prefix = "Linux version ";

	file = fopen("/proc/version", "r");
	if (file == NULL)
		return -1;

	tmp1 = fgets(version, PATH_MAX, file);
	fclose(file);
	if (tmp1 == NULL)
		return -1;

	tmp1 = strstr(version, prefix);
	if (tmp1 == NULL)
		return -1;

	tmp2 = strchr(tmp1 + strlen(prefix), ' ');
	if (tmp2)
		*tmp2 = '\0';

	if (strstr(tmp1, "x86_64") != NULL)
		*is_64bit = true;
	else
		*is_64bit = false;

	return 0;
}

static uint16_t *char_str_to_efi_str(const char *src)
{
	size_t i = 0;
	uint16_t *dst;
	size_t src_len = strlen(src);

	dst = calloc(src_len + 1, sizeof(uint16_t));
	if (!dst)
		abort();

	while (i < src_len) {
		dst[i] = src[i];
		i++;
	}
	dst[i] = 0;

	return dst;
}

static size_t efi_str_length(const uint16_t *str)
{
	size_t len = 0;

	while (*(str + len))
		len++;

	return len;
}

static char *efi_str_to_char_str(const uint16_t *src)
{
	size_t i = 0;
	char *dst;
	size_t src_len = efi_str_length(src);

	dst = calloc(src_len + 1, sizeof(char));
	if (!dst)
		abort();

	while (i < src_len) {
		dst[i] = src[i];
		i++;
	}
	dst[i] = '\0';

	return dst;
}

static char *get_efi_path(efi_guid_t guid, const char *name, const char *entry)
{
	char *path;

	if (asprintf(&path, VARS_PATH "%s-" GUID_FORMAT "%s", name,
			  guid.b[3], guid.b[2], guid.b[1], guid.b[0],
			  guid.b[5], guid.b[4], guid.b[7], guid.b[6],
			  guid.b[8], guid.b[9], guid.b[10], guid.b[11],
			  guid.b[12], guid.b[13], guid.b[14], guid.b[15],
			  entry) <= 0)
		abort();

	return path;
}

static int efi_write(const char *entry, void *data, ssize_t len)
{
	int fd;

	fd = open(entry, O_WRONLY);
	if (fd < 0) {
		LOG_ERROR(NULL, "Failed to open file", class_file_io, "\\*entry=\"%s\",strerror=\"%s\"*\\",
				entry, strerror(errno));
		return -1;
	}

	if (write(fd, data, len) != len) {
		LOG_ERROR(NULL, "Failed to write to file", class_file_io, "\\*entry=\"%s\",strerror=\"%s\"*\\",
				entry, strerror(errno));
		close(fd);
		return -1;
	}

	close(fd);

	return 0;
}

static bool efi_variable_exists(efi_guid_t guid, const char *name)
{
	char *path;
	bool result = false;

	path = get_efi_path(guid, name, "");

	if (access(path, F_OK) == 0)
		result = true;

	free(path);

	return result;
}

static int read_efi_variable(efi_guid_t guid, const char *name,
			     void **raw_var, ssize_t *raw_var_len,
			     bool *is_64bit)
{
	int fd;
	char *path;
	efi_variable_32_t *efi_var_32 = NULL;
	efi_variable_64_t *efi_var_64 = NULL;
	bool kernel_64_bit;

	if (kernel_arch_64_bit(&kernel_64_bit) < 0) {
		LOG_ERROR(NULL, "kernel architecture detection failed", class_efi, "");
		return -1;
	}

	path = get_efi_path(guid, name, "/raw_var");
	if (path == NULL)
		return -1;

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		LOG_ERROR(NULL, "Failed to open file", class_file_io, "\\*path=\"%s\",strerror=\"%s\"*\\",
				path, strerror(errno));
		free(path);
		return -1;
	}

	if (kernel_64_bit) {
		efi_var_64 = malloc(sizeof(efi_variable_64_t));
		if (!efi_var_64) {
			close(fd);
			free(path);
			return -1;
		}
		*raw_var = efi_var_64;
		*raw_var_len = sizeof(efi_variable_64_t);
	} else {
		efi_var_32 = malloc(sizeof(efi_variable_32_t));
		if (!efi_var_32) {
			close(fd);
			free(path);
			return -1;
		}
		*raw_var = efi_var_32;
		*raw_var_len = sizeof(efi_variable_32_t);
	}

	if (read(fd, *raw_var, *raw_var_len) != *raw_var_len) {
		LOG_ERROR(NULL, "Failed to read file", class_file_io, "\\*path=\"%s\",strerror=\"%s\"*\\",
				path, strerror(errno));
		close(fd);
		free(*raw_var);
		*raw_var = NULL;
		free(path);
		return -1;
	}

	if (is_64bit != NULL)
		*is_64bit = kernel_64_bit;

	close(fd);
	free(path);

	return 0;
}

static int get_efi_variable(efi_guid_t guid, const char *name, char **value)
{
	int ret;
	void *raw_var = NULL;
	ssize_t raw_var_len = 0;
	bool kernel_64_bit;
	const uint16_t *data;
	efi_variable_32_t *efi_var_32_ptr;
	efi_variable_64_t *efi_var_64_ptr;

	*value = NULL;

	ret = read_efi_variable(guid, name, &raw_var, &raw_var_len,
				&kernel_64_bit);
	if (ret < 0)
		return -1;

	if (kernel_64_bit) {
		efi_var_64_ptr = (efi_variable_64_t*)raw_var;
		data = (const uint16_t *)&(efi_var_64_ptr->data);
	} else {
		efi_var_32_ptr = (efi_variable_32_t*)raw_var;
		data = (const uint16_t *)&(efi_var_32_ptr->data);
	}

	*value = efi_str_to_char_str(data);

	free(raw_var);
	return 0;
}

static int delete_efi_variable(efi_guid_t guid, const char *name)
{
	int ret;
	void *raw_var = NULL;
	ssize_t raw_var_len = 0;

	ret = read_efi_variable(guid, name, &raw_var, &raw_var_len, NULL);
	if (ret < 0)
		return -1;

	ret = efi_write(VARS_PATH "del_var", raw_var, raw_var_len);

	free(raw_var);
	return ret;
}

static int set_efi_variable(efi_guid_t guid, const char *name,
			    const char *value)
{
	int i;
	char *path;
	void *raw_var = NULL;
	ssize_t raw_var_len = 0;
	bool kernel_64_bit;
	efi_variable_32_t efi_var_32;
	efi_variable_64_t efi_var_64;
	size_t efi_value_size;
	uint16_t *efi_value;

	if (kernel_arch_64_bit(&kernel_64_bit) < 0) {
		LOG_ERROR(NULL, "kernel architecture detection failed", class_efi, "");
		return -1;
	}

	efi_value = char_str_to_efi_str(value);
	efi_value_size = ((efi_str_length(efi_value) + 1) * sizeof(uint16_t));

	if ((strlen(name) > 1024) || (efi_value_size > 1024)) {
		LOG_ERROR(NULL, "Invalid EFI variable parameter", class_efi, "");
		free(efi_value);
		return -1;
	}

	path = get_efi_path(guid, name, "/data");
	if (path == NULL) {
		free(efi_value);
		return -1;
	}

	if ((access(path, F_OK) == 0) &&
	    (delete_efi_variable(guid, name) < 0)) {
		free(efi_value);
		free(path);
		return -1;
	}

	free(path);

	if (kernel_64_bit) {
		memset(&efi_var_64, 0, sizeof(efi_var_64));
		efi_var_64.guid = guid;
		efi_var_64.status = 0;
		efi_var_64.attributes = (uint32_t)LOADER_ATTRIBUTES;
		for (i = 0; name[i] != '\0'; i++)
			efi_var_64.name[i] = name[i];
		efi_var_64.data_sz = efi_value_size;
		memcpy(efi_var_64.data, (void *)efi_value, efi_value_size);
		raw_var = &efi_var_64;
		raw_var_len = sizeof(efi_var_64);
	} else {
		memset(&efi_var_32, 0, sizeof(efi_var_32));
		efi_var_32.guid = guid;
		efi_var_32.status = 0;
		efi_var_32.attributes = (uint32_t)LOADER_ATTRIBUTES;
		for (i = 0; name[i] != '\0'; i++)
			efi_var_32.name[i] = name[i];
		efi_var_32.data_sz = efi_value_size;
		memcpy(efi_var_32.data, (void *)efi_value, efi_value_size);
		raw_var = &efi_var_32;
		raw_var_len = sizeof(efi_var_32);
	}

	free(efi_value);

	if (efi_write(VARS_PATH "new_var", raw_var, raw_var_len) < 0)
		return -1;

	return 0;
}

static int version_sort_ascending(const void *a, const void *b)
{
	return ((int)a - (int)b);
}

int efivar_bootloader_set_next_boot_to_repair(enum repair_reason reason, struct list *version_list)
{
	int ret;
	char *reason_str = NULL;
	char *tmp;
	int i = 1;
	unsigned int count;

	count = list_len(version_list);

	if ((reason == repair_boot_check_failure) && (count == 1)) {
		if (asprintf(&reason_str, "boot-check-failure: %i",
					   (int)version_list->data) < 0)
			abort();
	} else if ((reason == repair_verify_failure) && (count > 0)) {
		if (asprintf(&reason_str, "integrity-check-failure: ") < 0)
			abort();

		version_list = list_sort(version_list, version_sort_ascending);
		while (count > VERIFY_FAILED_MAX_VERSIONS_COUNT) {
			version_list = version_list->next;
			count--;
		}

		while (version_list && (i <= VERIFY_FAILED_MAX_VERSIONS_COUNT)) {
			tmp = reason_str;
			if (asprintf(&reason_str, "%s%s%i",
						   tmp,
						   ((i != 1) ? "," : ""),
						   (int)version_list->data) < 0)
				abort();
			free(tmp);
			version_list = version_list->next;
			i++;
		}
	} else if ((reason == repair_update_failure) && (count == 2)) {
		if (asprintf(&reason_str, "update-failure: %i,%i",
					   (int)version_list->data,
					   (int)version_list->next->data) < 0)
			abort();
	} else if (reason == repair_restore_starpeak) {
		if (asprintf(&reason_str, "repair-starpeak-failure") < 0)
			abort();
	} else {
		LOG_INFO(NULL, "[LoaderEntryRepairReason] Invalid reason", class_efi, "");
		return -1;
	}

	LOG_INFO(NULL, "[LoaderEntryRepairReason] Set EFI variable to", class_efi, " %s", reason_str);

	ret = set_efi_variable(LOADER_GUID, "LoaderEntryRepairReason", reason_str);
	free(reason_str);
	if (ret < 0) {
		LOG_ERROR(NULL, "[LoaderEntryRepairReason] Failed to set EFI variable", class_efi,
				"\\*strerror=\"%s\"*\\", strerror(errno));
		return ret;
	}

	LOG_INFO(NULL, "[LoaderEntryOneShot] Set EFI variable to recovery", class_efi, "");

	ret = set_efi_variable(LOADER_GUID, "LoaderEntryOneShot", "recovery");
	if (ret < 0) {
		LOG_ERROR(NULL, "[LoaderEntryOneShot] Failed to set EFI variable", class_efi,
				"\\*strerror=\"%s\"*\\", strerror(errno));
		return ret;
	}

	// Inform upper layers that we need to reboot for repair.
	if (boot_for_repair_needed_cb != NULL) {
		boot_for_repair_needed_cb();
	}

	return ret;
}

void efivar_bootloader_set_boot_for_repair_needed_cb(efivar_bootloader_boot_for_repair_needed_cb_t cb)
{
	boot_for_repair_needed_cb = cb;
}

int efivar_bootloader_set_next_boot_to_version(int version)
{
	int ret;
	char *version_str;

	if (asprintf(&version_str, "%i", version) < 0)
		abort();

	LOG_INFO(NULL, "[LoaderEntryOneShot] Set EFI variable to version:", class_efi, "%s", version_str);

	ret = set_efi_variable(LOADER_GUID, "LoaderEntryOneShot", version_str);
	free(version_str);
	if (ret < 0) {
		LOG_ERROR(NULL, "[LoaderEntryOneShot] Failed to set EFI variable", class_efi,
				"\\*strerror=\"%s\"*\\", strerror(errno));
		return ret;
	}

	if (!efi_variable_exists(LOADER_GUID, "LoaderEntryDefault"))
		return ret;

	LOG_INFO(NULL, "[LoaderEntryDefault] Delete EFI variable", class_efi, "");

	ret = delete_efi_variable(LOADER_GUID, "LoaderEntryDefault");
	if (ret < 0) {
		LOG_ERROR(NULL, "[LoaderEntryDefault] Failed to delete EFI variable", class_efi,
				"\\*strerror=\"%s\"*\\", strerror(errno));
	}

	return ret;
}

int efivar_bootloader_clear_verify_error(void)
{
	int ret = 0;
	char *value = NULL;

	if (!efi_variable_exists(LOADER_GUID, "LoaderEntryRepairReason"))
		return 0;

	ret = get_efi_variable(LOADER_GUID, "LoaderEntryRepairReason", &value);
	if ((ret < 0) || (value == NULL)) {
		LOG_ERROR(NULL, "[LoaderEntryRepairReason] Failed to retrieve EFI variable", class_efi,
				"\\*strerror=\"%s\"*\\", strerror(errno));
		return ret;
	}

	ret = strncmp(value, "integrity-check-failure:", 24);
	free(value);
	if (ret != 0)
		return 0;

	LOG_INFO(NULL, "[LoaderEntryRepairReason] Delete EFI variable", class_efi, "");

	ret = delete_efi_variable(LOADER_GUID, "LoaderEntryRepairReason");
	if (ret < 0) {
		LOG_ERROR(NULL, "[LoaderEntryRepairReason] Failed to delete EFI variable", class_efi,
				"\\*strerror=\"%s\"*\\", strerror(errno));
		return ret;
	}

	if (!efi_variable_exists(LOADER_GUID, "LoaderEntryOneShot"))
		return 0;

	ret = get_efi_variable(LOADER_GUID, "LoaderEntryOneShot", &value);
	if ((ret < 0) || (value == NULL)) {
		LOG_ERROR(NULL, "[LoaderEntryOneShot] Failed to retrieve EFI variable", class_efi,
				"\\*strerror=\"%s\"*\\", strerror(errno));
		return ret;
	}

	ret = strncmp(value, "recovery", 8);
	free(value);
	if (ret != 0)
		return 0;

	LOG_INFO(NULL, "[LoaderEntryOneShot] Delete EFI variable", class_efi, "");

	ret = delete_efi_variable(LOADER_GUID, "LoaderEntryOneShot");
	if (ret < 0) {
		LOG_ERROR(NULL, "[LoaderEntryOneShot] Failed to delete EFI variable", class_efi,
				"\\*strerror=\"%s\"*\\", strerror(errno));
	}

	return ret;
}

/* This function is called at the end of the system boot script.
 * if the LoaderEntryOneShot EFI variable exists we compare the current running
 * version against its content. If the current_version != one_shot_version then
 * we've had a boot error, so we fallback either to the prior boot
 * (i.e we do nothing) or to the repair os if preferred (repair_fallback).
 * If current_version and one_shot_version are similar then we clear the
 * LoaderEntryOneShot variable. */
int efivar_bootloader_boot_check(int version, bool repair_fallback)
{
	int ret = 0;
	char *value = NULL;
	int one_shot_version;
	struct list *version_list = NULL;

	if (!efi_variable_exists(LOADER_GUID, "LoaderEntryOneShot")) {
		LOG_ERROR(NULL, "[LoaderEntryOneShot] EFI variable doesn't exist", class_efi, "");
		return 0;
	}

	ret = get_efi_variable(LOADER_GUID, "LoaderEntryOneShot", &value);
	if ((ret < 0) || (value == NULL)) {
		LOG_ERROR(NULL, "[LoaderEntryOneShot] Failed to retrieve EFI variable", class_efi,
				"\\*strerror=\"%s\"*\\", strerror(errno));
		return ret;
	}

	one_shot_version = strtol(value, NULL, 10);
	if (one_shot_version != version) { /* boot error */
		LOG_ERROR(NULL, "Boot check version mismatch", class_efi, "\\*one_shot_version=\"%i\",current_version=\"%i\"*\\",
				one_shot_version, version);

		if (repair_fallback) { /* ask bootloader to fallback to Repair OS at next boot */
			LOG_WARN(NULL, "Fallback to Repair OS at next boot", class_efi, "");

			version_list = list_prepend_data(version_list, (void *)((intptr_t)one_shot_version));
			ret = efivar_bootloader_set_next_boot_to_repair(repair_boot_check_failure,
									version_list);
			list_free_list(version_list);
		} else { /* do nothing, bootloader will fallback to prior OS at next boot */
			LOG_WARN(NULL, "Fallback to prior OS version at next boot", class_efi, "");
		}
	} else { /* boot ok */
		LOG_INFO(NULL, "[LoaderEntryOneShot] Delete EFI variable", class_efi, "");

		ret = delete_efi_variable(LOADER_GUID, "LoaderEntryOneShot");
		if (ret < 0) {
			LOG_ERROR(NULL, "[LoaderEntryOneShot] Failed to delete EFI variable", class_efi,
					"\\*strerror=\"%s\"*\\", strerror(errno));
		}
	}

	free(value);

	return ret;
}

static void dump_efi_var(const char *name)
{
	int ret;
	char *value = NULL;

	if (!efi_variable_exists(LOADER_GUID, name)) {
		log_stdout("[%s] EFI variable doesn't exist\n", name);
	} else {
		ret = get_efi_variable(LOADER_GUID, name, &value);
		if ((ret < 0) || (value == NULL)) {
			log_stdout("[%s] Failed to retrieve EFI variable: %s\n", name, strerror(errno));
			return;
		}
		log_stdout("[%s] = %s\n", name, value);
		free(value);
	}
}

void efivar_bootloader_dump(void)
{
	dump_efi_var("LoaderEntryOneShot");
	dump_efi_var("LoaderEntryRepairReason");
	dump_efi_var("LoaderEntryDefault");
}
#else /* SWUPD_WITHOUT_ESP */
void critical_verify_error(int version)
{
	LOG_ERROR(NULL, "Verify failed", class_osvol, "\\*version=\"%d\"*\\", version);
}

void clear_verify_error(void)
{
}
#endif /* SWUPD_*_ESP */
