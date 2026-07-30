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
