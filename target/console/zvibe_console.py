#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""
Python console for zvibe Z-machine interpreter using ctypes
Supports both minimal mode (default) and enhanced features (opt-in)
"""

import ctypes
import sys
import os
import argparse
import shutil
from ctypes import c_char_p, c_size_t, c_int, c_void_p, CFUNCTYPE, POINTER

# Define callback function types matching your C signatures
OUTPUT_FUNC = CFUNCTYPE(None, c_char_p, c_size_t)

# Constants - let's verify these match your zvibe_api.h exactly
ZVIBE_OK = 0
ZVIBE_ERROR = 1  
ZVIBE_WAIT_FOR_INPUT = 2
ZVIBE_GAME_FINISHED = 3
ZVIBE_SAVE_REQUESTED = 4
ZVIBE_RESTORE_REQUESTED = 5  
ZVIBE_RESTART_REQUESTED = 6

class ZVibeStatus(ctypes.Structure):
    _fields_ = [
        ("location", ctypes.c_char * 80),
        ("status", ctypes.c_char * 80),
        ("is_time", c_int),
        ("score", c_int),
        ("turns", c_int),
        ("hours", c_int),
        ("minutes", c_int)
    ]

# Status line support (define after ZVibeStatus)
STATUS_FUNC = CFUNCTYPE(None, POINTER(ZVibeStatus))

class ZVibeConsole:
    def __init__(self, library_path, enable_status=False, enable_save=True, script_file=None):
        """Initialize with path to your compiled zvibe library"""
        self.lib = ctypes.CDLL(library_path)
        self.ctx = None
        self.current_input = None
        self.enable_status = enable_status
        self.enable_save = enable_save
        self.script_file = script_file
        self.script_commands = []
        self.script_index = 0
        self.save_file_path = None
        self.terminal_width = 80
        self.status_line = ""
        
        # Define function signatures to match your zvibe_api.h
        self.lib.zvibe_create.argtypes = [OUTPUT_FUNC]
        self.lib.zvibe_create.restype = c_void_p
        
        self.lib.zvibe_load_story.argtypes = [c_void_p, c_char_p]
        self.lib.zvibe_load_story.restype = c_int
        
        self.lib.zvibe_run.argtypes = [c_void_p]
        self.lib.zvibe_run.restype = c_int
        
        self.lib.zvibe_input.argtypes = [c_void_p, c_char_p]
        self.lib.zvibe_input.restype = None
        
        self.lib.zvibe_destroy.argtypes = [c_void_p]
        self.lib.zvibe_destroy.restype = None
        
        # Create callback function
        self.output_callback = OUTPUT_FUNC(self._output_handler)
        
        # Check for enhanced features and set up optional functionality
        self._setup_optional_features()
        
        # Load script file if provided
        if self.script_file:
            self._load_script_file()
        
        # Terminal initialization will happen when the game starts
        
        mode = "standard"
        if self.enable_status:
            mode = f"enhanced (status display)"
        elif not self.enable_save:
            mode = "minimal (no save/restore)"
        
        print(f"ZVibe Python Console initialized - {mode} mode")
    
    def _output_handler(self, text, length):
        """Handle output from the game"""
        try:
            output = ctypes.string_at(text, length).decode('utf-8', errors='replace')
            print(output, end='', flush=True)
            
            # Status line redrawing currently disabled due to display issues
            # if self.enable_status and sys.stdout.isatty() and output.endswith(('\n', '.', '!', '?', ':')):
            #     self._draw_status_line()
        except Exception as e:
            print(f"[Output handler error: {e}]", file=sys.stderr)
    
    def create_context(self):
        """Create zvibe context"""
        self.ctx = self.lib.zvibe_create(self.output_callback)
        if not self.ctx:
            raise RuntimeError("Failed to create zvibe context")
        
        # Set up enhanced features if enabled
        if self.enable_status and hasattr(self, 'status_callback'):
            result = self.lib.zvibe_set_status_callback(self.ctx, self.status_callback)
            if result != ZVIBE_OK:
                print("[WARNING] Failed to set status callback")
        
    def load_story(self, story_path):
        """Load a Z3 story file"""
        if not self.ctx:
            raise RuntimeError("Context not created")
        
        if not os.path.exists(story_path):
            raise FileNotFoundError(f"Story file not found: {story_path}")
        
        # Set up save file path based on story file
        if self.enable_save:
            story_dir = os.path.dirname(os.path.abspath(story_path))
            story_name = os.path.splitext(os.path.basename(story_path))[0]
            self.save_file_path = os.path.join(story_dir, f"{story_name}.save")
        
        result = self.lib.zvibe_load_story(self.ctx, story_path.encode('utf-8'))
        if result != ZVIBE_OK:
            raise RuntimeError(f"Failed to load story: {result}")
    
    def run_game(self):
        """Main game loop with enhanced features"""
        if not self.ctx:
            raise RuntimeError("Context not created or story not loaded")
        
        mode_desc = "script" if self.script_file else "interactive"
        print(f"Starting game in {mode_desc} mode... (Press Ctrl+C to quit)\n")
        
        # Status window currently disabled due to display issues
        # if self.enable_status and not self.script_file and sys.stdout.isatty():
        #     self._init_terminal()
        
        try:
            while True:
                result = self.lib.zvibe_run(self.ctx)
                
                if result == ZVIBE_WAIT_FOR_INPUT:
                    # Get input from user or script
                    try:
                        if self.script_file:
                            # Get next command from script
                            user_input = self._get_next_script_command()
                            if user_input is None:
                                print("\n[Script completed]")
                                break
                        else:
                            # Get input from user
                            sys.stdout.flush()
                            user_input = input()
                        
                        # Send input to the game
                        self.lib.zvibe_input(self.ctx, user_input.encode('utf-8'))
                        
                    except EOFError:
                        break
                
                elif result == ZVIBE_GAME_FINISHED:
                    print("\n[Game finished]")
                    break
                
                # Handle enhanced features if enabled
                elif self.enable_save and result == ZVIBE_SAVE_REQUESTED:
                    self._handle_save_request()
                    
                elif self.enable_save and result == ZVIBE_RESTORE_REQUESTED:
                    self._handle_restore_request()
                    
                elif result == ZVIBE_RESTART_REQUESTED:  # if enabled
                    print("\n[Game restarted]")
                    self.lib.zvibe_restart_completed(self.ctx)
                    
                else:
                    print(f"\n[Game error: {result}]")
                    break
                    
        except KeyboardInterrupt:
            print("\n[Game interrupted by user]")
        except Exception as e:
            print(f"\n[Error during game: {e}]")
    
    def _handle_save_request(self):
        """Handle save game request - file-based only"""
        try:
            save_size = self.lib.zvibe_get_save_size(self.ctx)
            if save_size > 0 and save_size <= 65536:  # Reasonable size limit
                # Create a buffer for the save data
                save_buffer = ctypes.create_string_buffer(save_size)
                actual_size = self.lib.zvibe_get_save_data(self.ctx, save_buffer, save_size)
                if actual_size > 0:
                    # Hand the serialized save image to the frontend persistence hook.
                    success = self.save_callback(save_buffer, actual_size)
                    self.lib.zvibe_save_completed(self.ctx, success)
                else:
                    print("\n[Save failed - no data]")
                    self.lib.zvibe_save_completed(self.ctx, 0)  # Failure
            else:
                print("\n[Save failed - data too large]")
                self.lib.zvibe_save_completed(self.ctx, 0)  # Failure
        except Exception as e:
            print(f"\n[Save error: {e}]")
            self.lib.zvibe_save_completed(self.ctx, 0)  # Failure
    
    def _handle_restore_request(self):
        """Handle restore game request - file-based only"""
        try:
            # Check if save file exists
            if not self.save_file_path or not os.path.exists(self.save_file_path):
                print("\n[No save file found]")
                self.lib.zvibe_restore_completed(self.ctx, 0)  # Failure
                return
            
            # Read save file to get size
            with open(self.save_file_path, 'rb') as f:
                data = f.read()
            
            save_size = len(data)
            if save_size > 0:
                # Create a buffer for the restore data
                restore_buffer = ctypes.create_string_buffer(save_size)
                # Let the frontend load save data into the restore buffer.
                actual_size = self.restore_callback(restore_buffer, save_size)
                if actual_size > 0:
                    result = self.lib.zvibe_restore_data(self.ctx, restore_buffer, actual_size)
                    self.lib.zvibe_restore_completed(self.ctx, result == ZVIBE_OK)
                else:
                    print("\n[Restore failed - no data]")
                    self.lib.zvibe_restore_completed(self.ctx, 0)  # Failure
            else:
                print("\n[Save file is empty]")
                self.lib.zvibe_restore_completed(self.ctx, 0)  # Failure
        except Exception as e:
            print(f"\n[Restore error: {e}]")
            self.lib.zvibe_restore_completed(self.ctx, 0)  # Failure
    
    def cleanup(self):
        """Clean up resources"""
        if self.enable_status:
            self._cleanup_terminal()
        
        if self.ctx:
            self.lib.zvibe_destroy(self.ctx)
            self.ctx = None
    
    def _setup_optional_features(self):
        """Set up optional features based on library capabilities"""
        if self.enable_save:
            # Check if save/restore functions exist in library
            try:
                self.lib.zvibe_save_completed.argtypes = [c_void_p, c_int]
                self.lib.zvibe_save_completed.restype = c_int
                self.lib.zvibe_restore_completed.argtypes = [c_void_p, c_int]
                self.lib.zvibe_restore_completed.restype = c_int
                self.lib.zvibe_get_save_size.argtypes = [c_void_p]
                self.lib.zvibe_get_save_size.restype = c_size_t
                self.lib.zvibe_get_save_data.argtypes = [c_void_p, c_void_p, c_size_t]
                self.lib.zvibe_get_save_data.restype = c_size_t
                self.lib.zvibe_restore_data.argtypes = [c_void_p, c_void_p, c_size_t]
                self.lib.zvibe_restore_data.restype = c_int
                self.lib.zvibe_restart_completed.argtypes = [c_void_p]
                self.lib.zvibe_restart_completed.restype = c_int
                
                # Save/restore functionality enabled
            except AttributeError:
                print("[WARNING] Save/restore requested but not available in library")
                self.enable_save = False
        
        if self.enable_status:
            # Check if status functions exist in library
            try:
                self.lib.zvibe_set_status_callback.argtypes = [c_void_p, STATUS_FUNC]
                self.lib.zvibe_set_status_callback.restype = c_int
                self.lib.zvibe_get_status.argtypes = [c_void_p, POINTER(ZVibeStatus)]
                self.lib.zvibe_get_status.restype = c_int
                
                # Create status callback
                self.status_callback = STATUS_FUNC(self._status_handler)
                # Status line functionality enabled
            except AttributeError:
                print("[WARNING] Status line requested but not available in library")
                self.enable_status = False
    
    def _load_script_file(self):
        """Load commands from script file"""
        try:
            with open(self.script_file, 'r') as f:
                lines = f.readlines()
            
            # Filter out empty lines and comments
            for line in lines:
                line = line.strip()
                if line and not line.startswith('#'):
                    self.script_commands.append(line)
            
            # Script commands loaded
        except FileNotFoundError:
            print(f"[ERROR] Script file not found: {self.script_file}")
            sys.exit(1)
        except Exception as e:
            print(f"[ERROR] Failed to load script file: {e}")
            sys.exit(1)
    
    def _get_next_script_command(self):
        """Get next command from script"""
        if self.script_index < len(self.script_commands):
            command = self.script_commands[self.script_index]
            self.script_index += 1
            # Execute script command
            return command
        return None
    
    def _init_terminal(self):
        """Initialize terminal for status line"""
        try:
            # Get terminal size
            size = shutil.get_terminal_size((80, 24))
            self.terminal_width = size.columns
            
            # Set up terminal for status line
            print("\033[2J\033[H", end="")  # Clear screen and home cursor
            print(f"\033[2;{size.lines}r", end="")  # Set scroll region (leave top 1 line for status)
            print("\033[2;1H", end="")  # Position cursor at line 2
            print("\033[s", end="")  # Save cursor position
            sys.stdout.flush()
            
            # Draw initial status line
            self.status_line = " ZVibe Python Console"
            self._draw_status_line()
        except Exception as e:
            print(f"[WARNING] Failed to initialize terminal: {e}")
            self.enable_status = False
    
    def _cleanup_terminal(self):
        """Clean up terminal state"""
        try:
            size = shutil.get_terminal_size((80, 24))
            print(f"\033[1;{size.lines}r", end="")  # Reset scroll region
            print("\033[0m", end="")  # Reset video attributes
            print("\n", end="")
            sys.stdout.flush()
        except:
            pass
    
    def _draw_status_line(self):
        """Draw the status line"""
        if not self.enable_status:
            return
        
        try:
            # Save cursor position
            print("\033[s", end="")
            
            # Go to status line (row 1)
            print("\033[1;1H", end="")
            print("\033[7m", end="")  # Reverse video
            
            # Print status line with padding
            status_text = f"{self.status_line:<{self.terminal_width}}"[:self.terminal_width]
            print(status_text, end="")
            
            print("\033[0m", end="")  # Normal video
            
            # Restore cursor position
            print("\033[u", end="")
            sys.stdout.flush()
        except:
            pass
    
    def _status_handler(self, status_ptr):
        """Handle status updates from the game - simple display mode"""
        if not self.enable_status:
            return
        
        try:
            status = status_ptr.contents
            location = ctypes.string_at(status.location).decode('utf-8', errors='replace') or "Unknown"
            status_text = ctypes.string_at(status.status).decode('utf-8', errors='replace') or ""
            
            # Simple status display - just print when it changes
            if status_text:
                new_status = f"[Status: {location} - {status_text}]"
            elif status.is_time:
                new_status = f"[Status: {location} - Time: {status.hours:02d}:{status.minutes:02d}]"
            else:
                new_status = f"[Status: {location} - Score: {status.score}, Moves: {status.turns}]"
            
            # Only print if status actually changed
            if new_status != self.status_line:
                self.status_line = new_status
                print(f"\n{new_status}")
                
        except Exception as e:
            print(f"[Status error: {e}]", file=sys.stderr)
    
    def _save_callback(self, data_ptr, size):
        """Handle save requests from the game - file-based only"""
        try:
            if size > 65536:  # Reasonable size limit
                print("\n[Save data too large]")
                return 0
            
            # Get save data
            data = ctypes.string_at(data_ptr, size)
            
            # Write save data to file
            if self.save_file_path:
                with open(self.save_file_path, 'wb') as f:
                    f.write(data)
                print(f"\n[Game saved to {os.path.basename(self.save_file_path)} - {size} bytes]")
                return 1
            else:
                print("\n[Save error: No save file path configured]")
                return 0
        except Exception as e:
            print(f"\n[Save error: {e}]")
            return 0
    
    def _restore_callback(self, buffer_ptr, max_size):
        """Handle restore requests from the game - file-based only"""
        try:
            # Check if save file exists
            if not self.save_file_path or not os.path.exists(self.save_file_path):
                print("\n[No save file found]")
                return 0
            
            # Read save data from file
            with open(self.save_file_path, 'rb') as f:
                data = f.read()
            
            save_size = len(data)
            if save_size > max_size:
                print("\n[Save data too large for buffer]")
                return 0
            
            # Copy save data to the buffer
            ctypes.memmove(buffer_ptr, data, save_size)
            print(f"\n[Game restored from {os.path.basename(self.save_file_path)} - {save_size} bytes]")
            return save_size
        except Exception as e:
            print(f"\n[Restore error: {e}]")
            return 0


def main():
    # Default library path relative to script location
    script_dir = os.path.dirname(os.path.abspath(__file__))
    
    # Determine correct library extension based on platform
    import platform
    system = platform.system()
    if system == "Darwin":  # macOS
        lib_ext = ".dylib"
    elif system == "Linux":
        lib_ext = ".so"
    elif system == "Windows":
        lib_ext = ".dll"
    else:
        lib_ext = ".so"  # Default fallback
    
    default_library_path = os.path.join(script_dir, 'build', 'lib', f'libzvibe{lib_ext}')
    
    parser = argparse.ArgumentParser(
        description='Python console for zVibe Z-machine interpreter',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Basic usage (save/restore enabled by default)
  python3 zvibe_console.py zork1.z3
  
  # Enhanced mode with status display
  python3 zvibe_console.py --status zork1.z3
  
  # Minimal mode (no save/restore)
  python3 zvibe_console.py --no-save zork1.z3
  
  # Run with script file
  python3 zvibe_console.py --script commands.txt zork1.z3
  
  # Specify custom library
  python3 zvibe_console.py --library /path/to/libzvibe.dylib zork1.z3
""")
    
    parser.add_argument('story_file', 
                        help='Z-machine story file (.z3)')
    parser.add_argument('-l', '--library', 
                        default=default_library_path,
                        help=f'Path to zvibe shared library (default: {default_library_path})')
    parser.add_argument('--status', 
                        action='store_true',
                        help='Enable status information display (simple mode)')
    parser.add_argument('--no-save', 
                        action='store_true',
                        help='Disable save/restore functionality (minimal mode)')
    parser.add_argument('-s', '--script',
                        help='Read commands from script file')
    parser.add_argument('--version', 
                        action='version',
                        version='ZVibe Python Console 1.0')
    
    # Support legacy usage for backwards compatibility
    if len(sys.argv) >= 3 and not sys.argv[1].startswith('-'):
        # Check if this looks like old format: library_path story_file
        if os.path.exists(sys.argv[1]) and sys.argv[1].endswith(('.so', '.dylib', '.dll')):
            print("Warning: Using legacy argument format. Consider using --library option.")
            args = parser.parse_args([sys.argv[2], '--library', sys.argv[1]])
        else:
            args = parser.parse_args()
    else:
        args = parser.parse_args()
    
    # Check if library exists
    if not os.path.exists(args.library):
        print(f"Error: Library file not found: {args.library}")
        print("\nTo create the library, run 'make shared' from the zVibe root directory")
        print("This will create:")
        print(f"  build/lib/libzvibe{lib_ext}          (full-featured with all capabilities)")
        sys.exit(1)
    
    # Check if story file exists
    if not os.path.exists(args.story_file):
        print(f"Error: Story file not found: {args.story_file}")
        sys.exit(1)
    
    console = ZVibeConsole(
        library_path=args.library,
        enable_status=args.status,
        enable_save=not args.no_save,  # Save enabled by default, disabled with --no-save
        script_file=args.script
    )
    
    try:
        console.create_context()
        console.load_story(args.story_file)
        console.run_game()
    except KeyboardInterrupt:
        print("\n[Interrupted by user]")
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)
    finally:
        console.cleanup()


if __name__ == "__main__":
    main()
