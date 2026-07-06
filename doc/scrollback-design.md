# Mosh scrollback design

Status: in development on branch `scrollback`.

## Goal

Native-feeling scrollback for mosh sessions: output that scrolls off the top of
the screen lands in the **host terminal's own scrollback buffer** (Ghostty,
iTerm2, xterm, ...), so scrolling, searching, and selection use the terminal's
native UI. History is **authoritative on the server** and survives
disconnects: on reconnect (or any discontinuity) the client rebuilds the host
scrollback from the server's transcript, Pi-style (clear + replay).

Agreed constraints:

- History must be complete across disconnects/roaming (server-side capture).
- Graceful interop with stock mosh: a patched client against a stock server
  (or vice versa) behaves exactly like stock mosh today.
- Large replays on reconnect/resize are acceptable.
- `ESC[3J` wiping pre-mosh host scrollback on first replay is acceptable.
- Alternate-screen apps (vim, less) must not pollute scrollback → requires
  implementing DECSET 1047/1048/1049 in mosh's emulator (phase 3).

## Background: why this shape

Mosh's State Synchronization Protocol syncs *states*, not streams. The only
screen state is a width×height `Terminal::Framebuffer`; rows scrolled off the
top are destroyed (`Framebuffer::scroll` → `delete_line`). Diffs between
states are rendered ANSI bytes (`Complete::diff_from` → `Display::new_frame`)
carried in protobuf `Instruction`s. After a disconnect the sender diffs
straight to the latest state — intermediate output never reaches the client.
Hence:

- Scrollback content must be captured **server-side** at the moment rows
  scroll off, or it is lost forever.
- The client cannot rely on organic screen scrolls to feed host scrollback;
  it needs an explicit history channel plus a rebuild ("replay") mechanism.

Both protobuf schemas use `extensions 2 to max` and both `apply_string`
implementations silently skip instructions with unknown extensions, so new
extension messages are wire-compatible with stock mosh in both directions.
`MOSH_PROTOCOL_VERSION` stays at 2.

## Components

### 1. Server-side capture: `Terminal::HistoryRing`

New class (src/terminal/terminalhistory.{h,cc}), owned via
`shared_ptr` by `Framebuffer` so that the many state copies kept by the
transport sender/receiver all share one ring; per-state progress is recorded
in plain counters copied by value.

- **Capture point**: `Framebuffer::scroll(N>0)` when the scrolling region is
  the full screen (top row 0, bottom row height−1). Rows scrolled inside an
  app-defined margin region do not enter scrollback (matches xterm). Direct
  `delete_line` (CSI M) never captures.
- **Logical lines, not display rows**: terminal reflow (Ghostty et al.) only
  rewraps lines the terminal itself soft-wrapped. Mosh `Row`s carry a `wrap`
  flag; scrolled-off rows are stitched: a row with `wrap` set is a prefix of
  the next scrolled row. The ring accumulates a pending partial line and
  finalizes it when a non-wrapped row completes it (with a size cap to
  force-break pathological lines). On replay the client prints logical lines
  and lets the host terminal wrap them → native reflow on resize.
- **Rendering**: rows are rendered to self-contained ANSI strings at capture
  time (SGR via `Renditions::sgr()`, OSC 8 hyperlinks, wide/combining cells
  via `Cell::print_grapheme`, trailing default-blank cells trimmed on final
  rows, `SGR 0` + hyperlink close at end).
- **Counters** (members of `Framebuffer`, value-copied per state, all synced):
  - `history_line_count` — total logical lines ever finalized (== next seq).
  - `history_row_count` — total *display rows* ever scrolled off (drives the
    client fast path).
  - `history_clear_count` — bumped when the app clears scrollback (CSI 3 J);
    forces a client replay, which (with a cleared ring) clears host scrollback.
- Ring capacity: default 10 000 lines, configurable
  (`MOSH_SCROLLBACK_LINES`, `0` disables capture and the feature entirely).
  Capture is always on when enabled; the *subscription* only gates sending.

### 2. Protocol

`src/protobufs/hostinput.proto` (host → client), new extension field 8:

```proto
message HistoryLines {
  optional uint64 line_count = 1;    // total finalized logical lines at this state
  optional uint64 row_count = 2;     // total display rows scrolled off at this state
  optional uint64 clear_count = 3;   // scrollback-clear generation
  optional uint64 first_seq = 4;     // seq of lines[0]
  repeated bytes  lines = 5;         // rendered logical lines, oldest first
}
```

`diff_from(existing)` attaches a `HistoryLines` instruction whenever the
counters differ, carrying ring lines in `(existing.line_count,
line_count]` — clamped to what the ring still retains. If the receiver's
`first_seq` is ahead of its own ring end, that is a **gap** (disconnect longer
than ring retention): the client marks scrollback dirty and replays whatever
the ring holds. Reconnect backfill needs no special case: a fresh/old
`existing` state simply has a small `line_count`, so the diff carries the
backlog (transport fragmentation already handles large diffs).

Client applies `HistoryLines` idempotently (append only lines with
`seq >= ring.next_seq`) because the sender may transmit overlapping diffs.

`src/protobufs/userinput.proto` (client → server), new extension field 4:

```proto
message FeatureRequest {
  optional uint32 features = 1;      // bit 0: scrollback; bit 1: alt-screen (phase 3)
}
```

New `UserEvent` variant in the user stream. The client pushes it once at
startup, before any keystroke. `mosh-server` handles it by enabling history
sending (and, phase 3, alt-screen emulation) on its `Complete` object. A stock
server ignores the instruction; a stock client never sends it; either way both
ends behave exactly like today. This gating is *mandatory* for alt-screen
(phase 3): sync correctness requires both emulators to interpret diff bytes
identically, so the server must not change emulation semantics for stock
clients.

### 3. Client rendering (`STMClient`)

The client starts in the alternate screen exactly as today. On the **first**
`HistoryLines` it receives, it switches modes once:

1. leave the alternate screen (`rmcup`),
2. mark scrollback dirty → full replay (below),
3. from then on, run directly on the host primary screen.

Invariant to maintain: *host scrollback = transcript of scrolled-off content;
host viewport = current framebuffer.* Two mechanisms:

**Fast path (steady state).** When the newest synced state's `row_count`
exceeds what the client has emitted by `r`, and `r < height`, and no overlay
(notification/prediction) is painted over the top `r` rows: reset margins,
park the cursor at bottom-left, emit `r` linefeeds. The host pushes the top
`r` rows of the previous frame — which are precisely the rows that scrolled
off server-side — into its scrollback. Then hand the differ a synthetic
baseline (previous framebuffer scrolled by `r`, cursor at bottom-left) so it
repaints only what actually changed. Fast-path rows enter scrollback
hard-wrapped; any later replay rebuilds them reflowable.

**Clear-and-replay (discontinuities).** A dirty flag is set by: resize,
reconnect gap (`first_seq` jump), `clear_count` change, ring overflow, fast
path skipped (big jump `r ≥ height`, or overlays in the way), suspend/resume,
or mode switch. Replay is debounced (~300 ms after scroll activity settles,
so drag-resizes and `cat bigfile` bursts coalesce into one rebuild):

```
hide cursor; reset margins; SGR 0
ESC[2J ESC[3J ESC[H          # wipe viewport + host scrollback
for each logical line in ring: print line, CR LF   # host wraps → reflow metadata
emit (height−1) LFs           # push the tail out; exact regardless of wrap math
full frame repaint (differ with initialized=false)
```

The `(height−1)` linefeeds trick makes the push-out count exact without any
width/wcwidth computation: whatever remains in the viewport after printing is
by construction at most `height−1` rows of history, and blank filler rows are
never pushed out (for short history the leading LFs are absorbed moving the
cursor to the bottom margin first).

**Differ must not scroll.** When scrollback mode is active, the *local*
differ's scroll optimization is disabled (repaint-in-place instead), because
terminals differ on whether margin scrolls feed scrollback — all scrollback
insertion must go through the two controlled paths above. The *server-side*
differ (whose output feeds the client's emulator, not a real terminal) keeps
using scroll ops for bandwidth.

### 4. Resize

- Host scrollback: replay (debounced) reprints logical lines at the new
  width — native rewrap.
- Viewport: unchanged from mosh today (`Framebuffer::resize` crops/pads; the
  app redraws on SIGWINCH).
- Boundary: the host terminal shuffles rows between viewport and scrollback
  on its own during resize; the subsequent replay wipes and rebuilds, so the
  seam cannot accumulate garbage.

### 5. Phase 3: alternate screen (DECSET 1047/1048/1049)

- `Framebuffer` gains a second grid; 1049 = save cursor + switch + clear alt,
  1047 = switch, 1048 = save/restore cursor; RIS/soft-reset exits alt screen.
- Enabled **only after** the client's `FeatureRequest` announces support
  (stock clients keep stock semantics; see §2).
- While alt is active: no history capture, `row_count` frozen; client passes
  `1049h/l` through so the host terminal uses *its* native alt screen — vim
  never touches host scrollback, and exiting vim restores the primary screen.
- Differ: emits mode transitions between states; the endpoint case "both in
  alt screen but the hidden primary changed in between" sets the dirty flag,
  and the replay is deferred until the alt screen exits.

## Known limitations (accepted)

- Fast-path scrollback rows are hard-wrapped until the next replay; selection
  of long lines copies with embedded newlines until then.
- If an app rewrites the top rows and scrolls within one sync frame, the
  fast path can push a stale row; heals on the next replay.
- A disconnect longer than ring retention loses the overflowed middle; the
  replay simply shows the retained window.
- Full-screen apps that do not use the alternate screen leave output in
  scrollback (same as any native terminal).
- First replay wipes pre-mosh host scrollback (`ESC[3J`).

## Testing

- Existing emulation/e2e suites must pass unchanged (stock behavior when
  feature not negotiated).
- New e2e coverage: capture correctness (stitching, SGR, wide chars), sync
  across simulated reconnect, replay byte-stream shape.
- Manual: Ghostty — interactive scroll, `cat` bursts, reconnect backfill,
  drag-resize reflow, vim (phase 3), `clear`/CSI 3 J propagation.
