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
#include "src/statesync/user.h"

int main( void )
{
  Terminal::Complete terminal( 80, 24 );

  assert( terminal.act( "\033]11;?\033\\" ).empty() );

  terminal.set_OSC_color_response( 10, "rgb:0000/0000/0000" );
  terminal.set_OSC_color_response( 11, "rgb:ffff/ffff/ffff" );
  terminal.set_OSC_color_response( 12, "rgb:8888/8888/8888" );

  assert( terminal.act( "\033]10;?\033\\" ) == "\033]10;rgb:0000/0000/0000\033\\" );
  assert( terminal.act( "\033]11;?\007" ) == "\033]11;rgb:ffff/ffff/ffff\033\\" );
  assert( terminal.act( "\033]12;?\033\\" ) == "\033]12;rgb:8888/8888/8888\033\\" );

  terminal.set_OSC_color_response( 11, "\033]52;c;bad" );
  assert( terminal.act( "\033]11;?\033\\" ) == "\033]11;rgb:ffff/ffff/ffff\033\\" );

  assert( terminal.act( "\033]12;rgb:1111/2222/3333\033\\" ).empty() );
  assert( terminal.act( "\033]12;?\033\\" ) == "\033]12;rgb:1111/2222/3333\033\\" );

  Terminal::Complete terminal_without_local_cursor_color( 80, 24 );
  assert( terminal_without_local_cursor_color.act( "\033]12;rgb:1111/2222/3333\033\\" ).empty() );
  assert( terminal_without_local_cursor_color.act( "\033]12;?\033\\" ) == "\033]12;rgb:1111/2222/3333\033\\" );
  assert( terminal_without_local_cursor_color.act( "\033]112\033\\" ).empty() );
  assert( terminal_without_local_cursor_color.act( "\033]12;?\033\\" ).empty() );

  Network::UserStream colors;
  colors.push_back_terminal_color( 12, "rgb:1111/2222/3333" );

  Network::UserStream decoded;
  decoded.apply_string( colors.init_diff() );

  assert( decoded.size() == 1 );
  assert( decoded.get_event( 0 ).type == Network::TerminalColorType );
  assert( decoded.get_event( 0 ).terminal_color_osc == 12 );
  assert( decoded.get_event( 0 ).terminal_color == "rgb:1111/2222/3333" );

  return 0;
}
