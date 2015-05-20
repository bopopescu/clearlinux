use strict;
use warnings;

# this test was generated with Dist::Zilla::Plugin::Test::EOL 0.17

use Test::More 0.88;
use Test::EOL;

my @files = (
    'lib/Test/Warnings.pm',
    't/00-report-prereqs.dd',
    't/00-report-prereqs.t',
    't/01-basic.t',
    't/02-done_testing.t',
    't/03-subtest.t',
    't/04-no-tests.t',
    't/05-no-end-block.t',
    't/06-skip-all.t',
    't/07-no_plan.t',
    't/08-use-if.t',
    't/09-warnings-contents.t',
    't/10-no-done_testing.t',
    't/11-double-use.t',
    't/12-no-newline.t',
    't/zzz-check-breaks.t'
);

eol_unix_ok($_, { trailing_whitespace => 1 }) foreach @files;
done_testing;
