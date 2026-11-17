# zappy-log

A simple C++20 header-only structured logging library.

Zappy implements structured logging: a log record has a message, a level and
typed key-value attributes. JSON output matches the shape of Go's
[zap](https://github.com/uber-go/zap), so the same log readers work for both.

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
lock. A disabled level costs a bit test and nothing else.

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

### Attributes

Values can be integers, floating point numbers, booleans, or anything
string-like (`std::string`, `std::string_view`, `char const*`; a null pointer
is written as `""`). They are written as JSON numbers, booleans and strings.

Bind attributes to a logger to add them to every message it writes:

```c++
auto dev = app.with_attributes({{"device", serial}, {"channel", 3}});
dev.info("acquired", {{"frames", 128}});   // device, channel, frames
```

To log a type of your own, specialize `zappy::value_formatter`:

```c++
template <> struct zappy::value_formatter<point> {
    void operator()(point const& p, std::string& out) const
    {
        out += std::format("({},{})", p.x, p.y);
    }
};
app.info("moved", {{"to", p}});              // "to":"(3,-4)"
```

### Levels

Loggers, cores and sinks each take a level set, fixed when they are created:
`zappy::levels(min)`, `zappy::levels(min, max)` (inclusive), or a list such as
`zappy::level_mask{level::debug, level::error}`.

```c++
auto quiet = zappy::logger{"app", core, zappy::levels(level::info)};
```

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

- When the queue is full, a log call waits for room: nothing is lost.
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

### Sinks that may drop

Wrap a sink in `best_effort` when it must never slow logging down, for
example a console that can stall:

```c++
#include <zappy/sinks/best_effort.hpp>

zappy::best_effort(zappy::stdout_sink(), 1024)
```

It gets its own thread and a buffer of 1024 lines. When the buffer is full,
lines for that sink are dropped and counted, and once it catches up it writes
one `messages dropped` line. Every other sink is reliable: it receives every
message, and `error()` and `flush()` wait for it.

### Custom sinks

`func_sink` calls a function with each formatted line:

```c++
#include <zappy/sinks/func.hpp>

zappy::func_sink([](std::string_view line) { send(line); },
                 [] { /* flush */ },
                 zappy::all_levels, zappy::json_format());
```

The core serializes all calls to a sink, so a sink never needs to lock.
To log from inside a sink into its own core, use `logger.weak()`, which does
not keep the core alive. Such messages are written after the current one; a
message logged while writing those is dropped.

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

zappy-log is header-only and needs C++20. With CMake:

```cmake
add_subdirectory(zappy-log)
target_link_libraries(app PRIVATE zappy-log)
```

The example and the tests are built only when zappy-log is the top-level
project: `cmake -S . -B build && cmake --build build && ctest --test-dir build`.
