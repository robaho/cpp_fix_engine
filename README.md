## Summary

This is a C++ implementation of a FIX protocol engine.

**It is a WIP at the moment, but it is functional.** It is designed for ulta-high performance scenarios,
so it won't support FIX message persistence/replay in the early iterators. All sequence numbers are reset to
1 during connection.

It uses [cpp_fixed](https://github.com/robaho/cpp_fixed) to perform fixed decimal point integer math.
It uses [cpp_fix_codec](https://github.com/robaho/cpp_fix_codec) to perform low-level FIX message encoding/decoding.

## Building

Run `make all` which will also clone the [cpp_fix_codec](https://github.com/robaho/cpp_fix_codec) into a subdirectory.

The project builds by default using `make` and CLang. There is a `Makefile.gcc` for building using GCC.

## Design

The engine uses a thread per client in the server.

## Performance

Using a 4 GHz Quad-Core Intel Core i7:

24k round-trip quote message + ack per second on localhost.

Over-the-network timings coming soon.
