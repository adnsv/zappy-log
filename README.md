# zappy-log

A simple C++20 header-only structured logging library.

Zappy implements structured logging: a log record has a message, a level and
typed key-value attributes. JSON output uses the field names and level names
of Go's [zap](https://github.com/uber-go/zap) (`ts`, `logger`, `level`,
`msg`), so log readers built for zap read zappy's files too.

```json
{"ts":"2026-11-17T15:34:02.792Z","logger":"app","level":"info","msg":"acquired","device":"UT-4","channel":3,"elapsed_ms":42.5}
```

## Overview

- **Sinks** write formatted lines somewhere: the console, rotating files, or a
  callback. Each sink has a format (JSON, text, colored console text) and a
  set of levels it accepts.
- A **core** routes messages to its sinks. It is either synchronous (the
  default: a log call writes before it returns, no thread) or asynchronous (a
  worker thread writes). The logging code is the same for both.
- **Loggers** are the front end: a name, optional bound attributes and a set
  of levels. They are cheap to copy and safe to use from any thread.

Every log call formats its message on the calling thread, before taking any
lock. A call at a disabled level returns after two bit tests, without
formatting or allocating.

## Usage

Include `<zappy/logger.hpp>` and the sinks you need:

```c++
#include <zappy/logger.hpp>
#include <zappy/sinks/console.hpp>
#include <zappy/sinks/file.hpp>

using zappy::level;

auto core = zappy::make_core({
    zappy::stdout_sink(zappy::levels(level::debug, level::warn)),
    zappy::stderr_sink(zappy::levels(level::error)),
    zappy::rotating_json_file_sink("app.jsonl"),
});

auto app = zappy::logger{"app", core};

app.info("starting");
app.info("listening", {{"port", 8000}, {"tls", true}});
app.warn("slow response", {{"ms", 412.7}, {"path", path}});
```

Levels are `debug`, `info`, `warn`, `error` and `critical`. zap has no
critical level, so `critical` is written as `"fatal"`.

Timestamps are UTC, RFC 3339 with milliseconds: `2026-11-17T15:34:02.792Z`.

### Attributes

Values can be integers, floating point numbers, booleans, or anything
string-like (`std::string`, `std::string_view`, `char const*`; a null pointer
is written as `""`). They are written as JSON numbers, booleans and strings;
NaN and infinities are written as the strings `"NaN"`, `"+Inf"` and `"-Inf"`,
as zap does. A `char` is rejected at compile time: pass a string.

Bind attributes to a logger to add them to every message it writes, before
the call's own:

```c++
auto dev = app.with_attributes({{"device", serial}, {"channel", 3}});
dev.info("acquired", {{"frames", 128}});   // device, channel, frames
```

To log a type of your own, specialize `zappy::value_formatter`. The value is
written as a string:

```c++
template <> struct zappy::value_formatter<point> {
    void operator()(point const& p, std::string& out) const
    {
        out += std::format("({},{})", p.x, p.y);
    }
};
app.info("moved", {{"to", p}});              // "to":"(3,-4)"
```

Arguments are evaluated before the call, even at a disabled level. Guard
expensive ones:

```c++
if (app.should_log(level::debug))
    app.debug("state", {{"dump", expensive_dump()}});
```

### Levels

Loggers, cores and sinks each take a level set, fixed when they are created:
`zappy::levels(min)`, `zappy::levels(min, max)` (inclusive; reversed bounds
are swapped), or a list such as
`zappy::level_mask{level::debug, level::error}`.

```c++
auto quiet = zappy::logger{"app", core, zappy::levels(level::info)};
```

### Sinks

| Sink | Header | Writes |
|---|---|---|
| `stdout_sink(levels)`, `stderr_sink(levels)` | `sinks/console.hpp` | text, colored on a terminal |
| `rotating_json_file_sink(path, policy, levels)` | `sinks/file.hpp` | JSON lines |
| `rotating_text_file_sink(path, policy, levels)` | `sinks/file.hpp` | text lines |
| `rotating_file_sink(path, formatter, policy, levels)` | `sinks/file.hpp` | any format |
| `func_sink(write, flush, levels, formatter)` | `sinks/func.hpp` | calls your function |
| `best_effort(sink, capacity, on_error)` | `sinks/best_effort.hpp` | wraps a sink, see below |

Arguments after the first have defaults, except `rotating_file_sink`'s
formatter. Formatters are
`zappy::json_format()`, `zappy::text_format()` and `zappy::ansi_format(bool)`.

**Files** rotate by size: `{.max_size = 1 MiB, .max_count = 5}` by default.
`app.jsonl` becomes `app.1.jsonl`, `app.2.jsonl` and so on, oldest last.
`max_count = 1` truncates the file in place; `0` never rotates. Directories
are created as needed. A file that cannot be opened, or a rename that fails
(for example because another program holds the file open on Windows), never
loses what is already written: zappy keeps appending and retries later.

**Console** sinks use color when the stream is a terminal that supports it.
`NO_COLOR` turns color off, `TERM=dumb` too. On Windows the console's ANSI
processing is switched on as needed.

**Text** output quotes a key or value (`key="two words"`) when it is empty or
contains a space, `|`, `=` or `"`, so attributes can always be told apart.

### Synchronous and asynchronous cores

`make_core` creates a synchronous core. A log call formats the message and
writes it to every sink before it returns, and every sink is flushed after
each message. There is no thread and nothing to shut down: the right choice
for tools and small programs.

`make_async_core` creates a core with its own worker thread. A log call
formats the message, queues the bytes and returns; the worker writes them.

```c++
auto core = zappy::make_async_core({...}, {.capacity = 8192});
...
core->shutdown();   // write everything queued, flush, stop the worker
```

- When the queue is full, a log call waits for room, so nothing is lost
  (the one exception is logging from inside a sink, see Custom sinks).
- Messages at the `flush_on` levels (default: error and above) are written
  and flushed before the call returns, as on a synchronous core.
  `core->flush()` does the same for everything logged so far.
- The worker flushes the sinks whenever it runs out of work, and at least
  every 100 ms under continuous load.
- `shutdown()` is idempotent and is called when the last reference to the
  core goes away. Logging after shutdown writes synchronously on the caller's
  thread, so messages logged during program exit are kept.
- Call `shutdown()` before unloading a DLL that created a core, and do not
  `fork()` while an async core is running.

Every sink sees the messages of a core in the same order, on both kinds of
core.

Core options, all optional:

| Option | Default | |
|---|---|---|
| `levels` | all levels | messages the core accepts |
| `flush_on` | error and above | written and flushed before the log call returns |
| `on_error` | stderr, rate-limited | see Errors |
| `capacity` | 8192 | async only: queue length in messages |
| `manual` | false | async only: no thread; `core->pump()` writes (for tests) |

### Sinks that may drop

Wrap a sink in `best_effort` when it must never slow logging down, for
example a console that can stall (a Windows console in selection mode blocks
every write):

```c++
#include <zappy/sinks/best_effort.hpp>

auto core = zappy::make_async_core({
    zappy::best_effort(zappy::stdout_sink(), 1024),
    zappy::rotating_json_file_sink("app.jsonl"),
});
```

It gets its own thread and a buffer of 1024 lines. When the buffer is full,
lines for that sink are dropped and counted, and once it catches up it writes
one `messages dropped` line. Every other sink is reliable: it receives every
message, and `error()` and `flush()` wait for it.

If the wrapped sink fails (its write or flush throws), the wrapper reports it
through an optional third argument, an error handler like a core's `on_error`
(default: one line to stderr, rate-limited). The handler runs on the
wrapper's own thread.

### Custom sinks

`func_sink` calls a function with each formatted line, newline included:

```c++
#include <zappy/sinks/func.hpp>

zappy::func_sink([](std::string_view line) { send(line); },
                 [] { /* flush */ },
                 zappy::all_levels, zappy::json_format());
```

The core serializes all calls to a sink, so a sink never needs to lock.
To log from inside a sink into its own core, use `logger.weak()`, which does
not keep the core alive. Such messages are written after the current one (on
an async core they are queued, and dropped if the queue is full); a message
logged while writing those is dropped. Drops are reported through `on_error`.

### Errors

zappy never throws from a log call. A sink or formatter that fails is
reported through the core's `on_error` option; by default one line goes to
stderr, at most once every 10 s per problem:

```c++
auto core = zappy::make_core({...}, {
    .on_error = [](zappy::error_info const& e) { /* show it */ },
});
```

## Building

zappy-log is header-only and needs C++20. The CMake target is `zappy-log`:

```cmake
add_subdirectory(zappy-log)
target_link_libraries(app PRIVATE zappy-log)
```

**Why there are Go files in a C++ repository:** `go.mod` and `dummy.go` make
zappy-log a Go module, `github.com/adnsv/zappy-log`. Some C++ projects use Go's
module tooling as their package manager: they pin versions in their own
`go.mod`, and their CMake build finds each dependency's source directory with
`go list -m -json <module>`. The two files contain no Go code and do nothing
else; you can ignore them if you use plain CMake.

The example and the tests are built only when zappy-log is the top-level
project: `cmake -S . -B build && cmake --build build && ctest --test-dir build`.
`build/compile_commands.json` is written for clangd (see `.clangd`).
