# Distributed algorithms

Implementation of a handful distributed algorithms on top of posix libc UDP. Algorithms give deterministic guarantees of their properties (see below). While the main purpose is to maintain theoretical properties, in practice there are things that can affect the runtime. Most notably, congestion. If too many packets are aggressively sent it might result in failing syscalls. These algorithms do not recover from failed syscalls if these might affect correctness. In fact, it will result in an `abort` that cannot be caught. Failed syscalls that can be safely ignored without affecting correctness are in fact ignored. Therefore congestion control should be handled on the application level as these algorithms do some, but very little, congestion control. These algorithms are mostly network-bound not cpu-bound.

All algorithms assume to be running in an asynchronous (or stronger) model (packets can be randomly dropped, reordered, delayed) and crash-stop model (an incorrect process is not malicious but simply stops delivering and sending any messages, it is completely silent). Additionally there is an assumption that at least the majority of processes are correct.

## Algorithms

See `src/include` and `src/src` for implementation. Uses C++17 with no compiler extensions.

### Perfect links

A point-to-point communication guaranteeing delivery. Properties:

1. **Validity** - if p1 and p2 are correct, every message sent by p1 is eventually delivered by p2
2. **No duplication** - no message is delivered more than once
3. **No creation** - no message is delivered unless sent

### Best effort broadcast

A broadcasting communication guaranteeing delivery. Properties:

1. **Validity** - if p1 and p2 are correct, then every message broadcast by p1 is eventually delivered to p2
2. **No duplication** - no message is delivered more than once
3. **No creation** - no message is delivered unless it was broadcast

### Uniform reliable broadcast

A broadcasting communication guaranteeing delivery and agreement among processes. Properties:

1. **Validity** - if p1 and p2 are correct, then every message broadcast by p1 is eventually delivered to p2
2. **No duplication** - no message is delivered more than once
3. **No creation** - no message is delivered unless it was broadcast
4. **Uniform agreement** - if any process delivers m, then all correct processes eventually deliver m

### FIFO broadcast

A broadcasting communication based on uniform reliable broadcast but with the guarantee of ordered delivery of messages with respect to the order in which they were sent.

### Lattice agreement

A weak consensus. Properties:

Let Ii and Oi be the proposed and decided set respectively of a process Pi.

1. **Validity** - let a process Pi decide the set Oi. Then:
   - Ii is a subset of Oi
   - Oi is a subset of the union of all Ij
2. **Consistency** - Oi is a subset of Oj or Oj is a subset of Oi
3. **Termination** - Every correct process eventually decides
