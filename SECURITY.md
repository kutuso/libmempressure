# Security Policy

## Supported versions

The `master` branch is supported. Released versions are the tagged
`vX.Y.Z` commits.

## Reporting a vulnerability

Please report vulnerabilities privately:
[GitHub security advisories](https://github.com/kutuso/libmempressure/security/advisories/new).

This is a system library loaded into applications; memory-safety and
concurrency bugs are treated as security issues. CI runs the native test
suites under AddressSanitizer + UndefinedBehaviorSanitizer on every push —
reports that reproduce outside that coverage are especially welcome.

## Threat notes

- The library reads a kernel file (`/proc/pressure/memory` by default, or
  whatever `psi_path` is configured to). If an application points it at
  untrusted input, only double values are parsed; no execution paths derive
  from file contents.
- Callbacks fire from the monitor thread: application callbacks must be
  thread-safe and must not call `mp_shutdown` from within a callback.
