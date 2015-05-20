/*
 *   Software Updater - client side
 *
 *      Copyright © 2012-2015 Intel Corporation.
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
 *         Tom Keel <thomas.keel@intel.com>
 *
 */

#define _GNU_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>

#include <signature.h>
#include <swupd.h>


/*
 * Implementation flavors:
 *   FAKE ..... do nothing, always return success
 *   FORGIVE .. do everything, always return success
 *   REAL ..... do everything, return the real status
 */
#define IMPL_FAKE    0
#define IMPL_FORGIVE 1
#define IMPL_REAL    2

#warning TODO pick signing scheme
#if defined(SWUPD_LINUX_ROOTFS)
#define IMPL IMPL_FAKE
#endif

#if IMPL != IMPL_FAKE

static X509_STORE *create_store(const char *, const char *, const char *);

static char *VERIF_FAIL = "Signature verification failed";
static char *XSTORE_FAIL = "XSTORE creation failed";

static bool initialized = false;

static X509_STORE *x509_store = NULL;

bool signature_initialize(const char *ca_cert_filename)
{
	if (initialized) {
		return true;
	}
	OpenSSL_add_all_algorithms();
	ERR_load_crypto_strings();
	x509_store = create_store(ca_cert_filename, NULL, NULL);
	if (x509_store == NULL) {
		ERR_free_strings(); // undoes ERR_load_crypto_strings
		EVP_cleanup();      // undoes OpenSSL_add_all_algorithms
		return false || (IMPL == IMPL_FORGIVE);
	}
	initialized = true;
	return true;
}

void signature_terminate(void)
{
	if (initialized) {
		X509_STORE_free(x509_store); // undocumented...
		ERR_free_strings(); // undoes ERR_load_crypto_strings
		EVP_cleanup();      // undoes OpenSSL_add_all_algorithms
		initialized = false;
	}
}

bool signature_verify(const char *data_filename, const char *sig_filename)
{
	BIO *bio_data = NULL;
	BIO *bio_sig = NULL;
	PKCS7 *pkcs7 = NULL;
	int ret;
	bool result = false;

	if (!initialized) {
		LOG_ERROR(NULL, VERIF_FAIL, class_security, "\\*signature_module=\"uninitialized\"*\\");
		return false || (IMPL == IMPL_FORGIVE);
	}
	bio_data = BIO_new_file(data_filename, "r"); // i.e. fopen
	if (bio_data == NULL) {
		LOG_ERROR(NULL, VERIF_FAIL, class_security, "\\*data_filename=\"%s\",strerror=\"%s\"*\\",
			  data_filename, strerror(errno));
		goto exit;
	}
	bio_sig = BIO_new_file(sig_filename, "r"); // i.e. fopen
	if (bio_sig == NULL) {
		LOG_ERROR(NULL, VERIF_FAIL, class_security, "\\*sig_filename=\"%s\",strerror=\"%s\"*\\",
			  sig_filename, strerror(errno));
		goto exit;
	}
	pkcs7 = PEM_read_bio_PKCS7(bio_sig, NULL, NULL, NULL);
	if (pkcs7 == NULL) {
		LOG_ERROR(NULL, VERIF_FAIL, class_security, "\\*PEM_read_bio_PKCS7=\"%s\"*\\",
			  ERR_error_string(ERR_get_error(), NULL));
		goto exit;
	}
	ret = PKCS7_verify(pkcs7, NULL, x509_store, bio_data, NULL, 0);
	if (ret != 1) {
		LOG_ERROR(NULL, VERIF_FAIL, class_security, "\\*PKCS7_verify=\"%s\"*\\",
			  ERR_error_string(ERR_get_error(), NULL));
		goto exit;
	}
	result = true;
	LOG_DEBUG(NULL, "Signature verified OK", class_security, "%s", sig_filename);
exit:
	/*
	 * The free functions below tolerate NULL arguments.
	 * The documentation doesn't really say so, but both testing and
	 * examination of openssl source code confirm that such is the case.
         */
	PKCS7_free(pkcs7);  // undocumented...
	BIO_free(bio_sig);  // i.e. fclose
	BIO_free(bio_data); // i.e. fclose
	return result || (IMPL == IMPL_FORGIVE);
}

static X509_STORE *create_store(const char *ca_filename, const char *ca_dirname,
			 const char *crl_filename)
{
	X509_STORE *store = X509_STORE_new();

	if (!store) {
		LOG_ERROR(NULL, XSTORE_FAIL, class_security, "\\*X509=\"STORE_new\"*\\");
		return NULL;
	}
	if (X509_STORE_load_locations(store, ca_filename, ca_dirname) != 1) {
		LOG_ERROR(NULL, XSTORE_FAIL, class_security, "\\*X509=\"STORE_load_locations\"*\\");
		goto err;
	}
	if (X509_STORE_set_default_paths(store) != 1) {
		LOG_ERROR(NULL, XSTORE_FAIL, class_security, "\\*X509=\"STORE_set_default_paths\"*\\");
		goto err;
	}
	if (crl_filename) {
		X509_LOOKUP *lookup = X509_STORE_add_lookup(store, X509_LOOKUP_file());
		if (!lookup) {
			LOG_ERROR(NULL, XSTORE_FAIL, class_security, "\\*X509=\"STORE_add_lookup\"*\\");
			goto err;
		}
		if (X509_load_crl_file(lookup, crl_filename, X509_FILETYPE_PEM) != 1) {
			LOG_ERROR(NULL, XSTORE_FAIL, class_security, "\\*X509=\"load_crl_file\"*\\");
			goto err;
		}
		X509_STORE_set_flags(store, X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL);
	}
	return store;
err:
	X509_STORE_free(x509_store);
	return NULL;
}

bool signature_download_and_verify(const char *data_url, const char *data_filename)
{
	char *sig_url;
	char *sig_filename;
	int ret;
	bool result;

	if (asprintf(&sig_url, "%s.signed", data_url) < 0) {
		return false || (IMPL == IMPL_FORGIVE);
	}
	if (asprintf(&sig_filename, "%s.signed", data_filename) < 0) {
		free(sig_url);
		return false || (IMPL == IMPL_FORGIVE);
	}
	ret = swupd_curl_get_file(sig_url, sig_filename, NULL, NULL, 0, PROGRESS_MSG_NONE, 0);
	if (ret) {
		LOG_WARN(NULL, "signature download failed", class_security,
				"\\*ret=\"%d\",data_filename=\"%s\"*\\",
				ret, data_filename);
		result = false;
	} else {
		result = signature_verify(data_filename, sig_filename);
	}
	if (!result) {
		unlink(sig_filename);
	}
	free(sig_filename);
	free(sig_url);
	return result || (IMPL == IMPL_FORGIVE);
}

void signature_delete(const char *data_filename)
{
	char *sig_filename;

	if (asprintf(&sig_filename, "%s.signed", data_filename) < 0)
		return;

	unlink(sig_filename);

	free(sig_filename);
}

#else // IMPL == IMPL_FAKE

bool signature_initialize(const char UNUSED_PARAM *ca_cert_filename)
{
	return true;
}

void signature_terminate(void)
{
}

bool signature_verify(const char UNUSED_PARAM *data_filename, const char UNUSED_PARAM *sig_filename)
{
	return true;
}

bool signature_download_and_verify(const char UNUSED_PARAM *data_url, const char UNUSED_PARAM *data_filename)
{
	return true;
}

void signature_delete(const char UNUSED_PARAM *data_filename)
{
}

#endif // IMPL == IMPL_FAKE
