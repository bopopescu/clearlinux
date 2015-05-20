\name{readers}
\alias{readBytes}
\alias{readChars}
\title{Read Byte or Character Strings}
\description{
  Read byte or character strings from a connection.
}
\usage{
readBytes(con)
readChars(con, encoding = "")
}
\arguments{
  \item{con}{a \link{connection} object or a character string naming a
    file.}
  \item{encoding}{encoding to be assumed for input.}
}
\details{
  Both functions first read the raw bytes from the input connection into
  a character string.  \code{readBytes} then sets the \link{Encoding} of
  this to \code{"bytes"}; \code{readChars} uses \code{\link{iconv}} to
  convert from the specified input encoding to UTF-8 (replacing
  non-convertible bytes by their hex codes).
}
\value{
  For \code{readBytes}, a character string marked as \code{"bytes"}.
  For \code{readChars}, a character string marked as \code{"UTF-8"} if
  containing non-ASCII characters.
}
\seealso{
  \link{Encoding}
}
\keyword{IO}
