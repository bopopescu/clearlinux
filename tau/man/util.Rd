\name{util}
\alias{tokenize}
\alias{remove_stopwords}
\title{Preprocessing of Text Documents}
\description{
  Functions for common preprocessing tasks of text documents,
}
\usage{
tokenize(x, lines = FALSE, eol = "\n")
remove_stopwords(x, words, lines = FALSE)
}
\arguments{
  \item{x}{a vector of character.}
  \item{eol}{the end-of-line character to use.}
  \item{words}{a vector of character (tokens).}
  \item{lines}{assume the components are lines of text.}
}
\details{
  \code{tokenize} is a simple regular expression based parser that
  splits the components of a vector of character into tokens while
  protecting infix punctuation. If \code{lines = TRUE} assume \code{x}
  was imported with \code{readLines} and end-of-line markers need to be
  added back to the components.

  \code{remove_stopwords} removes the tokens given in \code{words} from
  \code{x}. If \code{lines = FALSE} assumes the components of both
  vectors contain tokens which can be compared using \code{match}.
  Otherwise, assumes the tokens in \code{x} are delimited by word
  boundaries (including infix punctuation) and uses regular expression
  matching.
}
\value{
  The same type of object as \code{x}.
}
\author{Christian Buchta}
\examples{
txt <- "\"It's almost noon,\" it@dot.net said."
## split
x <- tokenize(txt)
x
## reconstruct
t <- paste(x, collapse = "")
t

if (require("tm", quietly = TRUE)) {
    words <- readLines(system.file("stopwords", "english.dat",
                       package = "tm"))
    remove_stopwords(x, words)
    remove_stopwords(t, words, lines = TRUE)
} else
    remove_stopwords(t, words = c("it", "it's"), lines = TRUE)
}
\keyword{utilities}
\keyword{character}
