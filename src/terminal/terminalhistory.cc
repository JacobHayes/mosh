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

#include <algorithm>

#include "src/terminal/terminalframebuffer.h"
#include "src/terminal/terminalhistory.h"

using namespace Terminal;

int Terminal::render_extent( const Row& row, int width )
{
  const Renditions default_renditions( 0 );
  int end = std::min( (int)row.cells.size(), width );
  while ( end > 0 ) {
    const Cell& cell = row.cells.at( end - 1 );
    if ( cell.empty() && cell.get_renditions() == default_renditions && !cell.get_hyperlink() ) {
      end--;
    } else {
      break;
    }
  }
  return end;
}

std::string Terminal::render_row( const Row& row, int width, bool final_row )
{
  const Renditions default_renditions( 0 );
  const Hyperlink no_hyperlink;

  const int end = final_row ? render_extent( row, width ) : std::min( (int)row.cells.size(), width );

  std::string out;
  Renditions current = default_renditions;
  Hyperlink current_link;

  for ( int x = 0; x < end; ) {
    const Cell& cell = row.cells.at( x );
    if ( !( cell.get_renditions() == current ) ) {
      current = cell.get_renditions();
      out.append( current.sgr() );
    }
    if ( cell.get_hyperlink() != current_link ) {
      current_link = cell.get_hyperlink();
      out.append( current_link.osc8() );
    }
    cell.print_grapheme( out );
    x += cell.get_width();
  }

  if ( !( current == default_renditions ) ) {
    out.append( "\033[0m" );
  }
  if ( current_link ) {
    out.append( no_hyperlink.osc8() );
  }

  return out;
}

void HistoryRing::append_row( const std::shared_ptr<Row>& row, int width )
{
  const bool wrapped = row->get_wrap();
  entries.push_back( Entry { next_seq++, render_row( *row, width, !wrapped ), wrapped, row } );
  if ( entries.size() > RAW_KEEP ) {
    entries[entries.size() - 1 - RAW_KEEP].raw.reset();
  }
  while ( entries.size() > capacity ) {
    entries.pop_front();
  }
}

void HistoryRing::clear( void )
{
  entries.clear();
  clear_count++;
}

std::vector<std::shared_ptr<Row>> HistoryRing::pull_last_line( int max_rows )
{
  std::vector<std::shared_ptr<Row>> out;
  if ( entries.empty() ) {
    return out;
  }
  /* back up to the first row of the trailing logical line */
  size_t start = entries.size() - 1;
  while ( start > 0 && entries[start - 1].wrapped ) {
    start--;
  }
  if ( entries.size() - start > (size_t)max_rows ) {
    return out;
  }
  for ( size_t i = start; i < entries.size(); i++ ) {
    if ( !entries[i].raw ) {
      /* rendered-only (too old to still hold cells); can't pull */
      return std::vector<std::shared_ptr<Row>>();
    }
    out.push_back( entries[i].raw );
  }
  entries.erase( entries.begin() + start, entries.end() );
  truncate_count++;
  return out;
}

void HistoryRing::receive_row( uint64_t seq, const std::string& rendered, bool wrapped )
{
  if ( seq < next_seq ) {
    /* duplicate from an overlapping diff */
    return;
  }
  if ( seq > next_seq ) {
    /* disconnect outlasted the sender's ring; host scrollback needs a rebuild */
    discontinuity = true;
  }
  entries.push_back( Entry { seq, rendered, wrapped, std::shared_ptr<Row>() } );
  next_seq = seq + 1;
  if ( capacity ) {
    while ( entries.size() > capacity ) {
      entries.pop_front();
    }
  }
}

void HistoryRing::receive_clear( uint64_t new_clear_count )
{
  if ( new_clear_count <= clear_count ) {
    return;
  }
  clear_count = new_clear_count;
  entries.clear();
  discontinuity = true; /* force a replay so the host scrollback is wiped too */
}

void HistoryRing::begin_snapshot( uint64_t reset_seq, uint64_t s_truncate_count )
{
  entries.clear();
  next_seq = reset_seq;
  truncate_count = s_truncate_count;
  discontinuity = true;
}

HistoryRing::const_iterator HistoryRing::lower_bound( uint64_t seq ) const
{
  return std::lower_bound(
    entries.begin(), entries.end(), seq, []( const Entry& e, uint64_t v ) { return e.seq < v; } );
}
