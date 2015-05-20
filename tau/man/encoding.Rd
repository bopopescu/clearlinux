\name{encoding}
\alias{is.utf8}
\alias{is.ascii}
\alias{is.locale}
\alias{translate}
\alias{fixEncoding}
\title{Adapt the (Declared) Encoding of a Character Vector}
\description{
  Functions for testing and adapting the (declared) encoding
  of the components of a vector of mode \code{character}.
}
\usage{
is.utf8(x)
is.ascii(x)
is.locale(x)

translate(x, recursive = FALSE, internal = FALSE)
fixEncoding(x, latin1 = FALSE)
}
\arguments{
  \item{x}{a vector (of character).}
  \item{recursive}{option to process list components.}
  \item{internal}{option to use internal translation.}
  \item{latin1}{option to assume \code{"latin1"} if the declared 
    encoding is \code{"unknown"}.}
}
\details{
  \code{is.utf8} tests if the components of a vector of character
  are true UTF-8 strings, i.e. contain one or more valid UTF-8
  multi-byte sequence(s).

  \code{is.locale} tests if the components of a vector of character
  are in the encoding of the current locale.

  \code{translate} encodes the components of a vector of \code{character}
  in the encoding of the current locale. This includes the \code{names}
  attribute of vectors of arbitrary mode. If \code{recursive = TRUE}
  the components of a \code{list} are processed. If \code{internal = TRUE}
  multi-byte sequences that are invalid in the encoding of the current
  locale are changed to literal hex numbers (see FIXME).

  \code{fixEncoding} sets the declared encoding of the components of
  a vector of character to their correct or preferred values. If
  \code{latin1 = TRUE} strings that are not valid UTF-8 strings are
  declared to be in \code{"latin1"}. On the other hand, strings that
  are true UTF-8 strings are declared to be in \code{"UTF-8"} encoding.
}
\value{
  The same type of object as \code{x} with the (declared) encoding
  possibly changed.
}
\references{FIXME PCRE, RFC 3629}
\author{Christian Buchta}
\note{
  Currently \code{translate} uses \code{iconv} and therefore is not
  guaranteed to work on all platforms.
}
\seealso{\code{\link{Encoding}} and \code{\link{iconv}}.}
\examples{
## Note that we assume R runs in an UTF-8 locale
text <- c("aa", "a\xe4")
Encoding(text) <- c("unknown", "latin1")
is.utf8(text)
is.ascii(text)
is.locale(text)
## implicit translation
text
##
t1 <- iconv(text, from = "latin1", to = "UTF-8")
Encoding(t1)
## oops
t2 <- iconv(text, from = "latin1", to = "utf-8")
Encoding(t2)
t2
is.locale(t2)
##
t2 <- fixEncoding(t2)
Encoding(t2)
## explicit translation
t3 <- translate(text)
Encoding(t3)
}
\keyword{utilities}
\keyword{character}
