use strict;
use warnings;

# this test was generated with Dist::Zilla::Plugin::Test::EOL 0.18

use Test::More 0.88;
use Test::EOL;

my @files = (
    'lib/YAML/Tiny.pm',
    't/00-report-prereqs.dd',
    't/00-report-prereqs.t',
    't/01_api.t',
    't/01_compile.t',
    't/10_read.t',
    't/11_read_string.t',
    't/12_write.t',
    't/13_write_string.t',
    't/20_subclass.t',
    't/21_yamlpm_compat.t',
    't/30_yaml_spec_tml.t',
    't/31_local_tml.t',
    't/32_world_tml.t',
    't/86_fail.t',
    't/lib/TestBridge.pm',
    't/lib/TestML/Tiny.pm',
    't/lib/TestUtils.pm',
    't/tml'
);

eol_unix_ok($_, { trailing_whitespace => 1 }) foreach @files;
done_testing;
