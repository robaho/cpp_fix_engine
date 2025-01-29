## Summary

This is a C++ implementation of a FIX protocol engine.

**With the addition of some important test cases this has moved closer to production ready but it still under active development.** It is designed for ulta-high performance scenarios,
so it won't support FIX message persistence/replay in the early releases. All sequence numbers are reset to
1 during connection.

It uses [cpp_fixed](https://github.com/robaho/cpp_fixed) to perform fixed decimal point integer math.
It uses [cpp_fix_codec](https://github.com/robaho/cpp_fix_codec) to perform low-level FIX message encoding/decoding.

## Building

Clone the `[cpp_fix_codec](https://github.com/robaho/cpp_fix_codec)` project at the same directory level. Run `make all` in the `cpp_fix_codec` project.

The project builds by default using `make` and CLang. There is a `Makefile.gcc` for building using GCC.

## Design

The engine uses a thread per client in the server.

## Errata

The fix messages declared in the project are samples for demonstration purposes. A system would need to build/parse messages according to the providers specifications.

## Performance

Using a 4 GHz Quad-Core Intel Core i7, and the Boost Fibers for server and clients.

160k+ round-trip quote messages (single double-sided qoute) + ack per second on localhost. (ping pong i.e. wait for ack, not streaming).

```
$bin/sample_client localhost -bench 75
round-trip 811924 quotes, usec per quote 6.16218, quotes per sec 162280
```

Over-the-network timings coming soon.

## Design notes

There are two main branches: `thread_per_session` and `fibers`. The latter uses [Boost Fibers](https://live.boost.org/doc/libs/1_87_0/libs/fiber/doc/html/fiber/fiber_mgmt.html), and the former a platform thread per FIX session.

The `main` branch uses fibers as the performance was much better.

The `Initiator` uses a platform thread by default, but can be configured to use fibers by passing a `Poller` to the constructor. See the `sample_client` and `-bench` support for using multiple FIX initiators sharing Boost Fibers.
