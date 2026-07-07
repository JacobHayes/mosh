/*
    Mosh: the mobile shell
    Copyright 2012 Keith Winstein

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    In addition, as a special exception, the copyright holders give
    permission to link the code of portions of this program with the
    OpenSSL library under certain conditions as described in each
    individual source file, and distribute linked combinations including
    the two.

    You must obey the GNU General Public License in all respects for all
    of the code used other than OpenSSL. If you modify file(s) with this
    exception, you may extend this exception to your version of the
    file(s), but you are not obligated to do so. If you do not wish to do
    so, delete this exception statement from your version. If you delete
    this exception statement from all source files in the program, then
    also delete it here.
*/

#ifndef TERMINALHISTORY_HPP
#define TERMINALHISTORY_HPP

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace Terminal {
class Row;

static const size_t HISTORY_DEFAULT_LINES = 10000;

/* Ring buffer of display rows that have scrolled off the top of the
   screen.  One ring is shared (via shared_ptr) among all the
   Framebuffer copies kept by the state-synchronization transport;
   per-state progress lives in plain counters inside each Framebuffer.

   Each entry is one display row, pre-rendered as a self-contained
   ANSI string, plus its wrap flag: a wrapped row is a prefix of the
   row that follows it, so the receiver can reconstruct logical lines
   by concatenation (which is what lets the host terminal re-wrap
   scrollback natively).  On the capture side the raw row is retained
   for recent entries so a resize can pull rows back onto the screen.

   The receive side only accepts rows the server sent (capture must
   stay off: the client emulator replays screen *diffs*, whose scroll
   pattern differs from the original output). */
class HistoryRing
{
public:
  struct Entry
  {
    uint64_t seq;
    std::string rendered; /* self-contained ANSI string, no trailing newline */
    bool wrapped;         /* continues on the following row */
    std::shared_ptr<Row> raw; /* capture side, recent entries only; enables pull-back */
  };

  typedef std::deque<Entry>::const_iterator const_iterator;

private:
  std::deque<Entry> entries;
  size_t capacity;
  bool capture;            /* server captures; client only receives */
  uint64_t next_seq;       /* seq of the next captured row; never decreases */
  uint64_t clear_count;    /* bumped when the application clears scrollback */
  uint64_t truncate_count; /* bumped when tail rows are pulled back by a resize */
  bool discontinuity;      /* receiver's copy no longer matches; needs a rebuild */

  /* raw rows are only needed for resize pull-back; don't hold whole
     screens of cells alive for the entire ring */
  static const size_t RAW_KEEP = 1024;

public:
  HistoryRing( size_t s_capacity, bool s_capture )
    : entries(), capacity( s_capacity ), capture( s_capture ), next_seq( 0 ), clear_count( 0 ),
      truncate_count( 0 ), discontinuity( false )
  {}

  bool capture_enabled( void ) const { return capture && capacity > 0; }

  /* capture side */
  void append_row( const std::shared_ptr<Row>& row, int width );
  void clear( void ); /* CSI 3 J */
  /* Remove the trailing logical line so a resize can put it back on
     the screen.  Returns its raw rows oldest-first; empty if nothing
     can be pulled.  Bumps truncate_count. */
  std::vector<std::shared_ptr<Row>> pull_last_line( int max_rows );

  /* receive side */
  void receive_row( uint64_t seq, const std::string& rendered, bool wrapped );
  void receive_clear( uint64_t new_clear_count );
  /* wholesale replacement follows (after a sender-side truncate) */
  void begin_snapshot( uint64_t reset_seq, uint64_t s_truncate_count );

  uint64_t get_next_seq( void ) const { return next_seq; }
  uint64_t get_clear_count( void ) const { return clear_count; }
  uint64_t get_truncate_count( void ) const { return truncate_count; }
  uint64_t oldest_seq( void ) const { return entries.empty() ? next_seq : entries.front().seq; }

  bool has_discontinuity( void ) const { return discontinuity; }
  void set_discontinuity( void ) { discontinuity = true; }
  void clear_discontinuity( void ) { discontinuity = false; }

  const_iterator begin( void ) const { return entries.begin(); }
  const_iterator end( void ) const { return entries.end(); }
  size_t size( void ) const { return entries.size(); }
  const_iterator lower_bound( uint64_t seq ) const;
};

/* Number of leading cells that carry content: everything up to the
   last cell that isn't a default-rendition blank.  (A wrapped row is
   full-width by definition; callers pass its full extent instead.) */
int render_extent( const Row& row, int width );

/* Render a row as a self-contained ANSI string (SGR renditions and
   OSC 8 hyperlinks re-established from a default state, reset at the
   end).  A final (non-wrapped) row has trailing default blank cells
   trimmed; a wrapped row renders every cell so the continuation can be
   appended directly. */
std::string render_row( const Row& row, int width, bool final_row );
}

#endif
