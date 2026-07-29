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

static std::string hex_encode( const std::string& str )
{
  static const char hex_digits[] = "0123456789abcdef";
  std::string ret;
  for ( unsigned char ch : str ) {
    ret.push_back( hex_digits[ch >> 4] );
    ret.push_back( hex_digits[ch & 0xf] );
  }
  return ret;
}

int main( void )
{
  Terminal::Complete terminal( 80, 24 );

  assert( terminal.act( "\033[?25$p" ) == "\033[?25;1$y" );
  assert( terminal.act( "\033[?25l" ).empty() );
  assert( terminal.act( "\033[?25$p" ) == "\033[?25;2$y" );
  assert( terminal.act( "\033[?12$p" ) == "\033[?12;1$y" );
  assert( terminal.act( "\033[?12l" ).empty() );
  assert( terminal.act( "\033[?12$p" ) == "\033[?12;2$y" );

  assert( terminal.act( "\033[4$p" ) == "\033[4;2$y" );
  assert( terminal.act( "\033[4h" ).empty() );
  assert( terminal.act( "\033[4$p" ) == "\033[4;1$y" );
  assert( terminal.act( "\033[999$p" ) == "\033[999;0$y" );

  assert( terminal.act( "\033[2;4:3;9m" ).empty() );
  assert( terminal.act( "\033P$qm\033\\" ) == "\033P1$r0;2;4:3;9m\033\\" );

  assert( terminal.act( "\033[5 q" ).empty() );
  assert( terminal.act( "\033P$q q\033\\" ) == "\033P1$r5 q\033\\" );
  assert( terminal.act( "\033[3;20r" ).empty() );
  assert( terminal.act( "\033P$qr\033\\" ) == "\033P1$r3;20r\033\\" );
  assert( terminal.act( "\033P$qbad\033\\" ) == "\033P0$r\033\\" );

  const std::string xtgettcap_request = "\033P+q" + hex_encode( "Ss" ) + ";" + hex_encode( "Se" ) + "\033\\";
  const std::string xtgettcap_response = "\033P1+r" + hex_encode( "Ss" ) + "="
                                         + hex_encode( "\033[%p1%d q" ) + ";" + hex_encode( "Se" ) + "="
                                         + hex_encode( "\033[2 q" ) + "\033\\";
  assert( terminal.act( xtgettcap_request ) == xtgettcap_response );
  assert( terminal.act( "\033P+q5a5a\033\\" ) == "\033P0+r\033\\" );

  return 0;
}
