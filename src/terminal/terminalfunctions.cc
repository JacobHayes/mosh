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
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include <unistd.h>

#include "src/terminal/parseraction.h"
#include "src/terminal/terminalframebuffer.h"
#include "terminaldispatcher.h"

using namespace Terminal;

/* Terminal functions -- routines activated by CSI, escape or a control char */

static void clearline( Framebuffer* fb, int row, int start, int end )
{
  for ( int col = start; col <= end; col++ ) {
    fb->reset_cell( fb->get_mutable_cell( row, col ) );
  }
}

/* erase in line */
static void CSI_EL( Framebuffer* fb, Dispatcher* dispatch )
{
  switch ( dispatch->getparam( 0, 0 ) ) {
    case 0: /* default: active position to end of line, inclusive */
      clearline( fb, -1, fb->ds.get_cursor_col(), fb->ds.get_width() - 1 );
      break;
    case 1: /* start of screen to active position, inclusive */
      clearline( fb, -1, 0, fb->ds.get_cursor_col() );
      break;
    case 2: /* all of line */
      fb->reset_row( fb->get_mutable_row( -1 ) );
      break;
    default:
      break;
  }
}

static Function func_CSI_EL( CSI, "K", CSI_EL );

/* erase in display */
static void CSI_ED( Framebuffer* fb, Dispatcher* dispatch )
{
  switch ( dispatch->getparam( 0, 0 ) ) {
    case 0: /* active position to end of screen, inclusive */
      clearline( fb, -1, fb->ds.get_cursor_col(), fb->ds.get_width() - 1 );
      for ( int y = fb->ds.get_cursor_row() + 1; y < fb->ds.get_height(); y++ ) {
        fb->reset_row( fb->get_mutable_row( y ) );
      }
      break;
    case 1: /* start of screen to active position, inclusive */
      for ( int y = 0; y < fb->ds.get_cursor_row(); y++ ) {
        fb->reset_row( fb->get_mutable_row( y ) );
      }
      clearline( fb, -1, 0, fb->ds.get_cursor_col() );
      break;
    case 2: /* entire screen */
      /* Ghostty/iTerm2-style: the erased contents move into
         scrollback (this is what makes Ctrl-L feel native) */
      fb->capture_screen_to_history();
      for ( int y = 0; y < fb->ds.get_height(); y++ ) {
        fb->reset_row( fb->get_mutable_row( y ) );
      }
      fb->clear_graphics_passthrough_sequences();
      break;
    case 3: /* saved lines (xterm) -- clears scrollback, leaves screen alone */
      fb->clear_history_scrollback();
      break;
    default:
      break;
  }
}

static Function func_CSI_ED( CSI, "J", CSI_ED );

/* cursor movement -- relative and absolute */
static void CSI_cursormove( Framebuffer* fb, Dispatcher* dispatch )
{
  int num = dispatch->getparam( 0, 1 );

  switch ( dispatch->get_dispatch_chars()[0] ) {
    case 'A':
      fb->ds.move_row( -num, true );
      break;
    case 'B':
      fb->ds.move_row( num, true );
      break;
    case 'C':
      fb->ds.move_col( num, true );
      break;
    case 'D':
      fb->ds.move_col( -num, true );
      break;
    case 'H':
    case 'f':
      fb->ds.move_row( dispatch->getparam( 0, 1 ) - 1 );
      fb->ds.move_col( dispatch->getparam( 1, 1 ) - 1 );
      break;
    default:
      break;
  }
}

static Function func_CSI_cursormove_A( CSI, "A", CSI_cursormove );
static Function func_CSI_cursormove_B( CSI, "B", CSI_cursormove );
static Function func_CSI_cursormove_C( CSI, "C", CSI_cursormove );
static Function func_CSI_cursormove_D( CSI, "D", CSI_cursormove );
static Function func_CSI_cursormove_H( CSI, "H", CSI_cursormove );
static Function func_CSI_cursormove_f( CSI, "f", CSI_cursormove );

/* device attributes */
static void CSI_DA( Framebuffer* fb __attribute( ( unused ) ), Dispatcher* dispatch )
{
  dispatch->terminal_to_host.append( "\033[?62c" ); /* plain vt220 */
}

static Function func_CSI_DA( CSI, "c", CSI_DA );

/* secondary device attributes */
static void CSI_SDA( Framebuffer* fb __attribute( ( unused ) ), Dispatcher* dispatch )
{
  dispatch->terminal_to_host.append( "\033[>1;10;0c" ); /* plain vt220 */
}

static Function func_CSI_SDA( CSI, ">c", CSI_SDA );

/* screen alignment diagnostic */
static void Esc_DECALN( Framebuffer* fb, Dispatcher* dispatch __attribute( ( unused ) ) )
{
  for ( int y = 0; y < fb->ds.get_height(); y++ ) {
    for ( int x = 0; x < fb->ds.get_width(); x++ ) {
      fb->reset_cell( fb->get_mutable_cell( y, x ) );
      fb->get_mutable_cell( y, x )->append( 'E' );
    }
  }
}

static Function func_Esc_DECALN( ESCAPE, "#8", Esc_DECALN );

/* line feed */
static void Ctrl_LF( Framebuffer* fb, Dispatcher* dispatch __attribute( ( unused ) ) )
{
  fb->move_rows_autoscroll( 1 );
}

/* same procedure for index, vertical tab, and form feed control codes */
static Function func_Ctrl_LF( CONTROL, "\x0a", Ctrl_LF );
static Function func_Ctrl_IND( CONTROL, "\x84", Ctrl_LF );
static Function func_Ctrl_VT( CONTROL, "\x0b", Ctrl_LF );
static Function func_Ctrl_FF( CONTROL, "\x0c", Ctrl_LF );

/* carriage return */
static void Ctrl_CR( Framebuffer* fb, Dispatcher* dispatch __attribute( ( unused ) ) )
{
  fb->ds.move_col( 0 );
}

static Function func_Ctrl_CR( CONTROL, "\x0d", Ctrl_CR );

/* backspace */
static void Ctrl_BS( Framebuffer* fb, Dispatcher* dispatch __attribute( ( unused ) ) )
{
  fb->ds.move_col( -1, true );
}

static Function func_Ctrl_BS( CONTROL, "\x08", Ctrl_BS );

/* reverse index -- like a backwards line feed */
static void Ctrl_RI( Framebuffer* fb, Dispatcher* dispatch __attribute( ( unused ) ) )
{
  fb->move_rows_autoscroll( -1 );
}

static Function func_Ctrl_RI( CONTROL, "\x8D", Ctrl_RI );

/* newline */
static void Ctrl_NEL( Framebuffer* fb, Dispatcher* dispatch __attribute( ( unused ) ) )
{
  fb->ds.move_col( 0 );
  fb->move_rows_autoscroll( 1 );
}

static Function func_Ctrl_NEL( CONTROL, "\x85", Ctrl_NEL );

/* horizontal tab */
static void HT_n( Framebuffer* fb, size_t count )
{
  int col = fb->ds.get_next_tab( count );
  if ( col == -1 ) { /* no tabs, go to end of line */
    col = fb->ds.get_width() - 1;
  }

  /* A horizontal tab is the only operation that preserves but
     does not set the wrap state. It also starts a new grapheme. */

  bool wrap_state_save = fb->ds.next_print_will_wrap;
  fb->ds.move_col( col, false );
  fb->ds.next_print_will_wrap = wrap_state_save;
}

static void Ctrl_HT( Framebuffer* fb, Dispatcher* dispatch __attribute( ( unused ) ) )
{
  HT_n( fb, 1 );
}
static Function func_Ctrl_HT( CONTROL, "\x09", Ctrl_HT, false );

static void CSI_CxT( Framebuffer* fb, Dispatcher* dispatch )
{
  int param = dispatch->getparam( 0, 1 );
  if ( dispatch->get_dispatch_chars()[0] == 'Z' ) {
    param = -param;
  }
  if ( param == 0 ) {
    return;
  }
  HT_n( fb, param );
}

static Function func_CSI_CHT( CSI, "I", CSI_CxT, false );
static Function func_CSI_CBT( CSI, "Z", CSI_CxT, false );

/* horizontal tab set */
static void Ctrl_HTS( Framebuffer* fb, Dispatcher* dispatch __attribute( ( unused ) ) )
{
  fb->ds.set_tab();
}

static Function func_Ctrl_HTS( CONTROL, "\x88", Ctrl_HTS );

/* tabulation clear */
static void CSI_TBC( Framebuffer* fb, Dispatcher* dispatch )
{
  int param = dispatch->getparam( 0, 0 );
  switch ( param ) {
    case 0: /* clear this tab stop */
      fb->ds.clear_tab( fb->ds.get_cursor_col() );
      break;
    case 3: /* clear all tab stops */
      fb->ds.clear_default_tabs();
      for ( int x = 0; x < fb->ds.get_width(); x++ ) {
        fb->ds.clear_tab( x );
      }
      break;
    default:
      break;
  }
}

/* TBC preserves wrap state */
static Function func_CSI_TBC( CSI, "g", CSI_TBC, false );

static bool* get_DEC_mode( int param, Framebuffer* fb )
{
  switch ( param ) {
    case 1: /* cursor key mode */
      return &( fb->ds.application_mode_cursor_keys );
    case 3: /* 80/132. Ignore but clear screen. */
      /* clear screen */
      fb->ds.move_row( 0 );
      fb->ds.move_col( 0 );
      for ( int y = 0; y < fb->ds.get_height(); y++ ) {
        fb->reset_row( fb->get_mutable_row( y ) );
      }
      return NULL;
    case 5: /* reverse video */
      return &( fb->ds.reverse_video );
    case 6: /* origin */
      fb->ds.move_row( 0 );
      fb->ds.move_col( 0 );
      return &( fb->ds.origin_mode );
    case 7: /* auto wrap */
      return &( fb->ds.auto_wrap_mode );
    case 12: /* cursor blink */
      return &( fb->ds.cursor_blink );
    case 25:
      return &( fb->ds.cursor_visible );
    case 1004: /* xterm mouse focus event */
      return &( fb->ds.mouse_focus_event );
    case 1007: /* xterm mouse alternate scroll */
      return &( fb->ds.mouse_alternate_scroll );
    case 2004: /* bracketed paste */
      return &( fb->ds.bracketed_paste );
    default:
      break;
  }
  return NULL;
}

/* helper for CSI_DECSM and CSI_DECRM */
static void set_if_available( bool* mode, bool value )
{
  if ( mode ) {
    *mode = value;
  }
}

/* set private mode */
static void CSI_DECSM( Framebuffer* fb, Dispatcher* dispatch )
{
  for ( int i = 0; i < dispatch->param_count(); i++ ) {
    int param = dispatch->getparam( i, 0 );
    if ( param == 9 || ( param >= 1000 && param <= 1003 ) ) {
      fb->ds.mouse_reporting_mode = (Terminal::DrawState::MouseReportingMode)param;
    } else if ( param == 1005 || param == 1006 || param == 1015 ) {
      fb->ds.mouse_encoding_mode = (Terminal::DrawState::MouseEncodingMode)param;
    } else if ( param == 1047 || param == 1049 ) {
      fb->switch_to_alternate_screen( param == 1049 );
    } else if ( param == 1048 ) {
      if ( fb->get_altscreen_enabled() ) {
        fb->ds.save_cursor();
      }
    } else {
      set_if_available( get_DEC_mode( param, fb ), true );
    }
  }
}

/* clear private mode */
static void CSI_DECRM( Framebuffer* fb, Dispatcher* dispatch )
{
  for ( int i = 0; i < dispatch->param_count(); i++ ) {
    int param = dispatch->getparam( i, 0 );
    if ( param == 9 || ( param >= 1000 && param <= 1003 ) ) {
      fb->ds.mouse_reporting_mode = Terminal::DrawState::MOUSE_REPORTING_NONE;
    } else if ( param == 1005 || param == 1006 || param == 1015 ) {
      fb->ds.mouse_encoding_mode = Terminal::DrawState::MOUSE_ENCODING_DEFAULT;
    } else if ( param == 1047 || param == 1049 ) {
      fb->switch_to_primary_screen( param == 1049 );
    } else if ( param == 1048 ) {
      if ( fb->get_altscreen_enabled() ) {
        fb->ds.restore_cursor();
      }
    } else {
      set_if_available( get_DEC_mode( param, fb ), false );
    }
  }
}

/* These functions don't clear wrap state. */
static Function func_CSI_DECSM( CSI, "?h", CSI_DECSM, false );
static Function func_CSI_DECRM( CSI, "?l", CSI_DECRM, false );

static bool* get_ANSI_mode( int param, Framebuffer* fb )
{
  if ( param == 4 ) { /* insert/replace mode */
    return &( fb->ds.insert_mode );
  }
  return NULL;
}

/* set mode */
static void CSI_SM( Framebuffer* fb, Dispatcher* dispatch )
{
  for ( int i = 0; i < dispatch->param_count(); i++ ) {
    bool* mode = get_ANSI_mode( dispatch->getparam( i, 0 ), fb );
    if ( mode ) {
      *mode = true;
    }
  }
}

/* clear mode */
static void CSI_RM( Framebuffer* fb, Dispatcher* dispatch )
{
  for ( int i = 0; i < dispatch->param_count(); i++ ) {
    bool* mode = get_ANSI_mode( dispatch->getparam( i, 0 ), fb );
    if ( mode ) {
      *mode = false;
    }
  }
}

static Function func_CSI_SM( CSI, "h", CSI_SM );
static Function func_CSI_RM( CSI, "l", CSI_RM );

static int mode_status( const bool recognized, const bool set )
{
  if ( !recognized ) {
    return 0;
  }
  return set ? 1 : 2;
}

static int DEC_mode_status( const int param, const Framebuffer* fb )
{
  switch ( param ) {
    case 1:
      return mode_status( true, fb->ds.application_mode_cursor_keys );
    case 5:
      return mode_status( true, fb->ds.reverse_video );
    case 6:
      return mode_status( true, fb->ds.origin_mode );
    case 7:
      return mode_status( true, fb->ds.auto_wrap_mode );
    case 12:
      return mode_status( true, fb->ds.cursor_blink );
    case 25:
      return mode_status( true, fb->ds.cursor_visible );
    case 9:
    case 1000:
    case 1001:
    case 1002:
    case 1003:
      return mode_status( true, fb->ds.mouse_reporting_mode == param );
    case 1004:
      return mode_status( true, fb->ds.mouse_focus_event );
    case 1005:
    case 1006:
    case 1015:
      return mode_status( true, fb->ds.mouse_encoding_mode == param );
    case 1007:
      return mode_status( true, fb->ds.mouse_alternate_scroll );
    case 1047:
    case 1049:
      return mode_status( true, fb->get_alt_screen_active() );
    case 2004:
      return mode_status( true, fb->ds.bracketed_paste );
    default:
      return 0;
  }
}

static int ANSI_mode_status( const int param, const Framebuffer* fb )
{
  switch ( param ) {
    case 4:
      return mode_status( true, fb->ds.insert_mode );
    default:
      return 0;
  }
}

static void CSI_DECRQM_DEC( Framebuffer* fb, Dispatcher* dispatch )
{
  const int param = dispatch->getparam( 0, 0 );
  char response[64];
  snprintf( response, sizeof( response ), "\033[?%d;%d$y", param, DEC_mode_status( param, fb ) );
  dispatch->terminal_to_host.append( response );
}

static void CSI_DECRQM_ANSI( Framebuffer* fb, Dispatcher* dispatch )
{
  const int param = dispatch->getparam( 0, 0 );
  char response[64];
  snprintf( response, sizeof( response ), "\033[%d;%d$y", param, ANSI_mode_status( param, fb ) );
  dispatch->terminal_to_host.append( response );
}

static Function func_CSI_DECRQM_DEC( CSI, "?$p", CSI_DECRQM_DEC );
static Function func_CSI_DECRQM_ANSI( CSI, "$p", CSI_DECRQM_ANSI );

static void CSI_XTMODKEYS( Framebuffer* fb, Dispatcher* dispatch )
{
  const int option = dispatch->getparam( 0, -1 );
  if ( option != 4 ) { /* modifyOtherKeys */
    return;
  }

  int value = dispatch->getparam( 1, 0 );
  if ( value < 0 ) {
    value = 0;
  }
  if ( value > 3 ) {
    value = 3;
  }
  fb->ds.modify_other_keys = value;
}

static void CSI_XTDISABLEMODKEYS( Framebuffer* fb, Dispatcher* dispatch )
{
  const int option = dispatch->getparam( 0, -1 );
  if ( option == 4 ) { /* modifyOtherKeys */
    fb->ds.modify_other_keys = 0;
  }
}

static void CSI_XTQMODKEYS( Framebuffer* fb, Dispatcher* dispatch )
{
  const int option = dispatch->getparam( 0, -1 );
  if ( option != 4 ) { /* modifyOtherKeys */
    return;
  }

  char response[32];
  snprintf( response, sizeof( response ), "\033[>4;%dm", fb->ds.modify_other_keys );
  dispatch->terminal_to_host.append( response );
}

static const int KITTY_KEYBOARD_FLAGS_MASK = 0x1f;
static const size_t KITTY_KEYBOARD_STACK_MAX = 16;

static void set_kitty_keyboard_flags( Framebuffer* fb, int flags )
{
  if ( flags < 0 ) {
    flags = 0;
  }
  fb->ds.kitty_keyboard_flags = flags & KITTY_KEYBOARD_FLAGS_MASK;
}

static void CSI_KITTY_KEYBOARD_SET( Framebuffer* fb, Dispatcher* dispatch )
{
  const int flags = dispatch->getparam( 0, 0 ) & KITTY_KEYBOARD_FLAGS_MASK;
  const int mode = dispatch->getparam( 1, 1 );

  if ( mode == 1 ) {
    set_kitty_keyboard_flags( fb, flags );
  } else if ( mode == 2 ) {
    set_kitty_keyboard_flags( fb, fb->ds.kitty_keyboard_flags | flags );
  } else if ( mode == 3 ) {
    set_kitty_keyboard_flags( fb, fb->ds.kitty_keyboard_flags & ~flags );
  }
}

static void CSI_KITTY_KEYBOARD_PUSH( Framebuffer* fb, Dispatcher* dispatch )
{
  fb->ds.kitty_keyboard_stack.push_back( fb->ds.kitty_keyboard_flags );
  if ( fb->ds.kitty_keyboard_stack.size() > KITTY_KEYBOARD_STACK_MAX ) {
    fb->ds.kitty_keyboard_stack.erase( fb->ds.kitty_keyboard_stack.begin() );
  }
  set_kitty_keyboard_flags( fb, dispatch->getparam( 0, 0 ) );
}

static void CSI_KITTY_KEYBOARD_POP( Framebuffer* fb, Dispatcher* dispatch )
{
  int count = dispatch->getparam( 0, 1 );
  if ( count < 1 ) {
    count = 1;
  }

  int remaining = count;
  while ( remaining > 0 && !fb->ds.kitty_keyboard_stack.empty() ) {
    set_kitty_keyboard_flags( fb, fb->ds.kitty_keyboard_stack.back() );
    fb->ds.kitty_keyboard_stack.pop_back();
    remaining--;
  }

  if ( remaining > 0 ) {
    set_kitty_keyboard_flags( fb, 0 );
    fb->ds.kitty_keyboard_stack.clear();
  }
}

static void CSI_KITTY_KEYBOARD_QUERY( Framebuffer* fb, Dispatcher* dispatch )
{
  char response[32];
  snprintf( response, sizeof( response ), "\033[?%du", fb->ds.kitty_keyboard_flags );
  dispatch->terminal_to_host.append( response );
}

static Function func_CSI_XTMODKEYS( CSI, ">m", CSI_XTMODKEYS, false );
static Function func_CSI_XTDISABLEMODKEYS( CSI, ">n", CSI_XTDISABLEMODKEYS, false );
static Function func_CSI_XTQMODKEYS( CSI, "?m", CSI_XTQMODKEYS, false );
static Function func_CSI_KITTY_KEYBOARD_SET( CSI, "=u", CSI_KITTY_KEYBOARD_SET, false );
static Function func_CSI_KITTY_KEYBOARD_PUSH( CSI, ">u", CSI_KITTY_KEYBOARD_PUSH, false );
static Function func_CSI_KITTY_KEYBOARD_POP( CSI, "<u", CSI_KITTY_KEYBOARD_POP, false );
static Function func_CSI_KITTY_KEYBOARD_QUERY( CSI, "?u", CSI_KITTY_KEYBOARD_QUERY, false );

/* set top and bottom margins */
static void CSI_DECSTBM( Framebuffer* fb, Dispatcher* dispatch )
{
  int top = dispatch->getparam( 0, 1 );
  int bottom = dispatch->getparam( 1, fb->ds.get_height() );

  if ( ( bottom <= top ) || ( top > fb->ds.get_height() ) || ( top == 0 && bottom == 1 ) ) {
    return; /* invalid, xterm ignores */
  }

  fb->ds.set_scrolling_region( top - 1, bottom - 1 );
  fb->ds.move_row( 0 );
  fb->ds.move_col( 0 );
}

static Function func_CSI_DECSTMB( CSI, "r", CSI_DECSTBM );

/* terminal bell */
static void Ctrl_BEL( Framebuffer* fb, Dispatcher* dispatch __attribute( ( unused ) ) )
{
  fb->ring_bell();
}

static Function func_Ctrl_BEL( CONTROL, "\x07", Ctrl_BEL );

/* select graphics rendition -- e.g., bold, blinking, etc. */
struct SGRParam
{
  std::vector<int> subparams;

  SGRParam()
    : subparams()
  {}

  explicit SGRParam( const std::vector<int>& s_subparams )
    : subparams( s_subparams )
  {}
};

static bool parse_unsigned_param( const std::string& str, const size_t begin, const size_t end, int& value )
{
  if ( begin == end ) {
    return false;
  }

  int ret = 0;
  for ( size_t loc = begin; loc < end; loc++ ) {
    if ( str[loc] < '0' || str[loc] > '9' ) {
      return false;
    }
    ret = ret * 10 + str[loc] - '0';
    if ( ret > Dispatcher::PARAM_MAX ) {
      return false;
    }
  }

  value = ret;
  return true;
}

static std::vector<SGRParam> parse_SGR_params( const std::string& params )
{
  std::vector<SGRParam> ret;

  if ( params.empty() ) {
    ret.push_back( SGRParam { std::vector<int>( 1, 0 ) } );
    return ret;
  }

  size_t param_begin = 0;
  while ( param_begin <= params.size() ) {
    const size_t param_end = params.find( ';', param_begin );
    const size_t this_param_end = param_end == std::string::npos ? params.size() : param_end;
    SGRParam param;

    size_t sub_begin = param_begin;
    while ( sub_begin <= this_param_end ) {
      const size_t sub_end = params.find( ':', sub_begin );
      const size_t this_sub_end = ( sub_end == std::string::npos || sub_end > this_param_end ) ? this_param_end : sub_end;
      int parsed = -1;
      if ( parse_unsigned_param( params, sub_begin, this_sub_end, parsed ) ) {
        param.subparams.push_back( parsed );
      } else {
        param.subparams.push_back( -1 );
      }

      if ( sub_end == std::string::npos || sub_end >= this_param_end ) {
        break;
      }
      sub_begin = sub_end + 1;
    }

    ret.push_back( param );
    if ( param_end == std::string::npos ) {
      break;
    }
    param_begin = param_end + 1;
  }

  return ret;
}

static int SGR_primary_param( const SGRParam& param, const int defaultval )
{
  if ( param.subparams.empty() || param.subparams[0] < 0 ) {
    return defaultval;
  }
  return param.subparams[0];
}

static bool valid_color_component( const int value )
{
  return value >= 0 && value <= 255;
}

static bool SGR_color_from_subparams( const std::vector<int>& subparams, unsigned int& color )
{
  if ( subparams.size() < 2 ) {
    return false;
  }

  if ( subparams[1] == 5 ) {
    if ( subparams.size() < 3 || !valid_color_component( subparams[2] ) ) {
      return false;
    }
    color = subparams[2];
    return true;
  }

  if ( subparams[1] == 2 ) {
    if ( subparams.size() < 5 ) {
      return false;
    }
    const size_t red_index = subparams.size() - 3;
    const int red = subparams[red_index];
    const int green = subparams[red_index + 1];
    const int blue = subparams[red_index + 2];
    if ( !valid_color_component( red ) || !valid_color_component( green ) || !valid_color_component( blue ) ) {
      return false;
    }
    color = Renditions::make_true_color( red, green, blue );
    return true;
  }

  return false;
}

static bool SGR_color_from_semicolon_params( const std::vector<SGRParam>& params,
                                             const size_t loc,
                                             unsigned int& color,
                                             size_t& consumed )
{
  if ( loc + 2 < params.size() && SGR_primary_param( params[loc + 1], -1 ) == 5 ) {
    const int index = SGR_primary_param( params[loc + 2], -1 );
    if ( valid_color_component( index ) ) {
      color = index;
      consumed = 2;
      return true;
    }
  }

  if ( loc + 4 < params.size() && SGR_primary_param( params[loc + 1], -1 ) == 2 ) {
    const int red = SGR_primary_param( params[loc + 2], -1 );
    const int green = SGR_primary_param( params[loc + 3], -1 );
    const int blue = SGR_primary_param( params[loc + 4], -1 );
    if ( valid_color_component( red ) && valid_color_component( green ) && valid_color_component( blue ) ) {
      color = Renditions::make_true_color( red, green, blue );
      consumed = 4;
      return true;
    }
  }

  return false;
}

static void apply_SGR_color( Framebuffer* fb, const int rendition, const unsigned int color )
{
  switch ( rendition ) {
    case 38:
      fb->ds.set_foreground_color( color );
      break;
    case 48:
      fb->ds.set_background_color( color );
      break;
    case 58:
      fb->ds.get_renditions().set_underline_color( color );
      break;
    default:
      break;
  }
}

static void CSI_SGR( Framebuffer* fb, Dispatcher* dispatch )
{
  const std::vector<SGRParam> params = parse_SGR_params( dispatch->get_params_string() );

  for ( size_t i = 0; i < params.size(); i++ ) {
    const int rendition = SGR_primary_param( params[i], 0 );

    if ( rendition == 4 && params[i].subparams.size() > 1 ) {
      const int style = params[i].subparams[1] < 0 ? Renditions::UNDERLINE_SINGLE : params[i].subparams[1];
      fb->ds.get_renditions().set_underline_style( style );
      continue;
    }

    if ( rendition == 38 || rendition == 48 || rendition == 58 ) {
      unsigned int color;
      size_t consumed = 0;
      if ( SGR_color_from_subparams( params[i].subparams, color ) ) {
        apply_SGR_color( fb, rendition, color );
        continue;
      }
      if ( SGR_color_from_semicolon_params( params, i, color, consumed ) ) {
        apply_SGR_color( fb, rendition, color );
        i += consumed;
        continue;
      }
    }

    fb->ds.add_rendition( rendition );
  }
}

static Function func_CSI_SGR( CSI, "m", CSI_SGR, false ); /* changing renditions doesn't clear wrap flag */

/* save and restore cursor */
static void Esc_DECSC( Framebuffer* fb, Dispatcher* dispatch __attribute( ( unused ) ) )
{
  fb->ds.save_cursor();
}

static void Esc_DECRC( Framebuffer* fb, Dispatcher* dispatch __attribute( ( unused ) ) )
{
  fb->ds.restore_cursor();
}

static Function func_Esc_DECSC( ESCAPE, "7", Esc_DECSC );
static Function func_Esc_DECRC( ESCAPE, "8", Esc_DECRC );

/* device status report -- e.g., cursor position (used by resize) */
static void CSI_DSR( Framebuffer* fb, Dispatcher* dispatch )
{
  int param = dispatch->getparam( 0, 0 );

  switch ( param ) {
    case 5: /* device status report requested */
      dispatch->terminal_to_host.append( "\033[0n" );
      break;
    case 6: /* report of active position requested */
      char cpr[32];
      snprintf( cpr, 32, "\033[%d;%dR", fb->ds.get_cursor_row() + 1, fb->ds.get_cursor_col() + 1 );
      dispatch->terminal_to_host.append( cpr );
      break;
    default:
      break;
  }
}

static Function func_CSI_DSR( CSI, "n", CSI_DSR );

/* insert line */
static void CSI_IL( Framebuffer* fb, Dispatcher* dispatch )
{
  int lines = dispatch->getparam( 0, 1 );

  fb->insert_line( fb->ds.get_cursor_row(), lines );

  /* vt220 manual and Ecma-48 say to move to first column */
  /* but xterm and gnome-terminal don't */
  fb->ds.move_col( 0 );
}

static Function func_CSI_IL( CSI, "L", CSI_IL );

/* delete line */
static void CSI_DL( Framebuffer* fb, Dispatcher* dispatch )
{
  int lines = dispatch->getparam( 0, 1 );

  fb->delete_line( fb->ds.get_cursor_row(), lines );

  /* same story -- xterm and gnome-terminal don't
     move to first column */
  fb->ds.move_col( 0 );
}

static Function func_CSI_DL( CSI, "M", CSI_DL );

/* insert characters */
static void CSI_ICH( Framebuffer* fb, Dispatcher* dispatch )
{
  int cells = dispatch->getparam( 0, 1 );

  for ( int i = 0; i < cells; i++ ) {
    fb->insert_cell( fb->ds.get_cursor_row(), fb->ds.get_cursor_col() );
  }
}

static Function func_CSI_ICH( CSI, "@", CSI_ICH );

/* delete character */
static void CSI_DCH( Framebuffer* fb, Dispatcher* dispatch )
{
  int cells = dispatch->getparam( 0, 1 );

  for ( int i = 0; i < cells; i++ ) {
    fb->delete_cell( fb->ds.get_cursor_row(), fb->ds.get_cursor_col() );
  }
}

static Function func_CSI_DCH( CSI, "P", CSI_DCH );

/* line position absolute */
static void CSI_VPA( Framebuffer* fb, Dispatcher* dispatch )
{
  int row = dispatch->getparam( 0, 1 );
  fb->ds.move_row( row - 1 );
}

static Function func_CSI_VPA( CSI, "d", CSI_VPA );

/* character position absolute */
static void CSI_HPA( Framebuffer* fb, Dispatcher* dispatch )
{
  int col = dispatch->getparam( 0, 1 );
  fb->ds.move_col( col - 1 );
}

static Function func_CSI_CHA( CSI, "G", CSI_HPA );    /* ECMA-48 name: CHA */
static Function func_CSI_HPA( CSI, "\x60", CSI_HPA ); /* ECMA-48 name: HPA */

/* erase character */
static void CSI_ECH( Framebuffer* fb, Dispatcher* dispatch )
{
  int num = dispatch->getparam( 0, 1 );
  int limit = fb->ds.get_cursor_col() + num - 1;
  if ( limit >= fb->ds.get_width() ) {
    limit = fb->ds.get_width() - 1;
  }

  clearline( fb, -1, fb->ds.get_cursor_col(), limit );
}

static Function func_CSI_ECH( CSI, "X", CSI_ECH );

/* reset to initial state */
static void Esc_RIS( Framebuffer* fb, Dispatcher* dispatch __attribute( ( unused ) ) )
{
  fb->reset();
}

static Function func_Esc_RIS( ESCAPE, "c", Esc_RIS );

/* soft reset */
static void CSI_DECSTR( Framebuffer* fb, Dispatcher* dispatch __attribute( ( unused ) ) )
{
  fb->soft_reset();
}

static Function func_CSI_DECSTR( CSI, "!p", CSI_DECSTR );

/* set cursor style -- DECSCUSR */
static void CSI_DECSCUSR( Framebuffer* fb, Dispatcher* dispatch )
{
  fb->ds.set_cursor_style( dispatch->getparam( 0, 0 ) );
}

static Function func_CSI_DECSCUSR( CSI, " q", CSI_DECSCUSR, false );

static bool parse_printable_ascii( const std::vector<wchar_t>& chars, std::string& str )
{
  str.reserve( chars.size() );
  for ( wchar_t wide_char : chars ) {
    if ( wide_char < 32 || wide_char > 126 ) {
      return false;
    }
    str.append( 1, static_cast<char>( wide_char ) );
  }
  return true;
}

static bool parse_8bit_bytes( const std::vector<wchar_t>& chars, std::string& str )
{
  str.reserve( chars.size() );
  for ( wchar_t wide_char : chars ) {
    if ( static_cast<unsigned long>( wide_char ) > 255 ) {
      return false;
    }
    str.append( 1, static_cast<char>( wide_char ) );
  }
  return true;
}

static bool starts_with( const std::string& str, const std::string& prefix )
{
  return str.compare( 0, prefix.size(), prefix ) == 0;
}

static std::string CSI_payload( const std::string& csi )
{
  if ( csi.size() >= 2 && csi[0] == '\033' && csi[1] == '[' ) {
    return csi.substr( 2 );
  }
  return csi;
}

static void DCS_DECRQSS( Framebuffer* fb, Dispatcher* dispatch )
{
  std::string request;
  if ( !parse_printable_ascii( dispatch->get_DCS_string(), request ) ) {
    dispatch->terminal_to_host.append( "\033P0$r\033\\" );
    return;
  }

  std::string response;
  if ( request == "m" ) {
    response = CSI_payload( fb->ds.get_renditions().sgr() );
  } else if ( request == " q" ) {
    char cursor_style[32];
    snprintf( cursor_style, sizeof( cursor_style ), "%d q", fb->ds.cursor_style_param() );
    response = cursor_style;
  } else if ( request == ">4m" ) {
    char modify_other_keys[32];
    snprintf( modify_other_keys, sizeof( modify_other_keys ), ">4;%dm", fb->ds.modify_other_keys );
    response = modify_other_keys;
  } else if ( request == "r" ) {
    char margins[32];
    snprintf( margins,
              sizeof( margins ),
              "%d;%dr",
              fb->ds.get_scrolling_region_top_row() + 1,
              fb->ds.get_scrolling_region_bottom_row() + 1 );
    response = margins;
  } else {
    dispatch->terminal_to_host.append( "\033P0$r\033\\" );
    return;
  }

  dispatch->terminal_to_host.append( "\033P1$r" );
  dispatch->terminal_to_host.append( response );
  dispatch->terminal_to_host.append( "\033\\" );
}

static int hex_value( const char ch )
{
  if ( ch >= '0' && ch <= '9' ) {
    return ch - '0';
  }
  if ( ch >= 'a' && ch <= 'f' ) {
    return ch - 'a' + 10;
  }
  if ( ch >= 'A' && ch <= 'F' ) {
    return ch - 'A' + 10;
  }
  return -1;
}

static bool hex_decode( const std::string& hex, std::string& decoded )
{
  if ( hex.size() % 2 != 0 ) {
    return false;
  }

  decoded.clear();
  decoded.reserve( hex.size() / 2 );
  for ( size_t i = 0; i < hex.size(); i += 2 ) {
    const int high = hex_value( hex[i] );
    const int low = hex_value( hex[i + 1] );
    if ( high < 0 || low < 0 ) {
      return false;
    }
    decoded.push_back( static_cast<char>( ( high << 4 ) | low ) );
  }
  return true;
}

static std::string hex_encode( const std::string& str )
{
  static const char hex_digits[] = "0123456789abcdef";
  std::string ret;
  ret.reserve( str.size() * 2 );
  for ( unsigned char ch : str ) {
    ret.push_back( hex_digits[ch >> 4] );
    ret.push_back( hex_digits[ch & 0xf] );
  }
  return ret;
}

static bool XTGETTCAP_value( const std::string& name, std::string& value )
{
  if ( name == "TN" ) {
    value = "xterm-256color";
  } else if ( name == "Co" || name == "colors" ) {
    value = "256";
  } else if ( name == "RGB" ) {
    value = "8/8/8";
  } else if ( name == "Tc" ) {
    value = "1";
  } else if ( name == "Ms" ) {
    value = "\033]52;%p1%s;%p2%s\007";
  } else if ( name == "Ss" ) {
    value = "\033[%p1%d q";
  } else if ( name == "Se" ) {
    value = "\033[2 q";
  } else if ( name == "Cs" ) {
    value = "\033]12;%p1%s\007";
  } else if ( name == "Cr" ) {
    value = "\033]112\007";
  } else if ( name == "u6" ) {
    value = "\033[%i%d;%dR";
  } else if ( name == "u7" ) {
    value = "\033[6n";
  } else if ( name == "u8" ) {
    value = "\033[?62c";
  } else {
    return false;
  }
  return true;
}

static void DCS_XTGETTCAP( Dispatcher* dispatch )
{
  std::string request;
  if ( !parse_printable_ascii( dispatch->get_DCS_string(), request ) ) {
    dispatch->terminal_to_host.append( "\033P0+r\033\\" );
    return;
  }

  std::string response;
  size_t loc = 0;
  bool first = true;
  while ( loc <= request.size() ) {
    const size_t end = request.find( ';', loc );
    const std::string encoded_name = request.substr( loc, ( end == std::string::npos ? request.size() : end ) - loc );
    std::string name;
    std::string value;
    if ( encoded_name.empty() || !hex_decode( encoded_name, name ) || !XTGETTCAP_value( name, value ) ) {
      dispatch->terminal_to_host.append( "\033P0+r\033\\" );
      return;
    }

    if ( !first ) {
      response.push_back( ';' );
    }
    first = false;
    response.append( encoded_name );
    response.push_back( '=' );
    response.append( hex_encode( value ) );

    if ( end == std::string::npos ) {
      break;
    }
    loc = end + 1;
  }

  dispatch->terminal_to_host.append( "\033P1+r" );
  dispatch->terminal_to_host.append( response );
  dispatch->terminal_to_host.append( "\033\\" );
}

static bool DCS_sixel_passthrough( Framebuffer* fb, Dispatcher* dispatch )
{
  if ( dispatch->get_DCS_string_truncated() || dispatch->get_dispatch_chars() != "q" ) {
    return false;
  }

  std::string payload;
  if ( !parse_8bit_bytes( dispatch->get_DCS_string(), payload ) ) {
    return false;
  }

  std::string sequence = "\033P";
  sequence.append( dispatch->get_params_string_raw() );
  sequence.append( "q" );
  sequence.append( payload );
  sequence.append( "\033\\" );
  fb->push_passthrough_sequence( sequence );
  return true;
}

void Dispatcher::DCS_dispatch( const Parser::Unhook* act __attribute( ( unused ) ), Framebuffer* fb )
{
  if ( dispatch_chars == "$q" ) {
    DCS_DECRQSS( fb, this );
  } else if ( dispatch_chars == "+q" ) {
    DCS_XTGETTCAP( this );
  } else {
    DCS_sixel_passthrough( fb, this );
  }
}

static bool comma_arg_value( const std::string& args, const std::string& name, std::string& value )
{
  size_t loc = 0;
  while ( loc <= args.size() ) {
    const size_t end = args.find( ',', loc );
    const std::string arg = args.substr( loc, ( end == std::string::npos ? args.size() : end ) - loc );
    const size_t equals = arg.find( '=' );
    if ( equals != std::string::npos && arg.substr( 0, equals ) == name ) {
      value = arg.substr( equals + 1 );
      return true;
    }
    if ( end == std::string::npos ) {
      break;
    }
    loc = end + 1;
  }
  return false;
}

static bool comma_arg_nonnegative_int( const std::string& args, const std::string& name, int& value )
{
  std::string str;
  if ( !comma_arg_value( args, name, str ) || str.empty() ) {
    return false;
  }

  int parsed = 0;
  for ( std::string::const_iterator it = str.begin(); it != str.end(); ++it ) {
    if ( *it < '0' || *it > '9' ) {
      return false;
    }
    const int digit = *it - '0';
    if ( parsed > ( Dispatcher::PARAM_MAX - digit ) / 10 ) {
      return false;
    }
    parsed = parsed * 10 + digit;
  }

  value = parsed;
  return true;
}

static void kitty_graphics_control_data( const std::string& apc, std::string& control_data )
{
  const size_t separator = apc.find( ';', 1 );
  control_data = apc.substr( 1, separator == std::string::npos ? std::string::npos : separator - 1 );
}

static bool kitty_graphics_should_passthrough( const std::string& apc, std::string& control_data )
{
  if ( !starts_with( apc, "G" ) ) {
    return false;
  }

  kitty_graphics_control_data( apc, control_data );
  std::string transmission_medium;
  if ( comma_arg_value( control_data, "t", transmission_medium ) && transmission_medium != "d" ) {
    /* Do not let a remote host ask the local terminal to read or delete local
       files/shared memory.  Direct inline payloads are the safe transport for
       mosh. */
    return false;
  }
  return true;
}

static bool kitty_graphics_command_moves_cursor( const std::string& control_data, int& cols, int& rows )
{
  std::string action;
  if ( !comma_arg_value( control_data, "a", action ) ) {
    action = "t";
  }

  if ( action != "T" && action != "p" ) {
    return false;
  }

  std::string cursor_policy;
  if ( comma_arg_value( control_data, "C", cursor_policy ) && cursor_policy == "1" ) {
    return false;
  }

  cols = 0;
  rows = 0;
  comma_arg_nonnegative_int( control_data, "c", cols );
  comma_arg_nonnegative_int( control_data, "r", rows );
  return cols > 0 || rows > 0;
}

static void kitty_graphics_move_cursor( Framebuffer* fb, const int cols, const int rows )
{
  /* Match Ghostty/the kitty spec: move down r full rows (scrolling at
     the bottom, exactly like r index operations), then set the column
     to anchor_col + c clamped to the last column (no wrap). */
  const int anchor_col = fb->ds.get_cursor_col();

  if ( rows > 0 ) {
    fb->move_rows_autoscroll( rows );
  }

  int target_col = anchor_col + cols;
  const int max_col = fb->ds.get_width() - 1;
  if ( target_col > max_col ) {
    target_col = max_col;
  }
  fb->ds.move_col( target_col, false, false );
}

void Dispatcher::APC_dispatch( const Parser::APC_End* act __attribute( ( unused ) ), Framebuffer* fb )
{
  if ( get_APC_string_truncated() ) {
    end_kitty_graphics_chunk();
    return;
  }

  std::string apc;
  if ( !parse_printable_ascii( get_APC_string(), apc ) ) {
    end_kitty_graphics_chunk();
    return;
  }

  std::string control_data;
  if ( kitty_graphics_should_passthrough( apc, control_data ) ) {
    std::string action;
    comma_arg_value( control_data, "a", action );
    if ( action == "d" ) {
      /* Keep the delete event for attached clients, but do not retain older
         placements that it invalidates for future full-frame replays. */
      fb->clear_graphics_passthrough_sequences();
    }

    std::string sequence = std::string( "\033_" ) + apc + "\033\\";
    if ( action == "T" || action == "p" ) {
      /* Force C=1 on the forwarded/stored placement bytes so the host
         terminal never moves its cursor or scrolls as a side effect --
         mosh's differ owns all positioning.  The model's own
         cursor-movement decision below still uses the original control
         data (so an app sending C=0 still moves mosh's cursor). */
      force_kitty_graphics_cursor_policy( sequence );
    }
    /* a=q query commands are forwarded live but must not be replayed on
       a full-frame repaint, or the app gets duplicate query responses. */
    fb->push_passthrough_sequence( sequence, action != "q" );

    int more_chunks = -1;
    comma_arg_nonnegative_int( control_data, "m", more_chunks );

    if ( more_chunks == 1 ) {
      if ( !get_kitty_graphics_chunk_in_progress() ) {
        int cols = 0;
        int rows = 0;
        const bool moves_cursor = kitty_graphics_command_moves_cursor( control_data, cols, rows );
        start_kitty_graphics_chunk( moves_cursor, cols, rows );
      }
    } else if ( more_chunks == 0 ) {
      if ( get_kitty_graphics_chunk_in_progress() ) {
        if ( get_kitty_graphics_chunk_moves_cursor() ) {
          kitty_graphics_move_cursor( fb, get_kitty_graphics_chunk_cols(), get_kitty_graphics_chunk_rows() );
        }
        end_kitty_graphics_chunk();
      } else {
        int cols = 0;
        int rows = 0;
        if ( kitty_graphics_command_moves_cursor( control_data, cols, rows ) ) {
          kitty_graphics_move_cursor( fb, cols, rows );
        }
      }
    } else {
      int cols = 0;
      int rows = 0;
      if ( kitty_graphics_command_moves_cursor( control_data, cols, rows ) ) {
        kitty_graphics_move_cursor( fb, cols, rows );
      }
      end_kitty_graphics_chunk();
    }
  } else {
    end_kitty_graphics_chunk();
  }
}

static void OSC_8( const std::string& OSC_string, Framebuffer* fb )
{
  // OSC of the form "\033]8;params;url\007"
  assert( OSC_string[0] == '8' );
  if ( OSC_string.size() <= 2 || OSC_string[1] != ';' ) {
    // Bail early if the string is malformed.
    return;
  }

  size_t second_semicolon = OSC_string.find_first_of( ';', 2 );
  if ( second_semicolon == std::string::npos ) {
    // Missing the second semicolon, malformed.
    return;
  }

  std::string url = OSC_string.substr( second_semicolon + 1 );
  fb->ds.set_hyperlink( Hyperlink( OSC_string.substr( 2, second_semicolon - 2 ), std::move( url ) ) );
}

static bool OSC_color_query( const std::vector<wchar_t>& OSC_string, Dispatcher* dispatch, const Framebuffer* fb )
{
  long cmd_num = 0;
  size_t loc = 0;

  while ( loc < OSC_string.size() && OSC_string[loc] >= L'0' && OSC_string[loc] <= L'9' ) {
    cmd_num = cmd_num * 10 + ( OSC_string[loc] - L'0' );
    if ( cmd_num > Dispatcher::PARAM_MAX ) {
      return false;
    }
    loc++;
  }

  if ( loc == 0 || loc >= OSC_string.size() || OSC_string[loc] != L';' ) {
    return false;
  }

  if ( loc + 2 != OSC_string.size() || OSC_string[loc + 1] != L'?' ) {
    return false;
  }

  if ( cmd_num != 10 && cmd_num != 11 && cmd_num != 12 ) {
    return false;
  }

  std::string color;
  if ( cmd_num == 12 && fb != NULL ) {
    color = fb->ds.cursor_color;
  }
  if ( color.empty() ) {
    color = dispatch->get_OSC_color_response( cmd_num );
  }
  if ( color.empty() ) {
    return true;
  }

  char response[16];
  snprintf( response, sizeof response, "\033]%ld;", cmd_num );
  dispatch->terminal_to_host.append( response );
  dispatch->terminal_to_host.append( color );
  dispatch->terminal_to_host.append( "\033\\" );
  return true;
}

static bool contains_space( const std::string& str )
{
  return str.find( ' ' ) != std::string::npos;
}

static bool all_digits( const std::string& str )
{
  if ( str.empty() ) {
    return false;
  }
  for ( std::string::const_iterator it = str.begin(); it != str.end(); ++it ) {
    if ( *it < '0' || *it > '9' ) {
      return false;
    }
  }
  return true;
}

static bool safe_file_url( const std::string& url )
{
  if ( url.size() > 4096 || !starts_with( url, "file://" ) || contains_space( url ) ) {
    return false;
  }

  const size_t authority_end = url.find( '/', 7 );
  const size_t at = url.find( '@', 7 );
  return at == std::string::npos || ( authority_end != std::string::npos && at > authority_end );
}

static bool semicolon_arg_is( const std::string& args, const std::string& name, const std::string& value )
{
  size_t loc = 0;
  while ( loc <= args.size() ) {
    const size_t end = args.find( ';', loc );
    const std::string arg = args.substr( loc, ( end == std::string::npos ? args.size() : end ) - loc );
    const size_t equals = arg.find( '=' );
    if ( equals != std::string::npos && arg.substr( 0, equals ) == name && arg.substr( equals + 1 ) == value ) {
      return true;
    }
    if ( end == std::string::npos ) {
      break;
    }
    loc = end + 1;
  }
  return false;
}

static bool iterm2_file_args_inline( const std::string& args )
{
  return semicolon_arg_is( args, "inline", "1" );
}

static bool OSC_1337_should_passthrough( const std::string& osc, Dispatcher* dispatch )
{
  const std::string body = osc.substr( 5 ); /* after "1337;" */

  if ( starts_with( body, "CurrentDir=" ) ) {
    return body.size() <= 4107;
  }

  if ( body == "SetMark" ) {
    return true;
  }

  if ( starts_with( body, "File=" ) ) {
    const size_t colon = body.find( ':' );
    if ( colon == std::string::npos ) {
      return false;
    }
    const bool pass = iterm2_file_args_inline( body.substr( 5, colon - 5 ) );
    dispatch->set_iterm2_inline_file_in_progress( false );
    return pass;
  }

  if ( starts_with( body, "MultipartFile=" ) ) {
    const bool pass = iterm2_file_args_inline( body.substr( 14 ) );
    dispatch->set_iterm2_inline_file_in_progress( pass );
    return pass;
  }

  if ( starts_with( body, "FilePart=" ) ) {
    return dispatch->get_iterm2_inline_file_in_progress();
  }

  if ( body == "FileEnd" ) {
    const bool pass = dispatch->get_iterm2_inline_file_in_progress();
    dispatch->set_iterm2_inline_file_in_progress( false );
    return pass;
  }

  dispatch->set_iterm2_inline_file_in_progress( false );
  return false;
}

static bool OSC_133_mark_should_passthrough( const std::string& body )
{
  if ( body.empty() ) {
    return false;
  }

  const char mark = body[0];
  if ( mark == 'A' || mark == 'B' || mark == 'C' ) {
    return body.size() == 1 || body[1] == ';';
  }
  if ( mark == 'D' ) {
    return body.size() == 1 || ( body[1] == ';' && all_digits( body.substr( 2 ) ) );
  }
  return false;
}

static bool OSC_633_should_passthrough( const std::string& osc )
{
  const std::string body = osc.substr( 4 ); /* after "633;" */
  if ( OSC_133_mark_should_passthrough( body ) ) {
    return true;
  }

  if ( starts_with( body, "P;Cwd=" ) ) {
    return body.size() <= 4102;
  }

  return false;
}

static bool OSC_should_passthrough( const std::string& osc, Dispatcher* dispatch )
{
  if ( starts_with( osc, "7;" ) ) { /* current-directory URL */
    return safe_file_url( osc.substr( 2 ) );
  }

  if ( starts_with( osc, "133;" ) ) { /* FinalTerm shell marks */
    return OSC_133_mark_should_passthrough( osc.substr( 4 ) );
  }

  if ( starts_with( osc, "633;" ) ) { /* VS Code shell integration */
    return OSC_633_should_passthrough( osc );
  }

  if ( starts_with( osc, "1337;" ) ) { /* iTerm2 shell integration / inline images */
    return OSC_1337_should_passthrough( osc, dispatch );
  }

  return false;
}

/* xterm uses an Operating System Command to set the window title */
void Dispatcher::OSC_dispatch( const Parser::OSC_End* act __attribute( ( unused ) ), Framebuffer* fb )
{
  if ( OSC_color_query( OSC_string, this, fb ) ) {
    return;
  }

  std::string OSC_ascii;
  if ( parse_printable_ascii( OSC_string, OSC_ascii ) ) {
    if ( !get_OSC_string_truncated() && OSC_should_passthrough( OSC_ascii, this ) ) {
      fb->push_passthrough_sequence( std::string( "\033]" ) + OSC_ascii + "\007" );
      return;
    }

    if ( OSC_ascii == "112" || OSC_ascii == "112;" ) {
      fb->ds.cursor_color.clear();
      return;
    }
    if ( OSC_ascii.compare( 0, 3, "12;" ) == 0 ) {
      const std::string cursor_color = OSC_ascii.substr( 3 );
      if ( !cursor_color.empty() && cursor_color != "?" ) {
        fb->ds.cursor_color = cursor_color;
      }
      return;
    }
  }

  /* handle osc copy clipboard sequence 52;c; */
  if ( OSC_string.size() >= 5 && OSC_string[0] == L'5' && OSC_string[1] == L'2' && OSC_string[2] == L';'
       && OSC_string[3] == L'c' && OSC_string[4] == L';' ) {
    Terminal::Framebuffer::title_type clipboard( OSC_string.begin() + 5, OSC_string.end() );
    fb->set_clipboard( clipboard );
    /* handle osc terminal title sequence */
  } else if ( OSC_string.size() >= 1 ) {
    long cmd_num = -1;
    int offset = 0;
    if ( OSC_string[0] == L';' ) {
      /* OSC of the form "\033];<title>\007" */
      cmd_num = 0; /* treat it as as a zero */
      offset = 1;
    } else if ( ( OSC_string.size() >= 2 ) && ( OSC_string[1] == L';' ) ) {
      /* OSC of the form "\033]X;<title>\007" where X can be:
       * 0: set icon name and window title
       * 1: set icon name
       * 2: set window title */
      cmd_num = OSC_string[0] - L'0';
      offset = 2;
    }
    if ( cmd_num == 8 ) {
      // Handle OSC8 hyperlinks separately
      std::string osc_8_str;
      if ( !parse_printable_ascii( OSC_string, osc_8_str ) ) {
        //
        return;
      }
      OSC_8( osc_8_str, fb );
      return;
    }
    bool set_icon = cmd_num == 0 || cmd_num == 1;
    bool set_title = cmd_num == 0 || cmd_num == 2;
    if ( set_icon || set_title ) {
      fb->set_title_initialized();
      int title_length = std::min( OSC_string.size(), (size_t)256 );
      Terminal::Framebuffer::title_type newtitle( OSC_string.begin() + offset, OSC_string.begin() + title_length );
      if ( set_icon ) {
        fb->set_icon_name( newtitle );
      }
      if ( set_title ) {
        fb->set_window_title( newtitle );
      }
    }
  }
}

/* scroll down or terminfo indn */
static void CSI_SD( Framebuffer* fb, Dispatcher* dispatch )
{
  fb->scroll( dispatch->getparam( 0, 1 ) );
}

static Function func_CSI_SD( CSI, "S", CSI_SD );

/* scroll up or terminfo rin */
static void CSI_SU( Framebuffer* fb, Dispatcher* dispatch )
{
  fb->scroll( -dispatch->getparam( 0, 1 ) );
}

static Function func_CSI_SU( CSI, "T", CSI_SU );
