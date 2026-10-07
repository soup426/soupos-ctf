#pragma once

/* In-kernel assertions, run by the `sample` shell command. Returns the number
 * of failures, and klogs one machine-readable summary line the smoke gate
 * asserts on. Self-contained: leaves nothing behind, so it is safe to run on a
 * live machine rather than needing a special boot. */
int selftest_run(int slow);   /* slow: also the checks that take seconds */
