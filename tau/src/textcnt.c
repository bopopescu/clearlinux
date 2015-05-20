
// This is a slight nightmare
//
// ceeboo 2008 - 2010

#undef __DEBUG
#define USE_RINTERNALS

#include <R.h>
#include <Rinternals.h>
#include <time.h>

// character translation

#define __TRANSLATE

// the encoding of the locale
static Rboolean known_to_be_utf8   = FALSE;
static Rboolean known_to_be_latin1 = FALSE;

static Rboolean use_bytes = FALSE;

// workaround missing API functions [2009/8]
static Rboolean utf8locale(void) {
    return  *LOGICAL(VECTOR_ELT(eval(LCONS(install("l10n_info"), R_NilValue),
		R_GlobalEnv), 1));
}

static Rboolean latin1locale(void) {
    return  *LOGICAL(VECTOR_ELT(eval(LCONS(install("l10n_info"), R_NilValue),
		R_GlobalEnv), 2));
}

/* FIXME
 *
 * although the code is still in the source code it is no longer
 * accessible since R-2.7.0.
 *
for details see src/extra/pcre/pcre_valid.c in the R source code.

Arguments:
  string       points to the string
  length       length of string, or -1 if the string is zero-terminated

Returns:       < 0    if the string is a valid UTF-8 string
               >= 0   otherwise; the value is the offset of the bad byte
 */

extern long tau_pcre_valid_utf8(const unsigned char *string, long length);

// character indexed prefix tree

typedef struct cpnode {
    unsigned char index;
    int  count;
    struct cpnode *pl;
    struct cpnode *pr;
} CPN;

static CPN *rpn = 0, *lpn;	// pointer to tree
static int ncpn = 0;		// number of nodes in tree
static int nap, inc;		// number of strings processed

/* add string c to the tree p. note that the
 * counters of all the nodes on the prefix
 * path are incremented.
 */

static CPN *cpncount(CPN *p, const unsigned char *c) {
    if (!*c) {
	nap++;
	return p;
    }
    if (!p) {
	p = lpn = (CPN *) malloc(sizeof(CPN));
	if (!p)
	    return p;
	ncpn++;
	p->index = *c;
	p->count = inc;
	p->pr    = 0;
	p->pl    = cpncount(0, c+1);
    } else
    if (p->index == *c) {
	lpn = p;
	p->count += inc;
	p->pl    = cpncount(p->pl, c+1);
    } else
    if (p->index  < *c) {
	p->pr    = cpncount(p->pr, c);
    } else {
	CPN *q = lpn = (CPN *) malloc(sizeof(CPN));
	if (!q)
	    return q;
	ncpn++;
	q->index = *c;
	q->count = inc;
	q->pr    = p;
	q->pl    = cpncount(0, c+1);
	return q;
    }
    return p;
}

// and free the tree

static void cpnfree(CPN *p) {
    if (!p)
	return;
    cpnfree(p->pr);
    cpnfree(p->pl);
    free(p);
    ncpn--;
}

#define __CBUF_SIZE 1024

static SEXP rval, nval;			// pointer to pairlist
static int tcnt = 0;			// threshold for retrieval
static unsigned char enc;		// encoding
static unsigned char cbuf[__CBUF_SIZE];	// token buffer

// 

static cetype_t get_known_encoding(void) {
    if (use_bytes)
	return CE_NATIVE;
    if (enc) {
	if (known_to_be_utf8)
	    return CE_UTF8;
	if (known_to_be_latin1)
	    return CE_LATIN1;
    }
    return CE_NATIVE;
}

/*
 * we are damned if we do and we are screwed if we
 * don't set the encoding bit. note that R-2.6.2
 * does not indicate ASCII.
 *
 * switched from pairlists to vectors to avoid weird
 * memory problems. note that the number of nodes is
 * our best upper bound on the number of results.
 * clearly, if inc = 0 and/or lower > 0 the number
 * of results may be much lower and thus more memory
 * will be wasted. note that the alternative of 
 * traversing the tree twice comes at the cost of
 * increased runtime and code complexity and thus
 * is currently not considered. [2010/02]
 */

static void cpnretprefix(CPN *p, int n) {
    if (!p)
	return;
    if (n >= __CBUF_SIZE - 1) {
	cpnfree(p);
	return;
    }
    unsigned char cen = enc;

    enc |= (p->index > 0x7F);
    cbuf[n] = p->index;
#ifdef __DEBUG
    cbuf[n+1] = 0;
    Rprintf(" %3i %i %s\n", p->count, enc, cbuf);
#endif
    if (p->count > tcnt) {
	if (use_bytes ||
	    !known_to_be_utf8 ||
	    !p->pl || (p->pl->index & 0xC0) != 0x80) {
	    INTEGER(rval)[nap] = p->count;
#ifndef __DEBUG
	    cbuf[n+1] = 0;
#endif
	    SET_STRING_ELT(nval, nap, mkCharCE((const char *) cbuf,
					       get_known_encoding()));
	    nap++;
	}
	cpnretprefix(p->pl, n+1);
    } else
	if (inc)
	    cpnfree(p->pl);
	else
	    cpnretprefix(p->pl, n+1);
    enc = cen;
    cpnretprefix(p->pr, n);
    free(p);
    ncpn--;
}

/* count all ngrams up to a given number n and
 * a given list of character vectors in UTF-8
 * encoding x.
 *
 * returns a vector with counts greater than
 * lower and the gram strings as names, or
 * R-level NULL.
 *
 * note that we try to copy the strings to a
 * fixed length buffer.
 *
 * FIXME 1) persistent is not thread-safe but
 *       for now simpler than handing around 
 *       external pointers.
 *
 *       2) R_CheckUserInterrupt() does not
 *       allow a thread-safe implemention.
 *
 *       3) translateChar() may use up the R
 *       memory stack.
 */

static Rboolean persistent = FALSE;

static void error_reset(const char *msg) {
    cpnfree(rpn);
    rpn = 0;
    persistent = FALSE;
    error(msg);
}

SEXP R_utf8CountNgram(SEXP x, SEXP R_n, SEXP R_lower, SEXP R_verbose,
		      SEXP R_persistent, SEXP R_useBytes) {
    if (!persistent && rpn) {
	cpnfree(rpn);
	rpn = 0;
	warning("cleaning up stale state");
    }
    if (isNull(x) || TYPEOF(x) != VECSXP)
	error("'x' not of type list");
    if (isNull(R_n) || TYPEOF(R_n) != INTSXP)
	error("'n' not of type integer");
    if (isNull(R_lower) || TYPEOF(R_lower) != INTSXP)
	error("'lower' not of type integer");
    if (isNull(R_verbose) || TYPEOF(R_verbose) != LGLSXP)
	error("'verbose' not of type logical");
    if (isNull(R_persistent) || TYPEOF(R_persistent) != LGLSXP)
	error("'persistent' not of type logical");
    if (isNull(R_useBytes) || TYPEOF(R_useBytes) != LGLSXP)
	error("'useBytes' not of type logical");
    long l;
    int h, i, j, k, m, n;
    const unsigned char *c;
    SEXP r, s;

    if (!persistent) {
	known_to_be_utf8   = utf8locale();
	known_to_be_latin1 = latin1locale();
	use_bytes          = *LOGICAL(R_useBytes);
    } else
    if (use_bytes != *LOGICAL(R_useBytes))
	error("change of useBytes in persistent mode");
    else
    if (known_to_be_utf8   != utf8locale() ||
	known_to_be_latin1 != latin1locale())
	error_reset("change of locale in persistent mode");
    persistent = LOGICAL(R_persistent)[0];

    n = INTEGER(R_n)[0];
    if (n < 0) 
	error_reset("'n' invalid value");
    if (n == 0)
	return R_NilValue;

    if (!persistent) {
	tcnt = INTEGER(R_lower)[0];
	if (tcnt < 0)
	    error_reset("'lowr' invalid value");
    }

#ifdef _TIME_H
    clock_t t2, t1, t0 = clock();
    if (LOGICAL(R_verbose)[0] == TRUE) {
	Rprintf("counting ...");
#ifdef __DEBUG
	Rprintf("\n");
#endif
    }
#endif

    nap = 0;
    inc = 1;

    for (i = 0; i < LENGTH(x); i++) {
	r = VECTOR_ELT(x, i);
	if (TYPEOF(r) != STRSXP)
	    error_reset("not of type character");
	for (j = 0; j < LENGTH(r); j++) {
	    s = STRING_ELT(r, j);
	    l = LENGTH(s);
	    if (s == NA_STRING || !l)
		continue;
#ifdef __TRANSLATE
	    if (!use_bytes) {
		c = (const unsigned char *) translateChar(s);
		l = strlen((const char *) c);
	    } else
		c = (const unsigned char *) CHAR(s);
#else
	    c = (const unsigned char *) CHAR(s);
#endif
	    // strings of unknown encoding are not translated
	    // or strings marked as UTF-8 could be invalid, so
	    // we have to check.
	    if (!use_bytes &&
		known_to_be_utf8 &&
		tau_pcre_valid_utf8(c, l) >= 0)
		error_reset("not a valid UTF-8 string");
	    /* in an UTF-8 multibyte sequence any byte
	     * except the first has 10 as its leading bits.
	     * thus, 1) the byte cannot be the start of a
	     * suffix and 2) we have to expand the current
	     * window.
	     *
	     * '\1' is a special boundary marker that triggers
	     * reduced counting, ie omission of windows which
	     * start at a boundary and shrinkage of windows
	     * which end at a boundary.
	     */
	    int b;
	    for (k = 0; k < l; k++) {
		if (c[k] == '\0')
		    continue;
		if (!use_bytes &&
		    known_to_be_utf8 &&
		    (c[k] & 0xC0) == 0x80)
		    continue;
		if (k == 1 && c[0] == '\1')
		    continue;
		h = 0;
		m = k;
		b = k;
		while (m < l) {
		    if (m-k < __CBUF_SIZE)
			cbuf[m-k] = c[m];
		    else 
			error_reset("cannot copy string to buffer");
		    if (use_bytes ||
			!known_to_be_utf8 ||
			(c[m] & 0xC0) != 0x80) {
			h++;
			if (h > n) {
			    h--;
			    if (c[m] == '\1') {
				h--;
				m = b;
			    }
			    break;
			}
			b = m;
		    }
		    m++;
		}
		cbuf[m-k] = 0;
#ifdef __DEBUG
		Rprintf(" %i %i %i %s\n", k+1, m, h, cbuf);
#endif
		h = nap + 1;
		rpn = cpncount(rpn, cbuf);

		if (nap != h)
		    error_reset("cannot add string to tree");
	    }
	}
	R_CheckUserInterrupt();
    }
#ifdef _TIME_H
    t1 = clock();
    if (LOGICAL(R_verbose)[0] == TRUE) {
	Rprintf(" %i string(s) using %i nodes [%.2fs]\n", nap, ncpn,
		((double) t1 - t0) / CLOCKS_PER_SEC);
	if (!persistent)
	    Rprintf("writing  ...");
#ifdef __DEBUG
	Rprintf("\n");
#endif
    }
#endif
    if (persistent)
	return R_NilValue;

    nap  = enc = 0;
    rval = PROTECT(allocVector(INTSXP, ncpn));
    setAttrib(rval, R_NamesSymbol, (nval = allocVector(STRSXP, ncpn)));

    cpnretprefix(rpn, 0);

    if (ncpn) {
	cpnfree(rpn);
	rpn = 0;
	error("cannot retrieve count(s)");
    }
    rpn = 0;
#ifdef _TIME_H
    t2 = clock();
    if (LOGICAL(R_verbose)[0] == TRUE)
	Rprintf(" %i strings [%.2fs]\n", nap,
		((double) t2 - t1) / CLOCKS_PER_SEC);
#endif
    // reduce
    if (nap < LENGTH(rval)) {
	r = PROTECT(allocVector(INTSXP, nap));
	setAttrib(r, R_NamesSymbol, (s = allocVector(STRSXP, nap)));
	while (nap-- > 0) {
	    INTEGER(r)[nap] = INTEGER(rval)[nap];
	    SET_STRING_ELT(s, nap, STRING_ELT(nval, nap));
	}
	UNPROTECT(2);

	return r;
    }
    UNPROTECT(1);

    return rval;
}

// copy at most n multibytes from the reversed
// sequence to the buffer.

static long reverse_copy_utf8(const unsigned char *x, long l, long n) {
    int h = 0, m = 0;
    if (l < 0)
	l = strlen((const char *) x);
    if (n < 0)
	n = l;
    while (l-- > 0 && n > 0) {
	if (x[l] == '\0')
	    continue;
	if (h < __CBUF_SIZE - 1)
	    cbuf[h] = x[l];
	else 
	    break;
	if (!use_bytes &&
	    known_to_be_utf8) {
	    if ((x[l] & 0xC0) == 0x80)
		m++;
	    else {
		if (m) {
		    unsigned char t, *q, *p = cbuf + h;
		    int k = (m + 1) / 2;
		    while (k--) {
			q = p-m--;
			t = *p;
			*p++ = *q;
			*q = t;
		    }
		    m = 0;
		}
		n--;
	    }
	} else
	    n--;
	h++;
    }
    cbuf[h] = 0;
    return (n) ? l : -1;
}

// count strings x, their prefixes or suffixes
// and return counts greater than lower.
//
// note that for string counting the number of
// nodes usually will be much greater than the
// number of result strings. however, we could
// not determine a better upper bound without 
// traversing the tree.
SEXP R_utf8CountString(SEXP x, SEXP R_n, SEXP R_lower, SEXP R_type,
		       SEXP R_verbose, SEXP R_persistent, SEXP R_useBytes) {
    if (!persistent && rpn) {
	cpnfree(rpn);
	rpn = 0;
	warning("cleaning up stale state");
    }
    if (isNull(x) || TYPEOF(x) != VECSXP)
	error("'x' not of type list");
    if (isNull(R_n) || TYPEOF(R_n) != INTSXP)
	error("'n' not of type integer");
    if (isNull(R_lower) || TYPEOF(R_lower) != INTSXP)
	error("'lower' not of type integer");
    if (isNull(R_type) || TYPEOF(R_type) != INTSXP)
	error("'type' not of type integer");
    if (isNull(R_verbose) || TYPEOF(R_verbose) != LGLSXP)
	error("'verbose' not of type logical");
    if (isNull(R_persistent) || TYPEOF(R_persistent) != LGLSXP)
	error("'persistent' not of type logical");
    if (isNull(R_useBytes) || TYPEOF(R_useBytes) != LGLSXP)
	error("'useBytes' not of type logical");
    long l, n = 0;
    int h, i, j, k, type;
    const unsigned char *c;
    SEXP r, s;

    if (!persistent) {
	known_to_be_utf8   = utf8locale();
	known_to_be_latin1 = latin1locale();
	use_bytes          = *LOGICAL(R_useBytes);
    } else
    if (use_bytes != *LOGICAL(R_useBytes))
	error("change of useBytes in persistent mode");
    else
    if (known_to_be_utf8   != utf8locale() ||
        known_to_be_latin1 != latin1locale())
	error_reset("change of locale in persistent mode");
    persistent = LOGICAL(R_persistent)[0];

    if (!persistent) {
	tcnt = INTEGER(R_lower)[0];
	if (tcnt < 0)
	    error_reset("'lower' invalid value");
    }

    type = INTEGER(R_type)[0];

    switch (type) {
	case 0:				// strings
	    inc = 0;
	    break;
	case 1:				// prefixes
	case 2:				// suffixes
	case 3:
	    n = INTEGER(R_n)[0];
	    if (n < 0) 
		error_reset("'n' invalid value");
	    if (n == 0)
		return R_NilValue;
	    inc = 1;
	    break;
	default:
	    error_reset("'type' invalid value");
    }
#ifdef _TIME_H
    clock_t t2, t1, t0 = clock();
    if (LOGICAL(R_verbose)[0] == TRUE) {
	Rprintf("counting ...");
#ifdef __DEBUG
	Rprintf("\n");
#endif
    }
#endif

    nap = 0;

    for (i = 0; i < LENGTH(x); i++) {
	r = VECTOR_ELT(x, i);
	if (TYPEOF(r) != STRSXP)
	    error_reset("not of type character");
	for (j = 0; j < LENGTH(r); j++) {
	    s = STRING_ELT(r, j);
	    l = LENGTH(s);
	    if (s == NA_STRING || !l)
		continue;
#ifdef __TRANSLATE
	    if (!use_bytes) {
		c = (const unsigned char *) translateChar(s);
		l = strlen((const char *) c);
	    } else
		c = (const unsigned char *) CHAR(s);
#else
	    c = (const unsigned char *) CHAR(s);
#endif
	    if (!use_bytes &&
		known_to_be_utf8 &&
		tau_pcre_valid_utf8(c, l) >= 0)
		error_reset("not a valid UTF-8 string");
	    if (type > 1) {
		if (reverse_copy_utf8(c, l, n) >= 0)
		    error_reset("cannot copy string to buffer");
	    } else {
		if (type < 1)
		    n = l;
		h = 0;
		for (k = 0; k < l; k++) {
		    if (c[k] == '\0')
			continue;
		    if (k < __CBUF_SIZE - 1)
			cbuf[k] = c[k];
		    else 
			error_reset("cannot copy string to buffer");
		    if (use_bytes ||
			!known_to_be_utf8 ||
			(c[k] & 0xC0) != 0x80) {
			h++;
			if (h > n)
			    break;
		    }
		}
		cbuf[k] = 0;
	    }
#ifdef __DEBUG
	    Rprintf(" %s\n", cbuf);
#endif
	    h = nap + 1;
	    lpn = 0;
	    rpn = cpncount(rpn, cbuf);

	    if (nap != h)
		error_reset("cannot add string to tree");

	    if (!inc) {
		if (lpn)	    // should never be NULL
		    lpn->count++;
	    }
	}
	R_CheckUserInterrupt();
    }
#ifdef _TIME_H
    t1 = clock();
    if (LOGICAL(R_verbose)[0] == TRUE) {
	Rprintf(" %i string(s) using %i nodes [%.2fs]\n", nap, ncpn,
		((double) t1 - t0) / CLOCKS_PER_SEC);
	if (!persistent)
	    Rprintf("writing  ...");
#ifdef __DEBUG
	Rprintf("\n");
#endif
    }
#endif
    if (persistent)
	return R_NilValue;

    nap  = enc = 0;
    rval = PROTECT(allocVector(INTSXP, ncpn));
    setAttrib(rval, R_NamesSymbol, (nval = allocVector(STRSXP, ncpn)));

    cpnretprefix(rpn, 0);

    if (ncpn) {
	cpnfree(rpn);
	rpn = 0;
	error_reset("cannot retrieve count(s)");
    }
    rpn = 0;

    // reverse the reversed strings
    if (type == 2)
	for (i = 0; i < nap; i++) {
	    s = STRING_ELT(nval, i);
	    reverse_copy_utf8((const unsigned char *) CHAR(s), LENGTH(s), -1);
	    SET_STRING_ELT(nval, i, mkCharCE((const char *) cbuf, 
					     getCharCE(s)));
	}
#ifdef _TIME_H
    t2 = clock();
    if (LOGICAL(R_verbose)[0] == TRUE)
	Rprintf(" %i strings [%.2fs]\n", nap,
		((double) t2 - t1) / CLOCKS_PER_SEC); 
#endif
    // reduce
    if (nap < LENGTH(rval)) {
	r = PROTECT(allocVector(INTSXP, nap));
	setAttrib(r, R_NamesSymbol, (s = allocVector(STRSXP, nap)));
	while (nap-- > 0) {
	    INTEGER(r)[nap] = INTEGER(rval)[nap];
	    SET_STRING_ELT(s, nap, STRING_ELT(nval, nap));
	}
	UNPROTECT(2);

	return r;
    }
    UNPROTECT(1);

    return rval;
}

/* copy a list of character x and truncate the
 * character vectors to a most n components.
 *
 * note that we copy the attributes, i.e. we
 * assume they do not depend semantically on
 * the object(s).
 */

SEXP R_copyTruncate(SEXP x, SEXP R_n) {
    if (isNull(x) || TYPEOF(x) != VECSXP)
	error("'x' not of type list");
    if (isNull(R_n) || TYPEOF(R_n) != INTSXP)
	error("'n' not of type integer");
    int i, k, n;
    SEXP s, r, t = 0;

    n = INTEGER(R_n)[0];
    if (n < 0)
	error("'n' invalid value");

    r = PROTECT(allocVector(VECSXP, LENGTH(x)));

    for (i = 0; i < LENGTH(x); i++) {
	s = VECTOR_ELT(x, i);
	if (TYPEOF(s) != STRSXP)
	    error("component not of type character");
	if (LENGTH(s) > n) {
	    SET_VECTOR_ELT(r, i, (t = allocVector(STRSXP, n)));
	    for (k = 0; k < n; k++)
		SET_STRING_ELT(t, k, STRING_ELT(s, k));
	    copyMostAttrib(t, s);
	    if ((s = getAttrib(s, R_NamesSymbol)) != R_NilValue) {
		SEXP v;
		setAttrib(t, R_NamesSymbol, (v = allocVector(STRSXP, n)));
		for (k = 0; k < n; k++)
		    SET_STRING_ELT(v, k, STRING_ELT(s, k));
	    }
	} else
	    SET_VECTOR_ELT(r, i, s);
    }
    UNPROTECT(1);

    if (!t)
	return x;
    
    SET_ATTRIB(r, ATTRIB(x));
    SET_OBJECT(r, OBJECT(x));
    if (IS_S4_OBJECT(x))
	SET_S4_OBJECT(r);

    return r;
}

// move a window of length n over a vector of
// character and copy each into a list.

SEXP R_copyToNgram(SEXP x, SEXP R_n) {
    if (TYPEOF(x) != STRSXP)
	error("'x' not of type character");
    if (TYPEOF(R_n) != INTSXP)
	error("'n' not of type integer");
    int i, j, k, n;
    SEXP s, r;
    
    n = *INTEGER(R_n);
    if (n < 1)
	error("'n' invalid value");
    if (n > LENGTH(x))
	return allocVector(VECSXP, 0);

    r = PROTECT(allocVector(VECSXP, LENGTH(x) - n + 1));

    for (i = 0; i < LENGTH(x) - n + 1; i++) {
	SET_VECTOR_ELT(r, i, (s = allocVector(STRSXP, n)));
	k = 0;
	for (j = i; j < i + n; j++)
	    SET_STRING_ELT(s, k++, STRING_ELT(x, j));
    }
    UNPROTECT(1);

    return r;
}

// remove blank strings from a vector of character.

SEXP R_removeBlank(SEXP x) {
    if (TYPEOF(x) != STRSXP)
	error("'x' not of type character");
    int i, n;

    n = 0;
    for (i = 0; i < LENGTH(x); i++)
	if (STRING_ELT(x, i) == R_BlankString)
	    n++;
    if (n) {
	SEXP r = allocVector(STRSXP, LENGTH(x) - n);

	n = 0;
	for (i = 0; i < LENGTH(x); i++)
	    if (STRING_ELT(x, i) != R_BlankString)
		SET_STRING_ELT(r, n++, STRING_ELT(x, i));

	return r;	
    }

    return x;
}

//
