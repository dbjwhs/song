# Security Policy

## Threat model

Song is a learning / portfolio codebase, not a production framework. Its documented
deployment model is a **trusted local host or trusted LAN** (see `SONG_DESIGN.md`
Section 21.5 and the README security section): the default `SecurityLevel::none`
is described as "trusted LAN, development". For anything crossing an untrusted
network the intended posture is:

- **TLS** for confidentiality + server authentication (certificate or PSK mode), and
- **HMAC-SHA256** (`SecureTransport`) for message authentication as defense in depth.

The hardening below assumes an attacker who can reach a Song endpoint and send
arbitrary bytes (a malformed or hostile peer), because that is the realistic attack
surface even on a LAN. It does **not** assume the compiler (`songc`) processes
hostile input: `.song` IDL files are authored by the developer, so compiler
robustness items are treated as quality/robustness rather than a remote attack
surface.

## Reporting a vulnerability

This is a personal project without a formal disclosure process. If you find an
issue, open a GitHub issue describing it (or, for something sensitive, contact the
maintainer directly before filing publicly).

## Hardening performed

A dedicated security pass reviewed the wire protocol, buffer/decoder, transports
(pipe/TCP/TLS/HMAC), the fork/exec service lifecycle, the object registry,
subscriptions/streaming, the discovery and registry services, and the IDL
compiler. Each fix below is an individual commit with a regression test (except
where a test is impractical, noted inline) and passed the full CI gate.

### Memory safety
- **Registry object dispatch use-after-free** — `ObjectRegistry::get()` returned a
  raw pointer that another thread could `delete` via `release()` mid-dispatch. Added
  `get_shared()` returning an owning `shared_ptr` held across dispatch.
- **Subscription fan-out use-after-free** — `notify()` sent to raw transport pointers
  after releasing its lock, racing a disconnecting client's `unsubscribe_all()`. The
  lock is now held across the fan-out (recursive, to preserve reentrancy).
- **datacopy out-of-bounds write** — `write_chunk` used an unvalidated attacker offset
  to index a heap buffer; negative/oversized offsets are now rejected.
- **Stale/reused PID signal** — `alive()` reaped the child but left `pid_` set, so
  `terminate()` could signal a recycled PID; `pid_` is cleared on reap.

### Authentication / authorization
- **TLS hostname verification** — certificate-mode clients now verify the server name
  against the cert CN/SAN and **fail closed** when verification is required but no
  hostname is set (previously any same-CA cert was accepted — MITM).
- **TLS `VerifyMode::optional`** — the verification result is now inspected and a bad
  cert rejected (previously `optional` silently behaved like `none`).
- **Empty HMAC key** — `SecurityConfig` rejects an empty shared secret so security can
  never report `is_enabled()` while authenticating with a zero-length key.
- **Object IDOR** — `prop_get`/`prop_set`/`release` are scoped to the connection that
  created the object; a peer can no longer read, mutate, or free another peer's object.

### Denial of service / resource bounds
- Per-connection **client-thread cap** with self-reaping workers (`run_tcp_multi`).
- Per-message **completion deadline** so a stalled partial message cannot pin a worker
  (slowloris).
- Per-connection **subscription cap**; **streaming accumulation cap**
  (`call_streaming`); **registry registration + name/host length caps**; **discovery
  result cap + de-duplication**.
- **Decode preallocation bounds** — array count, init `method_count`, and TCP payload
  no longer preallocate an attacker-declared size before the bytes arrive.

### Cryptographic hygiene
- HMAC computation checks every OpenSSL `EVP_MAC_*` return and throws on failure
  instead of shipping a tag built from uninitialized stack bytes.
- Key material is scrubbed with a non-elidable `secure_zero()` on destruction.

### Compiler robustness
- `songc` catches `LexerError` (previously it escaped a `ParserError`-only handler and
  `std::terminate`d), and passes `unsigned char` to `<cctype>` functions to avoid UB on
  high-bit input.

## Accepted design trade-offs (documented, not "fixed")

These are genuine limitations that the trusted-LAN threat model deliberately accepts.
They are recorded here so no one mistakes the current behavior for authenticated,
internet-facing security. Each notes the production hardening it would need.

- **Registry has no per-name ownership model.** Any peer that reaches the registry can
  re-point any service name, and clients trust `discover()` output. Mitigated by
  binding the demo registry to loopback by default; a production registry needs a
  per-name ownership token and a peer-authenticating transport.
- **HMAC has no replay/reflection protection.** An authenticated message can be
  captured and replayed. Production use needs a nonce/sequence or timestamp window
  bound into the MAC.
- **mDNS discovery is unauthenticated.** Discovered records (instance name, host) are
  trusted without verification, so a LAN peer can impersonate a service. Discovery is a
  convenience for trusted networks; authenticate the resulting connection (TLS/HMAC)
  and treat discovered endpoints as untrusted until then.
- **No transport authentication by default.** `SecurityLevel::none` is the default and
  is appropriate only for a trusted host/LAN, as documented.

## Known remaining hardening items (lower priority)

Not addressed in this pass, with rationale:

- **`songc` recursion/complexity bounds** (deep inheritance cycle detection, per-line
  comment recursion, cubic duplicate-field scan, enum auto-value overflow) — real
  robustness issues but gated on hostile IDL input, which is outside the threat model
  (you compile your own `.song` files). Worth bounding if `songc` is ever exposed to
  untrusted input.
- **`pipe()`+`FD_CLOEXEC` race** on service spawn — a concurrent `fork()` can leak one
  fd; use `pipe2(O_CLOEXEC)` where available.
- **Moved-from `SecurityConfig` key residue** — `std::string` small-buffer storage can
  retain key bytes after a move; a fixed secure buffer type would eliminate it.
