\name{textcnt}
\alias{textcnt}
\alias{format.textcnt}
\title{Term or Pattern Counting of Text Documents}
\description{
  This function provides a common interface to perform typical term or
  pattern counting tasks on text documents.
}
\usage{
textcnt(x, n = 3L, split = "[[:space:][:punct:][:digit:]]+",
        tolower = TRUE, marker = "_", words = NULL, lower = 0L,
        method = c("ngram", "string", "prefix", "suffix"),
        recursive = FALSE, persistent = FALSE, useBytes = FALSE,
        perl = TRUE, verbose = FALSE, decreasing = FALSE)

\S3method{format}{textcnt}(x, ...)
}
\arguments{
  \item{x}{a (list of) vector(s) of character representing one (or more)
    text document(s).}
  \item{n}{the maximum number of characters considered in ngram,
    prefix, or suffix counting (for word counting see details).}
  \item{split}{the regular expression pattern (PCRE) to be used in word
    splitting (if \code{NULL}, do nothing).}
  \item{tolower}{option to transform the documents to lowercase (after
    word splitting).}
  \item{marker}{the string used to mark word boundaries.}
  \item{words}{the number of words to use from the beginning of a
    document (if \code{NULL}, all words are used).}
  \item{lower}{the lower bound for a count to be included in the result
    set(s).}
  \item{method}{the type of counts to compute.}
  \item{recursive}{option to compute counts for individual documents
    (default all documents).}
  \item{persistent}{option to count documents incrementally.}
  \item{useBytes}{option to process byte-by-byte instead of
    character-by-character.}
  \item{perl}{option to use PCRE in word splitting.}
  \item{verbose}{option to obtain timing statistics.}
  \item{decreasing}{option to return the counts in decreasing order.}
  \item{\dots}{further (unused) arguments.}
}
\details{
  The following counting methods are currently implemented:

  \describe{
    \item{\code{ngram}}{Count all word n-grams of order 1,\dots,\code{n}.}
    \item{\code{string}}{Count all word sequence n-grams of order \code{n}.}
    \item{\code{prefix}}{Count all word prefixes of at most length \code{n}.}
    \item{\code{suffix}}{Count all word suffixes of at most length \code{n}.}
  }

  The n-grams of a word are defined to be the substrings of length
  \code{n = min(length(word), n)} starting at positions
  1,\dots,\code{length(word)-n}.  Note that the value of \code{marker}
  is pre- and appended to word before counting. However, the empty word
  is never marked and therefore not counted. Note that
  \code{marker = "\1"} is reserved for counting of an efficient set
  of ngrams and \code{marker = "\2"} for the set proposed by Cavnar
  and Trenkle (see references).

  If \code{method = "string"} word-sequences of and only of length
  \code{n} are counted. Therefore, documents with less than \code{n}
  words are omitted.

  By default all documents are preprocessed and counted using a single C
  function call. For large document collections this may come at the
  price of considerable memory consumption. If \code{persistent = TRUE} and
  \code{recursive = TRUE} documents are counted incrementally, i.e., into a
  persistent prefix tree using as many C function calls as there are
  documents. Further, if \code{persistent = TRUE} and \code{recursive = FALSE}
  the documents are counted using a single call but no result is returned
  until the next call with \code{persistent = FALSE}. Thus, \code{persistent}
  acts as a switch with the counts being accumulated until release. Timing
  statistics have shown that incremental counting can be order of
  magnitudes faster than the default. 

  Be aware that the character strings in the documents are translated
  to the encoding of the current locale if the encoding is set (see
  \code{\link{Encoding}}). Therefore, with the possibility of \code{"unknown"}
  encodings when in an \code{"UTF-8"} locale, or invalid \code{"UTF-8"}
  strings declared to be in \code{"UTF-8"}, the code checks if each string
  is a valid \code{"UTF-8"} string and stops if not. Otherwise, strings
  are processed bytewise without any checks. However, embedded \code{nul}
  bytes are always removed from a string. Finally, note that during
  incremental counting a change of locale is not allowed (and a change
  in method is not recommended).

  Note that the C implementation counts words into a prefix tree.  Whereas this is highly efficient for n-gram, prefix, or suffix counting
  it may be less efficient for simple word counting. That is, implementations
  which use hash tables may be more efficient if the dictionary is large.

  \code{format.textcnt} pretty prints a named vector of counts (see below)
  including information about the rank and encoding details of the strings.
}
\value{
  Either a single vector of counts of mode \code{integer} with the names
  indexing the patterns counted, or a list of such vectors with the
  components corresponding to the individual documents. Note that by
  default the counts are in prefix tree (byte) order (for
  \code{method = "suffix"} this is the order of the reversed strings).
  Otherwise, if \code{decreasing = TRUE} the counts are sorted in 
  decreasing order. Note that the (default) order of ties is preserved
  (see \code{\link{sort}}).
}
\author{Christian Buchta}
\note{
  The C functions can be interrupted by \kbd{CTRL-C}. This is convenient in
  interactive mode but comes at the price that the C code cannot clean
  up the internal prefix tree. This is a known problem of the R API
  and the workaround is to defer the cleanup to the next function call.

  The C code calls \code{translateChar} for all input strings which is
  documented to release the allocated memory no sooner than when
  returning from the \code{.Call}/\code{.External} interface.
  Therefore, in order to avoid excessive memory consumption it is
  recommended to either translate the input data to the current locale
  or to process the data incrementally.

  \code{useBytes} may not be fully functional with R versions where
  \code{strsplit} does not support that argument.

  If \code{useBytes = TRUE} the character strings of \code{names} will
  never be declared to be in an encoding.
}
\references{
  W.B. Cavnar and J.M. Trenkle (1994).
  N-Gram Based Text Categorization.
  In Proceedings of SDAIR-94, 3rd Annual Symposium on Document
  Analysis and Information Retrieval, 161--175.
}
\examples{
## the classic
txt <- "The quick brown fox jumps over the lazy dog."

##
textcnt(txt, method = "ngram")
textcnt(txt, method = "prefix", n = 5L)

r <- textcnt(txt, method = "suffix", lower = 1L)
data.frame(counts = unclass(r), size = nchar(names(r)))
format(r)

## word sequences
textcnt(txt, method = "string")

## inefficient
textcnt(txt, split = "", method = "string", n = 1L)

## incremental
textcnt(txt, method = "string", persistent = TRUE, n = 1L)
textcnt(txt, method = "string", n = 1L)

## subset
textcnt(txt, method = "string", words = 5L, n = 1L)

## non-ASCII
txt <- "The quick br\xfcn f\xf6x j\xfbmps \xf5ver the lazy d\xf6\xf8g."
Encoding(txt) <- "latin1"
txt

## implicit translation
r <- textcnt(txt, method = "suffix")
table(Encoding(names(r)))
r
## efficient sets
textcnt("is",     n = 3L, marker = "\1")
textcnt("is",     n = 4L, marker = "\1")
textcnt("corpus", n = 5L, marker = "\1")
## CT sets
textcnt("corpus", n = 5L, marker = "\2")
}
\keyword{utilities}
\keyword{character}
