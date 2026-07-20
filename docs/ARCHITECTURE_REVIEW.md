# Song — Architecture Review

*A design-level review of the Song service framework, written after a full test-gap
and security pass over the codebase. It is meant to be read by someone deciding how
to evolve the project, not as onboarding documentation.*

Scope reviewed: ~13.8 KLOC of C++20 (`src/`, `include/song/`, `compiler/`), the
`sing/` integration suite, the Python client, and the build/CI tooling. ~1,085 C++
tests + 52 Python tests, all green.

---

## 1. What Song is

Song (**S**ervices **O**ver **N**ative **G**ateways) is a zero-dependency C++20 RPC
framework: define services in a `.song` IDL, generate type-safe C++ (and Python)
stubs, and call them across process boundaries over pipes, TCP, TLS, or an
HMAC-authenticated transport. It is deliberately built from first principles — its
own wire protocol, buffer/serialization layer, IDL compiler, and process lifecycle —
rather than wrapping gRPC/Cap'n Proto/FlatBuffers. That choice is the whole point:
the repo is a portfolio artifact demonstrating that a non-trivial systems project can
be built this way.

## 2. Component map and layering

The codebase has clean, mostly acyclic layering:

```
        songc (IDL compiler)                 generated stubs
   lexer -> parser -> resolver -> codegen  ---------------------+
        (compiler/, standalone binary)                          v
                                                     +---------------------+
   wire.hpp  (framing, headers, message builders)    |  application code   |
   buffer.hpp (SBO container, encode/decode)         |  (services/clients) |
        ^                                             +----------+----------+
        |                                                        |
   transport.hpp  Pipe / Tcp / Tls / (Secure decorator)          |
        ^                                                        |
   process.hpp   ServiceProcess (fork/exec)                      |
   runtime.hpp   ServiceRuntime (dispatch loop)  <---------------+
   manager.hpp   ServiceManager (lifecycle, discovery)
   object.hpp / subscription.hpp / stream.hpp   (object + push features)
   security.hpp / registry.hpp / discovery.hpp  (cross-cutting services)
```

The dependency direction is healthy: `buffer` and `wire` are the stable core;
transports depend on them; runtime/manager sit on top; the compiler is an entirely
separate tool that only shares `wire`/`buffer` type conventions via generated code.
`Transport` is a clean abstract seam that Pipe/TCP/TLS all implement and that
`SecureTransport` decorates — this is the codebase's best abstraction and it pays off
(streaming, subscriptions, and the multi-client loop are all transport-agnostic).

## 3. Wire protocol and data model

- Fixed **16-byte header** (magic `0x534F4E47`, type, flags, payload size, sequence
  id) + variable payload. Native endianness, no byte-swapping — an explicit
  same-architecture assumption, fine for the threat/deployment model, and a real
  portability limit to flag if that ever changes.
- `Buffer` is a move-only container with a 4 KB small-buffer optimization and typed
  encode/decode helpers. It is the workhorse and is well-tested (fuzz targets exist).
- The IDL supports structs, enums, services, and DAG-style **reference objects** with
  properties and reference counting — a genuinely ambitious feature set for a
  hand-rolled system.

The protocol is versioned (init handshake negotiates `first/current` version and a
32-bit capability bitset), which is the right foundation for evolution. Capabilities
are auto-detected from what a runtime registers (streaming, objects) and OR-ed with
manual/extension bits — a nice touch.

## 4. Concurrency model — the sharpest edge

This is where the design is most interesting and most fragile, and where the security
pass concentrated.

- Single-client pipe/TCP services run one dispatch loop; **`run_tcp_multi` spawns a
  thread per client**, all sharing one `ServiceRuntime` — hence one
  `ObjectRegistry`, one `SubscriptionRegistry`, and globally-numbered object ids.
- That sharing is the source of the two use-after-free classes found and fixed: the
  subscription fan-out sent to raw transport pointers after dropping its lock, and the
  object registry handed out raw `Object*` that another thread could free mid-dispatch.
  Both are now closed (lock-held fan-out; `shared_ptr` handle from the registry), and
  object operations are scoped to the creating connection (IDOR fix).

**Architectural observation:** the multi-client path grafts shared mutable state onto
a model that was originally single-client, and the object-id space is global and
predictable. The fixes make it *safe*, but the *cleaner* design is **per-connection
isolation** — each connection owning its own object registry and id space — which
would have made the UAF and IDOR issues structurally impossible rather than defended
against. If the object/multi-client feature is going to be a headline capability, that
refactor is the highest-value architectural investment. If it is a demo of the
concept, the current hardened-shared model is acceptable and documented.

Transports are single-writer by contract; the subscription registry is the one place
that writes cross-thread and now serializes its own fan-out. This is documented in
`transport.hpp` but is an invariant a future contributor could break — worth a debug
assertion if the multi-client path grows.

## 5. Service lifecycle

`ServiceProcess` (fork/exec, pipe redirection, init handshake, reuse) is faithful to
its Thor lineage and handles the awkward parts (SIGPIPE, partial reads, reaping)
correctly after this pass. `ServiceManager` adds registration, lazy start,
auto-restart, and discovery. The lifecycle code is solid; the main residual is the
non-atomic `pipe()`+`FD_CLOEXEC` sequence (documented) that `pipe2` would tighten.

## 6. The IDL compiler

A real lexer → parser → resolver → codegen pipeline that emits C++ and Python. It is
the most self-contained subsystem and the easiest to reason about. Codegen honestly
rejects the features it does not fully support (optional types, enum-valued fields,
multi-dimensional struct arrays) with a `CodegenError` rather than emitting broken
code — exactly the right call, and the README documents the gaps. The compiler's
input-robustness items (recursion/complexity bounds) are the main tidy-up left, and
they matter only if `songc` is ever pointed at untrusted IDL.

## 7. Error handling and failure modes

Post-pass, the runtime's decode paths are exception-safe: a malformed message
produces a decode-error reply for request types and is dropped for fire-and-forget
types, so a hostile peer cannot `std::terminate` a service. Typed exceptions
(`ProtocolError`, `ServiceError`, `SecurityError`, `CodegenError`, …) are used
consistently. The one philosophical wrinkle: some hardening favors "fail closed by
throwing," other spots "drop silently" (fire-and-forget) — both are correct for their
message class, and the code now comments the distinction, which is what matters.

## 8. Testing architecture

Genuinely strong and a differentiator: 900+ unit tests, 150+ cross-process
integration tests under `sing/`, a Python client suite, fuzz targets for the decoder,
and a reproducible CI gate (`tooling/ci.sh`) enforcing `-Wall -Wextra -Werror` +
full ctest + Python unittest. The security pass was only tractable *because* this
harness exists — nearly every fix shipped with a deterministic regression test, and
several bugs were characterization-tested first. The multi-client/`[[noreturn]]`
loops are the hardest thing to test in-process and are the thinnest-covered area
(covered indirectly via the backup integration service).

## 9. Strengths

- Clean transport abstraction; capability-negotiated, versioned protocol.
- Ambitious, coherent feature set (objects, streaming, subscriptions, discovery, TLS)
  for a from-scratch framework.
- Exceptional test and CI discipline; honest documentation of unsupported features.
- Zero third-party runtime deps (mbedTLS/OpenSSL/Avahi are optional, compile-gated).

## 10. Architectural risks and recommendations (priority order)

1. **Per-connection object isolation** (highest value). Give each connection its own
   object registry and id space; this dissolves the shared-state UAF/IDOR class the
   security pass had to defend against, and simplifies reasoning about the
   multi-client path.
2. **Formalize the transport single-writer invariant** — debug assertions or a small
   send-serialization type — so the multi-client fan-out contract cannot silently
   regress.
3. **Endianness**: the native-endian wire format is a hard portability boundary.
   Document it as a protocol invariant, or add byte-order normalization before any
   cross-architecture use.
4. **Registry/discovery are demos, not services.** If either graduates, they need the
   ownership/authentication model documented in `SECURITY.md`; today loopback-by-
   default + documentation is the right scoping.
5. **Backpressure**: `call_streaming` and the subscription fan-out buffer/serialize
   without flow control (now bounded by caps). Real streaming workloads would want
   credit-based backpressure rather than hard caps.

## 11. Bottom line

Song is a well-layered, well-tested, honestly-scoped systems project whose
abstractions (transport seam, versioned/capability-negotiated protocol, IDL pipeline)
are sound. Its one genuinely sharp architectural edge is the shared-state
multi-client model, which is now *safe* but not *simple*; per-connection isolation is
the change that would move it from "hardened" to "correct by construction." For its
stated purpose — a portfolio demonstration of end-to-end systems engineering and of
sound judgment about scope — the architecture holds up very well.
