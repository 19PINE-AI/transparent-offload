# Design A — fused transparent conflict detector (page-protection)

Goal: make the runtime transparent AND correct on UNLOCKED cross-connection shared state,
with NO app changes (removing the "relies on app locks" threat).

## Mechanism (write-fault + version-snapshot)
- Write-protect the app's data/BSS segments (`/proc/self/maps`), `PROT_READ`.
- At the offload park, snapshot a global version clock into the fiber; re-protect before each resume.
- On a post-offload write fault to a page whose version changed during the fiber's park →
  the fiber's pre-offload read is stale → **conflict** (a transparent read-modify-write race).
- Enforcement: a fiber-aware **handler lock** held from socket-read to socket-write (across the
  offload), serializing the read→offload→write critical section of conflicting handlers.

## Results
**Detection (transparent, no app cooperation):**
| app | detected conflicts | meaning |
|---|---:|---|
| `conn_server` (no shared writes) | **0** | safe → full overlap preserved |
| `hostile_server` (unlocked RMW on a global) | **125,039** | hazard flagged automatically |

**Enforcement:**
| mode | `hostile_server` unlocked | note |
|---|---|---|
| two-phase (`RT_ENFORCE`) | **0 lost — CORRECT** | sound; serializes the hot shared state |
| adaptive single-run (auto-enable on 1st conflict) | 28 / 28,866 lost | small transient; sound version needs rollback |

**Safe-app overlap preserved:** conn_server under `RT_DETECT` → 0 conflicts → no serialization,
stays correct and overlapped.

## Honest costs / limits
- **Overhead:** always-on page protection costs ~40% on safe apps (one `mprotect` per fiber
  resume). A real build would reprotect only dirtied pages / skip when no writes occurred.
- **Page granularity:** `shared` and `total` share a page → the detector over-counts (flags
  the benign `total++` too). Byte-granular would need the compiler-pass variant (Design B).
- **Adaptive transient:** the single-run adaptive mode loses a few updates before enforcement
  engages; the **two-phase mode is fully sound** (detector informs the decision to enable
  enforcement). Eliminating the transient in one pass requires checkpoint/rollback re-execution.
- **Scope:** protects the exe's globals/BSS (where the demo hazard lives); heap-shared state is
  the same mechanism extended to `[heap]` (not yet wired).

## Bottom line
The runtime is now **transparent AND correct for unlocked cross-connection shared state** —
it automatically detects the hazard the taxonomy predicts and enforces correctness with no app
locks, soundly in two-phase mode. This removes the "relies on app locks" gap; the remaining
work is overhead reduction and single-pass soundness (rollback).
