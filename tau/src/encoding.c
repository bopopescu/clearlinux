
// ceeboo 2008

#include <R.h>

#define USE_RINTERNALS
#include <Rinternals.h>

// see textcnt.c

// workaround missing API functions [2009/8]
static Rboolean utf8locale(void) {
    return  *LOGICAL(VECTOR_ELT(eval(LCONS(install("l10n_info"), R_NilValue),
		R_GlobalEnv), 1));
}

static Rboolean latin1locale(void) {
    return  *LOGICAL(VECTOR_ELT(eval(LCONS(install("l10n_info"), R_NilValue),
		R_GlobalEnv), 2));
}

// FIXME
//
// adapt this function to indicate ASCII

extern long tau_pcre_valid_utf8(const unsigned char *string, long length);

static long _valid_ascii(const unsigned char *s, long l) {
    register const unsigned char *p;
    if (l < 0)
	l = strlen((const char *) s);
    for (p = s; l-- > 0; p++)
	if (*p > 0x7F)
	    return p - s;
    return -1;
}

// test for ASCII
//

SEXP R_isASCII(SEXP x) {
    if (TYPEOF(x) != STRSXP)
	error("'x' not of type character");
    int i, l;
    SEXP s, r = PROTECT(allocVector(LGLSXP, LENGTH(x)));

    for (i = 0; i < LENGTH(x); i++) {
	s = STRING_ELT(x, i);
	l = LENGTH(s);
	if (!l)
	    LOGICAL(r)[i] = TRUE;
	else
	if (_valid_ascii((const unsigned char *) CHAR(s), l) < 0)
	    LOGICAL(r)[i] = TRUE;
	else
	    LOGICAL(r)[i] = FALSE;
    }
    UNPROTECT(1);

    return r;
}


// test for strict UTF-8.
//
// shame on R that we have to provide this!

SEXP R_isUTF8(SEXP x) {
    if (TYPEOF(x) != STRSXP)
	error("'x' not of type character");
    int i, l;
    SEXP s, r = PROTECT(allocVector(LGLSXP, LENGTH(x)));

    for (i = 0; i < LENGTH(x); i++) {
	s = STRING_ELT(x, i);
	l = LENGTH(s);
	if (!l)
	    LOGICAL(r)[i] = FALSE;
	else
	if (tau_pcre_valid_utf8((const unsigned char *) CHAR(s), l) < 0) {
	    if (_valid_ascii((const unsigned char *) CHAR(s), l) < 0)
		LOGICAL(r)[i] = FALSE;
	    else
		LOGICAL(r)[i] = TRUE;
	}
	else
	    LOGICAL(r)[i] = FALSE;
    }

    UNPROTECT(1);

    return r;
}

// Adapt the declared encoding of a vector of strings
// to the most likely / desired scenario.

SEXP R_fixEncoding(SEXP x, SEXP R_latin1) {
    if (TYPEOF(x) != STRSXP)
	error("'x' not of type character");
    if (TYPEOF(R_latin1) != LGLSXP)
	error("'latin1' not of type logical");
    int assume_latin1 = LOGICAL(R_latin1)[0];
    int i, l, e, n;
    const unsigned char *c;
    SEXP s, r = PROTECT(allocVector(STRSXP, LENGTH(x)));

    n = 0;
    for (i = 0; i < LENGTH(x); i++) {
	s = STRING_ELT(x, i);
	l = LENGTH(s);
	e = getCharCE(s);
	if (l) {
	    c  = (const unsigned char *) CHAR(s);
	    if (tau_pcre_valid_utf8(c, l) < 0) {
		if (_valid_ascii(c, l) < 0) {
		    // known to be ASCII
		    if (e != CE_NATIVE)
			s = mkCharCE(CHAR(s), CE_NATIVE);
		}
		else {
		    // unlikely not to be UTF8 (cf. tau_pcre_valid_utf8)
		    if (e == CE_NATIVE)
			s = mkCharCE(CHAR(s), CE_UTF8);
		}
	    }
	    else {
		// known not to be ASCII or UTF8
		if (assume_latin1) {
		    if (e != CE_LATIN1)
			s = mkCharCE(CHAR(s), CE_LATIN1);
		}
		else
		    if (e != CE_NATIVE)
			s = mkCharCE(CHAR(s), CE_NATIVE);
	    }
	}
	else {
	    // the empty string does not
	    // have a known encoding
	    if (e != CE_NATIVE)
		s = mkCharCE(CHAR(s), CE_NATIVE);
	}
	SET_STRING_ELT(r, i, s);
	if (s != VECTOR_ELT(x, i))
	    n++;
	R_CheckUserInterrupt();
    }

    UNPROTECT(1);

    return (n) ? r : x;
}

// Test if the strings of a vector are in the encoding of
// the current locale.

SEXP R_isLocale(SEXP x) {
    if (TYPEOF(x) != STRSXP)
	error("'x' not of type character");
    int i, l;
    int known_to_be_latin1 = latin1locale();
    int known_to_be_utf8 = utf8locale();
    SEXP s, r = PROTECT(allocVector(LGLSXP, LENGTH(x)));

    for (i = 0; i < LENGTH(x); i++) {
	s = STRING_ELT(x, i);
	l = LENGTH(s);
	if (!l)
	    LOGICAL(r)[i] = TRUE;
	else
	if (known_to_be_utf8) {
	    if (tau_pcre_valid_utf8((const unsigned char *) CHAR(s), l) < 0)
		LOGICAL(r)[i] = TRUE;
	    else
		LOGICAL(r)[i] = FALSE;
	}
	else
	if (known_to_be_latin1)
	    LOGICAL(r)[i] = TRUE;
	else {
	    if (_valid_ascii((const unsigned char *) CHAR(s), l) < 0)
		LOGICAL(r)[i] = TRUE;
	    else
		LOGICAL(r)[i] = FALSE;
	}
    }
    UNPROTECT(1);

    return r;
}

// Translate the strings of a vector to the encoding of
// the current locale.
//
// Shame on R that we have to provide this!

SEXP R_translateToLocale(SEXP x) {
    if (TYPEOF(x) != STRSXP)
	error("'x' not of type character");
    int i, n;
    int known_to_be_latin1 = latin1locale();
    const char *c;
    SEXP s, r = PROTECT(allocVector(STRSXP, LENGTH(x)));
    
    n = 0;
    for (i = 0; i < LENGTH(x); i++) {
	s = STRING_ELT(x, i);
	c = translateChar(s);
	if (c != CHAR(s)) {
	    if (tau_pcre_valid_utf8((const unsigned char *) c, -1) < 0) {
		if (_valid_ascii((const unsigned char *) c, -1) < 0)
		    s = mkCharCE(c, CE_NATIVE);
		else
		    s = mkCharCE(c, CE_UTF8);
	    } else {
		if (known_to_be_latin1)
		    s = mkCharCE(c, CE_LATIN1);
		else
		    s = mkCharCE(c, CE_NATIVE);
	    }
	    n++;
	}
	SET_STRING_ELT(r, i, s);
	R_CheckUserInterrupt();
    }
    UNPROTECT(1);

    return (n) ? r : x;
}

//
