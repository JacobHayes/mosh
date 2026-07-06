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
#include <string>

namespace Terminal {
class Row;

static const size_t HISTORY_DEFAULT_LINES = 10000;

/* Ring buffer of logical lines that have scrolled off the top of the
   screen.  One ring is shared (via shared_ptr) among all the
   Framebuffer copies kept by the state-synchronization transport;
   per-state progress lives in plain counters inside each Framebuffer.

   On the server the ring captures rows as they scroll off, stitching
   soft-wrapped rows into logical lines so the client's host terminal
   can reflow them natively.  On the client the ring only receives
   lines the server sent (capture must stay off: the client emulator
   replays screen *diffs*, whose scroll pattern differs from the
   original output). */
class HistoryRing
{
public:
  struct Line
  {
    uint64_t seq;
    std::string rendered; /* self-contained ANSI string, no trailing newline */
  };

  typedef std::deque<Line>::const_iterator const_iterator;

private:
  std::deque<Line> lines;
  size_t capacity;
  bool capture; /* server captures; client only receives */
  uint64_t next_seq;
  uint64_t next_row;    /* display rows ever absorbed (drives client fast path) */
  uint64_t clear_count; /* bumped when the application clears scrollback */
  std::string pending;  /* partial logical line awaiting its wrap continuation */
  bool discontinuity;   /* receiver saw a gap in seq numbers */

  /* keep a pathological never-ending logical line from growing without bound */
  static const size_t PENDING_CAP = 1048576;

  void finalize( void );

public:
  HistoryRing( size_t s_capacity, bool s_capture )
    : lines(), capacity( s_capacity ), capture( s_capture ), next_seq( 0 ), next_row( 0 ), clear_count( 0 ),
      pending(), discontinuity( false )
  {}

  bool capture_enabled( void ) const { return capture && capacity > 0; }

  /* sender side */
  void append_row( const Row& row, int width );
  void flush_pending( void ); /* resize or terminal reset interrupts stitching */
  void clear( void );         /* CSI 3 J */

  /* receiver side */
  void receive_line( uint64_t seq, const std::string& rendered );
  void receive_clear( uint64_t new_clear_count );

  uint64_t get_next_seq( void ) const { return next_seq; }
  uint64_t get_next_row( void ) const { return next_row; }
  uint64_t get_clear_count( void ) const { return clear_count; }
  uint64_t oldest_seq( void ) const { return lines.empty() ? next_seq : lines.front().seq; }

  bool has_discontinuity( void ) const { return discontinuity; }
  void set_discontinuity( void ) { discontinuity = true; }
  void clear_discontinuity( void ) { discontinuity = false; }

  const_iterator begin( void ) const { return lines.begin(); }
  const_iterator end( void ) const { return lines.end(); }
  size_t size( void ) const { return lines.size(); }
  const_iterator lower_bound( uint64_t seq ) const;
};

/* Render a row as a self-contained ANSI string (SGR renditions and
   OSC 8 hyperlinks re-established from a default state, reset at the
   end).  A final (non-wrapped) row has trailing default blank cells
   trimmed; a wrapped row renders every cell so the continuation can be
   appended directly. */
std::string render_row( const Row& row, int width, bool final_row );
}

#endif
