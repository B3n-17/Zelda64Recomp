#ifndef __CRASH_HANDLER_OOT_HPP__
#define __CRASH_HANDLER_OOT_HPP__

// Installs a process-wide handler that prints a symbolized backtrace on a fatal
// memory fault. Call once, as early in main() as possible.
void register_crash_handler();

#endif
