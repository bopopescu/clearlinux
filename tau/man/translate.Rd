\name{translate}
\alias{translate_Unicode_latin_ligatures}
\title{Translate Unicode Latin Ligatures}
\description{
  Translate Unicode \dQuote{Latin ligature} characters to their
  respective constituents.
}
\usage{translate_Unicode_latin_ligatures(x)}
\arguments{
  \item{x}{a character vector in UTF-8 encoding.}
}
\details{
  In typography, a ligature occurs where two or more
  graphemes are joined as a single glyph.  (See
  \url{http://en.wikipedia.org/wiki/Typographic_ligature} for more
  information.)

  Unicode (\url{http://www.unicode.org/}) lists the following
  \dQuote{Latin} ligatures:
  \tabular{rl}{
    Code \tab Name \cr
    0132 \tab LATIN CAPITAL LIGATURE IJ \cr
    0133 \tab LATIN SMALL LIGATURE IJ \cr
    0152 \tab LATIN CAPITAL LIGATURE OE \cr
    0153 \tab LATIN SMALL LIGATURE OE \cr
    FB00 \tab LATIN SMALL LIGATURE FF \cr
    FB01 \tab LATIN SMALL LIGATURE FI \cr
    FB02 \tab LATIN SMALL LIGATURE FL \cr
    FB03 \tab LATIN SMALL LIGATURE FFI \cr
    FB04 \tab LATIN SMALL LIGATURE FFL \cr
    FB05 \tab LATIN SMALL LIGATURE LONG S T \cr
    FB06 \tab LATIN SMALL LIGATURE ST
  }

  \code{translate_Unicode_latin_ligatures} translates these to their
  respective constituent characters.
}
\keyword{utilities}
\keyword{character}
