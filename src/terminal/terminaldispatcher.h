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

#ifndef TERMINALDISPATCHER_HPP
#define TERMINALDISPATCHER_HPP

#include <map>
#include <string>
#include <vector>

namespace Parser {
class Action;
class Param;
class Collect;
class Clear;
class Esc_Dispatch;
class CSI_Dispatch;
class Execute;
class Hook;
class Put;
class Unhook;
class APC_Start;
class APC_Put;
class APC_End;
class OSC_Start;
class OSC_Put;
class OSC_End;
}

namespace Terminal {
class Framebuffer;
class Dispatcher;

enum Function_Type
{
  ESCAPE,
  CSI,
  CONTROL
};

class Function
{
public:
  Function() : function( NULL ), clears_wrap_state( true ) {}
  Function( Function_Type type,
            const std::string& dispatch_chars,
            void ( *s_function )( Framebuffer*, Dispatcher* ),
            bool s_clears_wrap_state = true );
  void ( *function )( Framebuffer*, Dispatcher* );
  bool clears_wrap_state;
};

using dispatch_map_t = std::map<std::string, Function>;

class DispatchRegistry
{
public:
  dispatch_map_t escape;
  dispatch_map_t CSI;
  dispatch_map_t control;

  DispatchRegistry() : escape(), CSI(), control() {}
};

DispatchRegistry& get_global_dispatch_registry( void );

class Dispatcher
{
private:
  std::string params;
  std::vector<int> parsed_params;
  bool parsed;

  std::string dispatch_chars;
  std::vector<wchar_t> DCS_string;
  std::vector<wchar_t> APC_string;
  std::vector<wchar_t> OSC_string;
  bool DCS_string_truncated;
  bool APC_string_truncated;
  bool OSC_string_truncated;
  bool iterm2_inline_file_in_progress;
  std::map<int, std::string> osc_color_responses;

  void parse_params( void );

public:
  static const int PARAM_MAX = 65535;
  /* prevent evil escape sequences from causing long loops */

  std::string terminal_to_host; /* this is the reply string */

  Dispatcher();
  int getparam( size_t N, int defaultval );
  int param_count( void );

  void newparamchar( const Parser::Param* act );
  void collect( const Parser::Collect* act );
  void clear( const Parser::Clear* act );

  std::string str( void );

  void dispatch( Function_Type type, const Parser::Action* act, Framebuffer* fb );
  std::string get_dispatch_chars( void ) const { return dispatch_chars; }
  const std::string& get_params_string( void ) const { return params; }
  const std::string& get_params_string_raw( void ) const { return params; }
  std::vector<wchar_t> get_DCS_string( void ) const { return DCS_string; }
  std::vector<wchar_t> get_APC_string( void ) const { return APC_string; }
  std::vector<wchar_t> get_OSC_string( void ) const { return OSC_string; }
  bool get_DCS_string_truncated( void ) const { return DCS_string_truncated; }
  bool get_APC_string_truncated( void ) const { return APC_string_truncated; }
  bool get_OSC_string_truncated( void ) const { return OSC_string_truncated; }
  bool get_iterm2_inline_file_in_progress( void ) const { return iterm2_inline_file_in_progress; }
  void set_iterm2_inline_file_in_progress( bool in_progress )
  {
    iterm2_inline_file_in_progress = in_progress;
  }
  void set_OSC_color_response( int osc_number, const std::string& color );
  std::string get_OSC_color_response( int osc_number ) const;

  void DCS_hook( const Parser::Hook* act );
  void DCS_put( const Parser::Put* act );
  void DCS_dispatch( const Parser::Unhook* act, Framebuffer* fb );

  void APC_put( const Parser::APC_Put* act );
  void APC_start( const Parser::APC_Start* act );
  void APC_dispatch( const Parser::APC_End* act, Framebuffer* fb );

  void OSC_put( const Parser::OSC_Put* act );
  void OSC_start( const Parser::OSC_Start* act );
  void OSC_dispatch( const Parser::OSC_End* act, Framebuffer* fb );

  bool operator==( const Dispatcher& x ) const;
};
}

#endif
