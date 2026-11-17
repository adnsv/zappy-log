// Copyright (c) 2024-2026 Alex Denisov
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <exception>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include <zappy/details/common.hpp>

namespace zappy {

// What went wrong inside zappy, reported through core_options::on_error.
enum class error_kind {
    sink_write, // a sink's write threw
    sink_flush, // a sink's flush threw
    format,     // a formatter threw; the message was not written to that sink
    dropped,    // messages were dropped (nested too deep, or a full queue
                // while logging from inside a sink)
};

struct error_info {
    error_kind kind;
    sink const* source = nullptr; // the sink involved, if any
    std::size_t count = 1;        // how many messages or failures
    std::string_view what;        // exception text or a short description
};

using error_handler = std::function<void(error_info const&)>;

struct core_options {
    level_mask levels = all_levels;
    // messages at these levels are written and flushed before the log call
    // returns (relevant to async cores; a sync core always does this)
    level_mask flush_on = zappy::levels(level::error);
    // default: one line to stderr, at most once per 10 s per problem
    error_handler on_error = {};
};

// Options for make_async_core.
struct async_core_options {
    level_mask levels = all_levels;
    // messages at these levels are written and flushed before the log call
    // returns, like on a sync core
    level_mask flush_on = zappy::levels(level::error);
    // default: one line to stderr, at most once per 10 s per problem
    error_handler on_error = {};
    // queue length in messages; a producer waits when the queue is full
    std::size_t capacity = 8192;
    // no worker thread: messages wait until pump() (for tests)
    bool manual = false;
};

namespace details {

inline auto to_sv(error_kind k) -> std::string_view
{
    switch (k) {
    case error_kind::sink_write:
        return "sink write failed";
    case error_kind::sink_flush:
        return "sink flush failed";
    case error_kind::format:
        return "formatter failed";
    default:
        return "messages dropped";
    }
}

// default_error_handler writes one line to stderr per problem (kind and
// sink), at most once per 10 s, with the number of occurrences since.
inline auto default_error_handler() -> error_handler
{
    struct state {
        std::mutex mtx;
        struct entry {
            std::chrono::steady_clock::time_point last;
            std::size_t pending = 0;
        };
        std::map<std::pair<int, void const*>, entry> seen;
    };
    return [st = std::make_shared<state>()](error_info const& e) {
        auto const now = std::chrono::steady_clock::now();
        auto n = std::size_t{0};
        {
            auto _ = std::lock_guard{st->mtx};
            auto [it, fresh] =
                st->seen.try_emplace({int(e.kind), e.source});
            it->second.pending += e.count;
            if (!fresh && now - it->second.last < std::chrono::seconds{10})
                return;
            it->second.last = now;
            n = std::exchange(it->second.pending, 0);
        }
        std::fprintf(stderr, "zappy: %.*s (%zu): %.*s\n",
            int(to_sv(e.kind).size()), to_sv(e.kind).data(), n,
            int(e.what.size()), e.what.data());
    };
}

struct core_state;

// A message formatted for a core, set aside or queued: its bytes for each
// format group the message needs.
struct formatted {
    std::shared_ptr<core_state> target;
    level lvl = level::info;
    std::vector<std::string> bytes; // indexed by format group; empty = unused
    std::vector<bool> ok;           // false: that group's formatter failed
};

// Per-thread state, shared by all cores.
struct thread_state {
    bool in_sink = false;     // this thread is inside a zappy sink call
    bool in_nested = false;   // ... writing a message logged from a sink
    std::vector<std::string> scratch; // formatted bytes, by format group
    std::vector<bool> ok;
    std::vector<std::string> nested_scratch; // same, for a nested message
    std::vector<bool> nested_ok;
    std::vector<formatted> pending; // logged from a sink, written after
};

inline auto this_thread() -> thread_state&
{
    thread_local thread_state ts;
    return ts;
}

// in_sink_scope marks the current thread as running sinks.
struct in_sink_scope {
    thread_state& ts;
    bool const prev;
    explicit in_sink_scope(thread_state& t)
        : ts{t}
        , prev{t.in_sink}
    {
        ts.in_sink = true;
    }
    ~in_sink_scope() { ts.in_sink = prev; }
};

inline auto exception_text(std::exception_ptr const& e) -> std::string
{
    try {
        std::rethrow_exception(e);
    }
    catch (std::exception const& x) {
        return x.what();
    }
    catch (...) {
        return "unknown exception";
    }
}

// A queued message on an async core. The formatted bytes are swapped in and
// out of the slot rather than copied, so their capacity circulates between
// slots and producer threads and a steady stream allocates nothing.
struct entry {
    std::uint64_t seq = 0;
    level lvl = level::info;
    bool flush = false;  // the producer waits until this is flushed
    bool nested = false; // logged from inside a sink
    std::vector<std::string> bytes;
    std::vector<bool> ok;
};

enum class enqueued { queued, closed, full };

struct core_state : std::enable_shared_from_this<core_state> {
    std::vector<sink_ptr> sinks;
    std::vector<std::size_t> group_of; // sink index -> format group
    std::vector<formatter const*> groups;
    level_mask accepted;               // core levels that some sink accepts
    // the format groups each level needs (masks are fixed, so this is
    // computed once)
    std::array<std::vector<std::size_t>, 5> groups_for;
    core_options opts;
    std::mutex mtx; // held across all sinks while one message is written

    // async core
    bool async = false;
    bool manual = false;
    std::mutex qm; // guards everything below
    std::condition_variable not_empty, not_full, flushed_cv;
    std::vector<entry> ring;
    std::size_t head = 0, count = 0;
    std::uint64_t next_seq = 0;    // last sequence number handed out
    std::uint64_t written_seq = 0; // written to the sinks up to here
    std::uint64_t flushed_seq = 0; // written and flushed up to here
    bool flush_requested = false;
    bool worker_waiting = false;
    int producers_waiting = 0;
    bool stopping = false; // shutdown requested
    bool closed = false;   // no more queueing: log calls write synchronously
    bool stopped = false;  // the worker has written and flushed everything
    std::thread worker;
    std::mutex pump_mtx; // serializes pump() in manual mode
    std::vector<entry> batch;
    std::chrono::steady_clock::time_point last_flush;

    ~core_state()
    {
        if (worker.joinable())
            worker.detach(); // only when the worker itself drops the state
    }

    core_state(std::span<sink_ptr const> ss, core_options o)
        : opts{std::move(o)}
    {
        for (auto const& s : ss) {
            if (!s)
                continue;
            sinks.push_back(s);
            auto const f = &s->format();
            auto g = std::size_t{0};
            while (g < groups.size() && groups[g] != f)
                ++g;
            if (g == groups.size())
                groups.push_back(f);
            group_of.push_back(g);
            accepted.bits |= s->levels().bits;
        }
        accepted.bits &= opts.levels.bits;
        for (unsigned l = 0; l < groups_for.size(); ++l)
            for (std::size_t g = 0; g < groups.size(); ++g)
                for (std::size_t i = 0; i < sinks.size(); ++i)
                    if (group_of[i] == g &&
                        sinks[i]->levels().contains(level(l))) {
                        groups_for[l].push_back(g);
                        break;
                    }
        if (!opts.on_error)
            opts.on_error = default_error_handler();
    }

    void report(error_info const& e) noexcept
    {
        try {
            opts.on_error(e);
        }
        catch (...) {
        }
    }

    // format writes r once per format group needed at its level. bytes and
    // ok are indexed by group.
    void format(record const& r, std::vector<std::string>& bytes,
        std::vector<bool>& ok)
    {
        if (bytes.size() < groups.size())
            bytes.resize(groups.size());
        ok.assign(groups.size(), false);
        for (auto g : groups_for[unsigned(r.lvl)]) {
            try {
                groups[g]->format(r, bytes[g]);
                ok[g] = true;
            }
            catch (...) {
                auto const what = exception_text(std::current_exception());
                report({error_kind::format, nullptr, 1, what});
            }
        }
    }

    // write_to_sinks writes one formatted message to every sink that takes
    // its level, each under the sink's lock, and flushes them. The caller
    // holds mtx and has marked the thread as inside a sink.
    void write_to_sinks(level l, std::vector<std::string> const& bytes,
        std::vector<bool> const& ok, bool flush)
    {
        for (std::size_t i = 0; i < sinks.size(); ++i) {
            auto& s = *sinks[i];
            auto const g = group_of[i];
            if (!s.levels().contains(l) || !ok[g])
                continue;
            auto _ = std::lock_guard{s.mtx_};
            try {
                s.write(bytes[g]);
            }
            catch (...) {
                auto const what = exception_text(std::current_exception());
                report({error_kind::sink_write, &s, 1, what});
            }
            if (flush)
                flush_one(s);
        }
    }

    // flush_one flushes s; the caller holds s's lock.
    void flush_one(sink& s)
    {
        try {
            s.flush();
        }
        catch (...) {
            auto const what = exception_text(std::current_exception());
            report({error_kind::sink_flush, &s, 1, what});
        }
    }

    void flush_sinks()
    {
        auto& ts = this_thread();
        auto _ = std::lock_guard{mtx};
        auto scope = in_sink_scope{ts};
        for (auto& s : sinks) {
            auto __ = std::lock_guard{s->mtx_};
            flush_one(*s);
        }
    }

    // deliver_sync writes formatted bytes now, on this thread.
    void deliver_sync(level l, std::vector<std::string> const& bytes,
        std::vector<bool> const& ok)
    {
        auto& ts = this_thread();
        auto _ = std::lock_guard{mtx};
        auto scope = in_sink_scope{ts};
        write_to_sinks(l, bytes, ok, true);
    }

    // submit handles one log call: format before any lock, then deliver.
    void submit(record const& r)
    {
        auto& ts = this_thread();
        if (ts.in_sink) {
            submit_nested(r, ts);
            return;
        }
        format(r, ts.scratch, ts.ok);
        if (!async ||
            enqueue(r.lvl, ts.scratch, ts.ok, false) == enqueued::closed)
            deliver_sync(r.lvl, ts.scratch, ts.ok);
        write_pending(ts);
    }

    // submit_nested handles a message logged from inside a sink. One level
    // only: a message logged while writing a nested message is dropped. On a
    // sync core (or a stopped async one) the message is set aside and
    // written once the current message is done; on an async core it is
    // queued without waiting, and dropped if the queue is full.
    void submit_nested(record const& r, thread_state& ts)
    {
        if (ts.in_nested) {
            report({error_kind::dropped, nullptr, 1,
                "logged from a sink while writing a message logged from a "
                "sink"});
            return;
        }
        if (async) {
            format(r, ts.nested_scratch, ts.nested_ok);
            switch (enqueue(r.lvl, ts.nested_scratch, ts.nested_ok, true)) {
            case enqueued::queued:
                return;
            case enqueued::full:
                report({error_kind::dropped, nullptr, 1,
                    "queue full while logging from inside a sink"});
                return;
            case enqueued::closed:
                break; // set aside below
            }
        }
        auto f = formatted{shared_from_this(), r.lvl, {}, {}};
        format(r, f.bytes, f.ok);
        ts.pending.push_back(std::move(f));
    }

    // write_pending writes the messages set aside on this thread. It runs
    // once the thread is no longer inside a sink.
    static void write_pending(thread_state& ts)
    {
        if (ts.pending.empty() || ts.in_sink)
            return;
        auto batch = std::move(ts.pending);
        ts.pending.clear();
        ts.in_nested = true;
        for (auto& f : batch)
            f.target->deliver_sync(f.lvl, f.bytes, f.ok);
        ts.in_nested = false;
    }

    // --- async core ---

    void start(std::size_t capacity, bool manual_mode)
    {
        async = true;
        manual = manual_mode;
        ring.resize(std::max<std::size_t>(capacity, 1));
        batch.resize(ring.size());
        last_flush = std::chrono::steady_clock::now();
        if (!manual)
            worker = std::thread{
                [self = shared_from_this()] { self->worker_loop(); }};
    }

    // enqueue swaps bytes/ok into the queue. A nested message (logged from
    // inside a sink) never waits: it reports full instead. Otherwise the
    // producer waits for room, and for flush_on levels until the message is
    // written and flushed. closed: the core is shut down; write it yourself.
    auto enqueue(level l, std::vector<std::string>& bytes,
        std::vector<bool>& ok, bool nested) -> enqueued
    {
        auto lk = std::unique_lock{qm};
        while (!closed && count == ring.size()) {
            if (nested)
                return enqueued::full;
            if (manual) {
                lk.unlock();
                pump();
                lk.lock();
                continue;
            }
            ++producers_waiting;
            not_full.wait(lk);
            --producers_waiting;
        }
        if (closed)
            return enqueued::closed;

        auto& e = ring[(head + count) % ring.size()];
        e.seq = ++next_seq;
        e.lvl = l;
        e.flush = !nested && opts.flush_on.contains(l);
        e.nested = nested;
        std::swap(e.bytes, bytes);
        std::swap(e.ok, ok);
        ++count;
        auto const seq = e.seq;
        auto const wait = e.flush;
        if (worker_waiting)
            not_empty.notify_one();

        if (wait) {
            if (manual) {
                lk.unlock();
                pump();
            }
            else
                flushed_cv.wait(
                    lk, [&] { return flushed_seq >= seq || stopped; });
        }
        return enqueued::queued;
    }

    // flush_all writes everything logged so far and flushes every sink.
    // From inside a sink it cannot wait for itself: it asks the worker to
    // flush once the current batch is done.
    void flush_all()
    {
        if (!async) {
            flush_sinks();
            return;
        }
        if (this_thread().in_sink) {
            auto _ = std::lock_guard{qm};
            flush_requested = true;
            return;
        }
        if (manual) {
            pump();
            return;
        }
        auto lk = std::unique_lock{qm};
        if (closed) {
            lk.unlock();
            flush_sinks();
            return;
        }
        auto const target = next_seq;
        flush_requested = true;
        if (worker_waiting)
            not_empty.notify_one();
        flushed_cv.wait(lk, [&] {
            return (flushed_seq >= target && !flush_requested) || stopped;
        });
    }

    // process takes everything queued, writes it, and flushes when the queue
    // is empty afterwards, when asked to, or at least every 100 ms. Called
    // by the worker, or by pump(), with lk held; returns with lk held.
    void process(std::unique_lock<std::mutex>& lk, bool force_flush)
    {
        auto const n = count;
        for (std::size_t i = 0; i < n; ++i)
            std::swap(batch[i], ring[(head + i) % ring.size()]);
        head = (head + n) % ring.size();
        count = 0;
        auto need_flush =
            force_flush || std::exchange(flush_requested, false);
        if (producers_waiting)
            not_full.notify_all();
        auto last = written_seq;
        lk.unlock();

        auto& ts = this_thread();
        {
            auto _ = std::lock_guard{mtx};
            auto scope = in_sink_scope{ts};
            for (std::size_t i = 0; i < n; ++i) {
                auto& e = batch[i];
                ts.in_nested = e.nested;
                write_to_sinks(e.lvl, e.bytes, e.ok, false);
                ts.in_nested = false;
                need_flush = need_flush || e.flush;
                last = e.seq;
                // do not keep a huge buffer around in every slot
                for (auto& b : e.bytes)
                    if (b.capacity() > 64 * 1024)
                        std::string{}.swap(b);
            }
        }
        write_pending(ts);

        lk.lock();
        written_seq = last;
        auto const now = std::chrono::steady_clock::now();
        need_flush = need_flush || flush_requested || count == 0 ||
                     now - last_flush >= std::chrono::milliseconds{100};
        if (!need_flush)
            return;
        flush_requested = false;
        lk.unlock();
        flush_sinks();
        lk.lock();
        flushed_seq = std::max(flushed_seq, last);
        last_flush = now;
        flushed_cv.notify_all();
    }

    void worker_loop()
    {
        auto lk = std::unique_lock{qm};
        for (;;) {
            while (count == 0 && !stopping && !flush_requested) {
                worker_waiting = true;
                not_empty.wait(lk);
                worker_waiting = false;
            }
            if (count == 0 && stopping)
                break;
            process(lk, false);
        }
        finish(lk);
    }

    // finish closes the queue (later log calls write synchronously), flushes
    // the sinks, and marks the core stopped. Called with lk held and the
    // queue empty.
    void finish(std::unique_lock<std::mutex>& lk)
    {
        closed = true;
        auto const last = written_seq;
        lk.unlock();
        flush_sinks();
        lk.lock();
        flushed_seq = std::max(flushed_seq, last);
        stopped = true;
        flushed_cv.notify_all();
        not_full.notify_all();
    }

    // pump processes everything queued (manual mode).
    void pump()
    {
        auto _ = std::lock_guard{pump_mtx};
        auto lk = std::unique_lock{qm};
        if (!stopped)
            process(lk, true);
    }

    // shutdown drains the queue, flushes, and stops the worker. Idempotent.
    // On the worker itself (the last reference dropped inside a sink) it
    // cannot join, so it detaches and the worker finishes on its own.
    void shutdown()
    {
        if (!async)
            return;
        {
            auto lk = std::unique_lock{qm};
            if (stopping)
                return;
            stopping = true;
            if (worker_waiting)
                not_empty.notify_one();
        }
        if (manual) {
            auto _ = std::lock_guard{pump_mtx};
            auto lk = std::unique_lock{qm};
            while (count != 0)
                process(lk, true);
            finish(lk);
            return;
        }
        if (worker.get_id() == std::this_thread::get_id())
            worker.detach();
        else if (worker.joinable())
            worker.join();
    }
};

} // namespace details

// core routes messages to its sinks. Create one with make_core (synchronous:
// a log call writes before it returns) or make_async_core (a worker thread
// writes; see async_core_options).
struct core {
    explicit core(std::shared_ptr<details::core_state> st)
        : st_{std::move(st)}
    {
    }
    core(core const&) = delete;
    auto operator=(core const&) -> core& = delete;

    // accepts reports whether a message at level l would reach any sink.
    auto accepts(level l) const -> bool { return st_->accepted.contains(l); }

    ~core() { st_->shutdown(); }

    // flush writes everything logged so far and flushes every sink. On an
    // async core it waits until the worker has done so.
    void flush() { st_->flush_all(); }

    // shutdown (async core): write everything queued, flush, and stop the
    // worker. Idempotent; ~core calls it. Later log calls write
    // synchronously on the caller's thread.
    void shutdown() { st_->shutdown(); }

    // pump (async core created with manual = true): write everything
    // queued, now, on this thread.
    void pump() { st_->pump(); }

    void submit(record const& r) { st_->submit(r); }

private:
    std::shared_ptr<details::core_state> st_;
};

// make_core creates a synchronous core: each log call formats on the caller
// and writes to the sinks before it returns. No thread, nothing to shut down.
inline auto make_core(std::span<sink_ptr const> sinks, core_options opts = {})
    -> std::shared_ptr<core>
{
    return std::make_shared<core>(
        std::make_shared<details::core_state>(sinks, std::move(opts)));
}

inline auto make_core(std::initializer_list<sink_ptr> sinks,
    core_options opts = {}) -> std::shared_ptr<core>
{
    return make_core(
        std::span<sink_ptr const>{sinks.begin(), sinks.size()}, std::move(opts));
}

// make_async_core creates a core with its own worker thread. A log call
// formats on the caller, queues the bytes and returns; the worker writes
// them. Messages at flush_on levels, and flush(), wait until written and
// flushed. shutdown() (or the last reference going away) drains and stops.
inline auto make_async_core(std::span<sink_ptr const> sinks,
    async_core_options opts = {}) -> std::shared_ptr<core>
{
    auto st = std::make_shared<details::core_state>(sinks,
        core_options{opts.levels, opts.flush_on, std::move(opts.on_error)});
    st->start(opts.capacity, opts.manual);
    return std::make_shared<core>(std::move(st));
}

inline auto make_async_core(std::initializer_list<sink_ptr> sinks,
    async_core_options opts = {}) -> std::shared_ptr<core>
{
    return make_async_core(
        std::span<sink_ptr const>{sinks.begin(), sinks.size()}, std::move(opts));
}

} // namespace zappy
