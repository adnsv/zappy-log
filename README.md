# zappy-log

A simple C++ header-only structured logging library

Zappy implements structured logging, where log records include a message, a
severity level, and an optional set of key-value pairs.

## Overview

An application needs to instantiate a core that manages the queuing and routing
of logging messages.

The core is connected to various sinks, such as stdout/stderr sinks, file sinks,
etc.

The application then creates at least one logger to dispatch messages to the core.
Additional named loggers can be created to provide logical and functional
convenience. 

The library offers severity level filtering capabilities for both loggers and
sinks.

## Usage

To get started, create one or more sinks:

```c++
// jsonl file sink
auto all_fsink = zappy::rotating_json_file_sink("filename.jsonl");

// jsonl file sink that only logs error and critical messages
auto err_fsink = zappy::rotating_json_file_sink("filename.errors.jsonl", 
    zappy::levels(zappy::level::error, zappy::level::critical));

// console stdout sink for debug, info, and warn messages
auto console_out_sink = zappy::stdout_sink(
    zappy::levels(zappy::level::debug, zappy::level::warn)),

// console stderr sink for error messages
auto console_err_sink = zappy::stderr_sink(
    zappy::levels(zappy::level::error, zappy::level::critical)),   
```

Then create a core that outputs to these sinks:

```c++
auto core = zappy::make_core(
    64, // message queue capacity
    { all_fsink, err_fsink, console_out_sink, console_err_sink}
);
```

Now we are ready to create our loggers:

```c++
// application lifecycle events
auto app = zappy::logger{"app", core}; 

// communication events
auto com = zappy::logger{"com", core};
```

At this point, logging is configured and we can start producing 
the logs:

```c++
app.info("starting app");
app.info("hello world!", {{"key1", "value 1"}});

com.info("starting listener", {{"port", "8000"}});
```