/*
    Mosh: the mobile shell
    Copyright 2026 Jacob Hayes

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#include <cassert>
#include <string>

#include "src/statesync/completeterminal.h"
#include "src/terminal/terminaldisplay.h"

static std::string frame_from( const Terminal::Framebuffer& before, const Terminal::Framebuffer& after )
{
  Terminal::Display display( false );
  return display.new_frame( true, before, after );
}

static void assert_contains( const std::string& haystack, const std::string& needle )
{
  assert( haystack.find( needle ) != std::string::npos );
}

static void assert_not_contains( const std::string& haystack, const std::string& needle )
{
  assert( haystack.find( needle ) == std::string::npos );
}

static std::string act_user_bytes( Terminal::Complete& terminal, const std::string& bytes )
{
  std::string ret;
  for ( unsigned char ch : bytes ) {
    ret.append( terminal.act( Parser::UserByte( ch ) ) );
  }
  return ret;
}

int main( void )
{
  Terminal::Complete terminal( 80, 24 );

  assert( terminal.act( "\033[?u" ) == "\033[?0u" );
  assert( terminal.act( "\033[=3u" ).empty() );
  assert( terminal.act( "\033[?u" ) == "\033[?3u" );
  assert( terminal.act( "\033[>5u" ).empty() );
  assert( terminal.act( "\033[?u" ) == "\033[?5u" );
  assert( terminal.act( "\033[<u" ).empty() );
  assert( terminal.act( "\033[?u" ) == "\033[?3u" );
  assert( terminal.act( "\033[<u" ).empty() );
  assert( terminal.act( "\033[?u" ) == "\033[?0u" );

  assert( terminal.act( "\033[?4m" ) == "\033[>4;0m" );
  assert( terminal.act( "\033[>4;2m" ).empty() );
  assert( terminal.act( "\033[?4m" ) == "\033[>4;2m" );
  assert( terminal.act( "\033P$q>4m\033\\" ) == "\033P1$r>4;2m\033\\" );

  assert( act_user_bytes( terminal, "\033[65;5u" ) == "\033[65;5u" );
  assert( act_user_bytes( terminal, "\033OA" ) == "\033[A" );

  Terminal::Framebuffer blank( 80, 24 );
  std::string frame = frame_from( blank, terminal.get_fb() );
  assert_contains( frame, "\033[>4;2m" );


  Terminal::Framebuffer before = terminal.get_fb();
  assert( terminal.act( "\033[=17u" ).empty() );
  frame = frame_from( before, terminal.get_fb() );
  assert_contains( frame, "\033[=17;1u" );

  before = terminal.get_fb();
  assert( terminal.act( "\033]7;file://remote.example/tmp/project\033\\" ).empty() );
  frame = frame_from( before, terminal.get_fb() );
  assert_contains( frame, "\033]7;file://remote.example/tmp/project\007" );

  before = terminal.get_fb();
  assert( terminal.act( "\033]633;A\033\\" ).empty() );
  frame = frame_from( before, terminal.get_fb() );
  assert_contains( frame, "\033]633;A\007" );

  before = terminal.get_fb();
  assert( terminal.act( "\033]633;E;rm -rf /\033\\" ).empty() );
  frame = frame_from( before, terminal.get_fb() );
  assert_not_contains( frame, "\033]633;E;rm -rf /\007" );

  before = terminal.get_fb();
  assert( terminal.act( "\033]7;https://example.com/tmp\033\\" ).empty() );
  frame = frame_from( before, terminal.get_fb() );
  assert_not_contains( frame, "\033]7;https://example.com/tmp\007" );

  before = terminal.get_fb();
  assert( terminal.act( "\033_Ga=q,i=1;AAAA\033\\" ).empty() );
  frame = frame_from( before, terminal.get_fb() );
  assert_contains( frame, "\033_Ga=q,i=1;AAAA\033\\" );

  before = terminal.get_fb();
  assert( terminal.act( "\033_Ga=T,t=f;/tmp/local-file.png\033\\" ).empty() );
  frame = frame_from( before, terminal.get_fb() );
  assert_not_contains( frame, "\033_Ga=T,t=f;/tmp/local-file.png\033\\" );

  {
    Terminal::Complete graphics_terminal( 80, 24 );
    const std::string large_payload( 600 * 1024, 'A' );
    assert( graphics_terminal.act( std::string( "\033_Ga=T,f=32,s=1,v=1;" ) + large_payload + "\033\\" ).empty() );
    assert( graphics_terminal.get_fb().get_passthrough_sequences().size() == 1 );
    frame = frame_from( Terminal::Framebuffer( 80, 24 ), graphics_terminal.get_fb() );
    assert_contains( frame, large_payload.substr( 0, 1024 ) );
  }

  {
    Terminal::Complete graphics_terminal( 80, 24 );
    const std::string first_chunk( 512, 'A' );
    const std::string middle_chunk( 512, 'B' );
    const std::string final_chunk( 512, 'Z' );

    assert( graphics_terminal.act( "\033_Ga=T,f=32,s=1,v=1,m=1\033\\" ).empty() );
    assert( graphics_terminal.act( std::string( "\033_Gm=1;" ) + first_chunk + "\033\\" ).empty() );
    for ( int i = 0; i < 63; i++ ) {
      assert( graphics_terminal.act( std::string( "\033_Gm=1;" ) + middle_chunk + "\033\\" ).empty() );
    }
    assert( graphics_terminal.act( std::string( "\033_Gm=0;" ) + final_chunk + "\033\\" ).empty() );

    assert( graphics_terminal.get_fb().get_passthrough_sequences().size() > 32 );
    frame = frame_from( Terminal::Framebuffer( 80, 24 ), graphics_terminal.get_fb() );
    /* The action-carrying chunk is stored/forwarded with C=1 forced on. */
    assert_contains( frame, "\033_Ga=T,f=32,s=1,v=1,m=1,C=1\033\\" );
    assert_contains( frame, std::string( "\033_Gm=1;" ) + first_chunk.substr( 0, 64 ) );
    assert_contains( frame, std::string( "\033_Gm=0;" ) + final_chunk.substr( 0, 64 ) );
  }

  {
    Terminal::Complete graphics_terminal( 80, 10 );
    assert( graphics_terminal.act( "\033[2;1H" ).empty() );
    assert( graphics_terminal.act( "\033_Ga=T,c=4,r=3,m=1;AAAA\033\\" ).empty() );
    assert( graphics_terminal.get_fb().ds.get_cursor_row() == 1 );
    assert( graphics_terminal.get_fb().ds.get_cursor_col() == 0 );
    assert( graphics_terminal.act( "\033_Gm=0;BBBB\033\\" ).empty() );
    /* Cursor moves down r=3 full rows (1 -> 4) and to anchor_col + c = 4. */
    assert( graphics_terminal.get_fb().ds.get_cursor_row() == 4 );
    assert( graphics_terminal.get_fb().ds.get_cursor_col() == 4 );
    assert( graphics_terminal.act( "\r\nX" ).empty() );
    assert_contains( graphics_terminal.get_fb().get_cell( 5, 0 )->debug_contents(), "'X'" );
  }

  {
    Terminal::Complete graphics_terminal( 80, 10 );
    assert( graphics_terminal.act( "\033[3;5H" ).empty() );
    assert( graphics_terminal.act( "\033_Ga=T,c=4,r=3,C=1;AAAA\033\\" ).empty() );
    assert( graphics_terminal.get_fb().ds.get_cursor_row() == 2 );
    assert( graphics_terminal.get_fb().ds.get_cursor_col() == 4 );
  }

  {
    Terminal::Complete graphics_terminal( 80, 10 );
    assert( graphics_terminal.act( "\033[2;79H" ).empty() );
    assert( graphics_terminal.act( "\033_Ga=T,c=2,r=1;AAAA\033\\" ).empty() );
    /* anchor_col + c = 78 + 2 = 80 clamps to the last column (79), no wrap. */
    assert( graphics_terminal.get_fb().ds.get_cursor_row() == 2 );
    assert( graphics_terminal.get_fb().ds.get_cursor_col() == 79 );
  }

  {
    Terminal::Complete graphics_terminal( 80, 5 );
    assert( graphics_terminal.act( "\033[3;1H" ).empty() );
    assert( graphics_terminal.act( "\033_Ga=T,c=1,r=1,C=1;AAAA\033\\" ).empty() );
    assert( graphics_terminal.get_fb().get_passthrough_sequences().size() == 1 );
    assert( graphics_terminal.get_fb().get_passthrough_sequences().front().cursor_row == 2 );
    assert( graphics_terminal.act( "\033[5;1H\n" ).empty() );
    assert( graphics_terminal.get_fb().get_passthrough_sequences().size() == 1 );
    assert( graphics_terminal.get_fb().get_passthrough_sequences().front().cursor_row == 1 );
    for ( int i = 0; i < 2; i++ ) {
      assert( graphics_terminal.act( "\033[5;1H\n" ).empty() );
    }
    assert( graphics_terminal.get_fb().get_passthrough_sequences().empty() );
  }

  {
    Terminal::Complete graphics_terminal( 80, 5 );
    assert( graphics_terminal.act( "\033_Ga=T,s=10,v=50,c=1,r=5,m=1;AAAA\033\\" ).empty() );
    assert( graphics_terminal.act( "\033_Gm=0;BBBB\033\\" ).empty() );
    /* Placing a 5-row image from the top row of a 5-row screen scrolls
       once during the placement itself (5 index operations), so the
       retained sequence is already clipped by one row (10 px) before any
       newline; the cursor lands on the bottom row. */
    assert( graphics_terminal.get_fb().ds.get_cursor_row() == 4 );
    assert( graphics_terminal.get_fb().get_passthrough_sequences().size() == 2 );
    assert( graphics_terminal.get_fb().get_passthrough_sequences().front().cursor_row == 0 );
    assert_contains( graphics_terminal.get_fb().get_passthrough_sequences().front().sequence, "y=10" );
    assert_contains( graphics_terminal.get_fb().get_passthrough_sequences().front().sequence, "h=40" );
    assert_contains( graphics_terminal.get_fb().get_passthrough_sequences().front().sequence, "r=4" );
    /* each further scroll clips another row (10 px) off the top */
    assert( graphics_terminal.act( "\n" ).empty() );
    assert_contains( graphics_terminal.get_fb().get_passthrough_sequences().front().sequence, "y=20" );
    assert_contains( graphics_terminal.get_fb().get_passthrough_sequences().front().sequence, "h=30" );
    assert_contains( graphics_terminal.get_fb().get_passthrough_sequences().front().sequence, "r=3" );
    assert( graphics_terminal.act( "\n" ).empty() );
    assert_contains( graphics_terminal.get_fb().get_passthrough_sequences().front().sequence, "y=30" );
    assert_contains( graphics_terminal.get_fb().get_passthrough_sequences().front().sequence, "h=20" );
    assert_contains( graphics_terminal.get_fb().get_passthrough_sequences().front().sequence, "r=2" );
  }

  {
    /* Placement inside a scroll region whose bottom is above the screen
       bottom keeps the cursor at the region bottom, exactly like the
       equivalent run of IND operations. */
    Terminal::Complete graphics_terminal( 80, 8 );
    assert( graphics_terminal.act( "\033[1;5r" ).empty() ); /* region rows 0..4 */
    assert( graphics_terminal.act( "\033[1;1H" ).empty() );
    assert( graphics_terminal.act( "\033_Ga=T,s=10,v=50,c=1,r=5,m=1;AAAA\033\\" ).empty() );
    assert( graphics_terminal.act( "\033_Gm=0;BBBB\033\\" ).empty() );

    Terminal::Complete ind_terminal( 80, 8 );
    assert( ind_terminal.act( "\033[1;5r" ).empty() );
    assert( ind_terminal.act( "\033[1;1H" ).empty() );
    for ( int i = 0; i < 5; i++ ) {
      assert( ind_terminal.act( "\033D" ).empty() );
    }
    assert( ind_terminal.get_fb().ds.get_cursor_row() == 4 );
    assert( graphics_terminal.get_fb().ds.get_cursor_row() == 4 );
  }

  {
    /* A chunk group that never terminates cannot grow retention without
       bound: past the hard ceiling the group is dropped, replaced by a
       quiet finisher for the host, and its later chunks are discarded. */
    Terminal::Framebuffer fb( 80, 24 );
    fb.push_passthrough_sequence( "\033_Ga=T,f=32,s=1,v=1,m=1\033\\" );
    const std::string continuation = std::string( "\033_Gm=1;" ) + std::string( 1024 * 1024, 'A' ) + "\033\\";
    for ( int i = 0; i < 33; i++ ) {
      fb.push_passthrough_sequence( continuation );
    }
    assert( fb.get_passthrough_sequences().size() == 1 );
    assert_contains( fb.get_passthrough_sequences().front().sequence, "q=2,m=0" );
    fb.push_passthrough_sequence( continuation );
    assert( fb.get_passthrough_sequences().size() == 1 );
    fb.push_passthrough_sequence( "\033_Gm=0;BBBB\033\\" ); /* the group's real finisher */
    assert( fb.get_passthrough_sequences().size() == 1 );
    fb.push_passthrough_sequence( "\033_Ga=T,c=1,r=1,C=1;AAAA\033\\" ); /* retention works again */
    assert( fb.get_passthrough_sequences().size() == 2 );
  }

  {
    Terminal::Complete graphics_terminal( 80, 10 );
    assert( graphics_terminal.act( "\033_Ga=T,c=1,r=1,C=1;AAAA\033\\" ).empty() );
    assert( graphics_terminal.act( "\033_Ga=d\033\\" ).empty() );
    assert( graphics_terminal.get_fb().get_passthrough_sequences().size() == 1 );
    frame = frame_from( Terminal::Framebuffer( 80, 10 ), graphics_terminal.get_fb() );
    assert_not_contains( frame, "\033_Ga=T,c=1,r=1,C=1;AAAA\033\\" );
    assert_contains( frame, "\033_Ga=d\033\\" );
  }

  {
    /* C=1 is forced onto forwarded/stored placement bytes so the host
       terminal never moves its own cursor; the app's C=0 (or absent C)
       is rewritten. */
    Terminal::Complete graphics_terminal( 80, 24 );
    assert( graphics_terminal.act( "\033_Ga=T,f=32,s=1,v=1,c=1,r=1;AAAA\033\\" ).empty() );
    frame = frame_from( Terminal::Framebuffer( 80, 24 ), graphics_terminal.get_fb() );
    assert_contains( frame, "\033_Ga=T,f=32,s=1,v=1,c=1,r=1,C=1;AAAA\033\\" );
    assert_not_contains( frame, "\033_Ga=T,f=32,s=1,v=1,c=1,r=1;AAAA\033\\" );
  }

  {
    Terminal::Complete graphics_terminal( 80, 24 );
    assert( graphics_terminal.act( "\033_Ga=T,f=32,s=1,v=1,c=1,r=1,C=0;AAAA\033\\" ).empty() );
    frame = frame_from( Terminal::Framebuffer( 80, 24 ), graphics_terminal.get_fb() );
    assert_contains( frame, "\033_Ga=T,f=32,s=1,v=1,c=1,r=1,C=1;AAAA\033\\" );
    assert_not_contains( frame, "C=0" );
  }

  {
    /* Query commands are never rewritten with C=1. */
    Terminal::Complete graphics_terminal( 80, 24 );
    assert( graphics_terminal.act( "\033_Ga=q,i=1;AAAA\033\\" ).empty() );
    frame = frame_from( Terminal::Framebuffer( 80, 24 ), graphics_terminal.get_fb() );
    assert_contains( frame, "\033_Ga=q,i=1;AAAA\033\\" );
    assert_not_contains( frame, "C=1" );
  }

  {
    /* A full-frame repaint deletes all host images before replaying the
       retained placement, and only when graphics are actually retained. */
    Terminal::Complete graphics_terminal( 80, 10 );
    assert( graphics_terminal.act( "\033_Ga=T,f=32,s=1,v=1,c=1,r=1,C=1;AAAA\033\\" ).empty() );
    Terminal::Framebuffer copy = graphics_terminal.get_fb();
    Terminal::Display display( false );
    const std::string repaint = display.new_frame( false, copy, graphics_terminal.get_fb() );
    const size_t del = repaint.find( "\033_Ga=d,d=A\033\\" );
    const size_t placement = repaint.find( "\033_Ga=T" );
    assert( del != std::string::npos );
    assert( placement != std::string::npos );
    assert( del < placement );

    const Terminal::Framebuffer plain( 80, 10 );
    assert_not_contains( display.new_frame( false, plain, plain ), "\033_Ga=d" );
  }

  {
    /* On a full-frame repaint a retained placement is replayed but a
       retained query is not. */
    Terminal::Complete graphics_terminal( 80, 10 );
    assert( graphics_terminal.act( "\033_Ga=q,i=1;AAAA\033\\" ).empty() );
    assert( graphics_terminal.act( "\033_Ga=T,f=32,s=1,v=1,c=1,r=1,C=1;AAAA\033\\" ).empty() );
    Terminal::Framebuffer copy = graphics_terminal.get_fb();
    Terminal::Display display( false );
    const std::string repaint = display.new_frame( false, copy, graphics_terminal.get_fb() );
    assert_contains( repaint, "\033_Ga=T" );
    assert_not_contains( repaint, "\033_Ga=q" );
  }

  {
    /* Eviction is chunk-group-atomic: completed groups are dropped whole
       from the front, never leaving orphaned continuation chunks. */
    const size_t chunk = 512 * 1024;
    auto push_group = []( Terminal::Complete& t, char payload, int mid_chunks ) {
      assert( t.act( "\033_Ga=T,f=32,s=1,v=1,m=1\033\\" ).empty() );
      for ( int i = 0; i < mid_chunks; i++ ) {
        assert( t.act( std::string( "\033_Gm=1;" ) + std::string( 512 * 1024, payload ) + "\033\\" ).empty() );
      }
      assert( t.act( std::string( "\033_Gm=0;" ) + std::string( 512 * 1024, payload ) + "\033\\" ).empty() );
    };

    Terminal::Complete graphics_terminal( 80, 24 );
    push_group( graphics_terminal, 'A', 2 ); /* ~1.5 MiB each */
    push_group( graphics_terminal, 'B', 2 );
    push_group( graphics_terminal, 'C', 2 ); /* total ~4.5 MiB > 4 MiB cap */

    const Terminal::Framebuffer::passthrough_sequences_type& seqs
      = graphics_terminal.get_fb().get_passthrough_sequences();
    assert( !seqs.empty() );
    assert_contains( seqs.front().sequence, "a=T" ); /* front starts a group */
    size_t total = 0;
    bool has_a = false;
    for ( const auto& s : seqs ) {
      total += s.sequence.size();
      if ( s.sequence.find( std::string( 512, 'A' ) ) != std::string::npos ) {
        has_a = true;
      }
    }
    assert( total <= 4u * 1024 * 1024 );
    assert( !has_a ); /* the oldest group was evicted whole */
    (void)chunk;
  }

  {
    /* An in-progress (unterminated) group is never partially evicted:
       older complete groups go first and the caps run soft until it
       finishes. */
    Terminal::Complete graphics_terminal( 80, 24 );
    auto push_group = []( Terminal::Complete& t, char payload, int mid_chunks ) {
      assert( t.act( "\033_Ga=T,f=32,s=1,v=1,m=1\033\\" ).empty() );
      for ( int i = 0; i < mid_chunks; i++ ) {
        assert( t.act( std::string( "\033_Gm=1;" ) + std::string( 512 * 1024, payload ) + "\033\\" ).empty() );
      }
      assert( t.act( std::string( "\033_Gm=0;" ) + std::string( 512 * 1024, payload ) + "\033\\" ).empty() );
    };
    push_group( graphics_terminal, 'A', 2 );
    push_group( graphics_terminal, 'B', 2 );
    /* Open a third group over 4 MiB and leave it unterminated. */
    assert( graphics_terminal.act( "\033_Ga=T,f=32,s=1,v=1,m=1\033\\" ).empty() );
    const int c_chunks = 8;
    for ( int i = 0; i < c_chunks; i++ ) {
      const std::string c_chunk = std::string( "\033_Gm=1;" ) + std::string( 512 * 1024, 'C' ) + "\033\\";
      assert( graphics_terminal.act( c_chunk ).empty() );
    }

    const Terminal::Framebuffer::passthrough_sequences_type& seqs
      = graphics_terminal.get_fb().get_passthrough_sequences();
    assert_contains( seqs.front().sequence, "a=T" );
    size_t c_count = 0;
    bool has_ab = false;
    for ( const auto& s : seqs ) {
      if ( s.sequence.find( std::string( 512, 'C' ) ) != std::string::npos ) {
        c_count++;
      }
      if ( s.sequence.find( std::string( 512, 'A' ) ) != std::string::npos
           || s.sequence.find( std::string( 512, 'B' ) ) != std::string::npos ) {
        has_ab = true;
      }
    }
    assert( c_count == (size_t)c_chunks ); /* in-progress group kept every chunk */
    assert( !has_ab );                     /* older complete groups evicted */
  }

  before = terminal.get_fb();
  assert( terminal.act( "\033Pq~~~~\033\\" ).empty() );
  frame = frame_from( before, terminal.get_fb() );
  assert_contains( frame, "\033Pq~~~~\033\\" );

  before = terminal.get_fb();
  assert( terminal.act( "\033]1337;File=name=dGVzdA==;inline=1:AAAA\033\\" ).empty() );
  frame = frame_from( before, terminal.get_fb() );
  assert_contains( frame, "\033]1337;File=name=dGVzdA==;inline=1:AAAA\007" );

  before = terminal.get_fb();
  assert( terminal.act( "\033]1337;File=name=dGVzdA==:AAAA\033\\" ).empty() );
  frame = frame_from( before, terminal.get_fb() );
  assert_not_contains( frame, "\033]1337;File=name=dGVzdA==:AAAA\007" );

  return 0;
}
