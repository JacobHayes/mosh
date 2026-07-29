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

static void assert_cell_sgr( const Terminal::Complete& terminal, int row, int col, const std::string& sgr )
{
  assert( terminal.get_fb().get_cell( row, col )->get_renditions().sgr() == sgr );
}

int main( void )
{
  Terminal::Complete terminal( 80, 24 );

  terminal.act( "\033[2mA\033[22mB" );
  assert_cell_sgr( terminal, 0, 0, "\033[0;2m" );
  assert_cell_sgr( terminal, 0, 1, "\033[0m" );

  terminal.act( "\033[9mC\033[29mD" );
  assert_cell_sgr( terminal, 0, 2, "\033[0;9m" );
  assert_cell_sgr( terminal, 0, 3, "\033[0m" );

  terminal.act( "\033[4:3mE\033[58;5;123mF\033[58:2:1:2:3mG\033[59mH" );
  assert_cell_sgr( terminal, 0, 4, "\033[0;4:3m" );
  assert_cell_sgr( terminal, 0, 5, "\033[0;4:3;58;5;123m" );
  assert_cell_sgr( terminal, 0, 6, "\033[0;4:3;58;2;1;2;3m" );
  assert_cell_sgr( terminal, 0, 7, "\033[0;4:3m" );

  terminal.act( "\033[24m\033[38:2:4:5:6mI\033[48:5:200mJ" );
  assert_cell_sgr( terminal, 0, 8, "\033[0;38;2;4;5;6m" );
  assert_cell_sgr( terminal, 0, 9, "\033[0;38;2;4;5;6;48;5;200m" );

  terminal.act( "\033[5 q\033[?12l\033]12;rgb:1111/2222/3333\033\\" );
  assert( terminal.get_fb().ds.cursor_shape == Terminal::DrawState::CURSOR_SHAPE_BAR );
  assert( !terminal.get_fb().ds.cursor_blink );
  assert( terminal.get_fb().ds.cursor_style_param() == 6 );
  assert( terminal.get_fb().ds.cursor_color == "rgb:1111/2222/3333" );

  Terminal::Framebuffer blank( 80, 24 );
  Terminal::Display display( false );
  const std::string frame = display.new_frame( false, blank, terminal.get_fb() );
  assert( frame.find( "\033[6 q" ) != std::string::npos );
  assert( frame.find( "\033]12;rgb:1111/2222/3333\007" ) != std::string::npos );

  terminal.act( "\033]112\033\\" );
  assert( terminal.get_fb().ds.cursor_color.empty() );

  return 0;
}
