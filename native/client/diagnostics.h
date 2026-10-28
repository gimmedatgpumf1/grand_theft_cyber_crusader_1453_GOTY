#pragma once

#include <string>

// A run log and a last-gasp handler. A client that dies without a word is a
// client nobody can fix, so this writes down what it was doing as it went and,
// on a hard fault, what the fault was and where it happened. The log lives next
// to the executable, because a console that closes on exit takes its output
// with it.

namespace gtc::native {

// Installs the fault handler and opens the log. Safe to call once, early.
void installCrashHandler(const char* program);

// One line in the log, also echoed to stderr.
void trace(const char* note);

// Same, with printf formatting.
void tracef(const char* format, ...);

// Where the log was written. Valid after installCrashHandler.
const char* logPath();

// Directory holding the executable, without a trailing separator. Empty if it
// cannot be determined.
std::string executableDirectory();

} // namespace gtc::native
