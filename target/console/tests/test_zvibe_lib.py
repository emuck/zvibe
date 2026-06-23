#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""
ZVibe Library Test Runner
Automated test runner for zvibe Z-machine interpreter using ctypes
Downloads games dynamically and runs script files to verify game completion
"""

import ctypes
import sys
import os
from ctypes import c_char_p, c_size_t, c_int, c_void_p, CFUNCTYPE, POINTER

OUTPUT_FUNC = CFUNCTYPE(None, c_char_p, c_size_t)

# Constants - let's verify these match your zvibe_api.h exactly
ZVIBE_OK = 0
ZVIBE_ERROR = 1  
ZVIBE_WAIT_FOR_INPUT = 2
ZVIBE_GAME_FINISHED = 3

# Game configuration - Seastalker is our standard test game
# Download via: cd games && ./get_games.py browse
GAME_CONFIG = {
    'name': 'seastalker',
    'file': 'seastalker-mac-r15-s840522.z3',
    'completion_markers': [
        'Your score is 100 points out of 100',
        'This score gives you the rank of a famous adventurer',
        'Game finished'
    ]
}

class ZVibeLibTest:
    def __init__(self, library_path):
        """Initialize with path to your compiled zvibe library"""
        self.lib = ctypes.CDLL(library_path)
        self.ctx = None
        self.script_commands = []
        self.command_index = 0
        self.output_buffer = []
        self.game_completed = False
        
        # Define function signatures to match your zvibe_api.h
        self.lib.zvibe_create.argtypes = [OUTPUT_FUNC]
        self.lib.zvibe_create.restype = c_void_p
        
        self.lib.zvibe_load_story.argtypes = [c_void_p, c_char_p]
        self.lib.zvibe_load_story.restype = c_int
        
        self.lib.zvibe_run.argtypes = [c_void_p]
        self.lib.zvibe_run.restype = c_int
        
        self.lib.zvibe_input.argtypes = [c_void_p, c_char_p]
        self.lib.zvibe_input.restype = None  # Match original zvibe_console.py
        
        self.lib.zvibe_destroy.argtypes = [c_void_p]
        self.lib.zvibe_destroy.restype = None
        
        # Create callback function - keep reference to prevent garbage collection
        self.output_callback = OUTPUT_FUNC(self._output_handler)
        
        print("ZVibe Library Test initialized")
    
    def _output_handler(self, text, length):
        """Handle output from the game - capture and print it"""
        try:
            output = ctypes.string_at(text, length).decode('utf-8', errors='replace')
            print(output, end='', flush=True)
            # Store output for completion verification
            self.output_buffer.append(output)
        except Exception as e:
            print(f"[Output handler error: {e}]", file=sys.stderr)
    
    def download_game(self):
        """Locate the standard test game (Seastalker) in games/catalog/"""
        script_dir = os.path.dirname(os.path.abspath(__file__))
        catalog_dir = os.path.join(script_dir, '..', '..', '..', 'games', 'catalog')
        game_file = os.path.join(catalog_dir, GAME_CONFIG['file'])

        if not os.path.exists(game_file):
            raise RuntimeError(
                f"Game file not found: {game_file}\n"
                f"Download it first: cd games && ./get_games.py browse")

        print(f"Using game file: {game_file}")
        return game_file
    
    def load_script(self):
        """Load script commands from file"""
        # Navigate to project root and find scripts directory
        script_dir = os.path.dirname(os.path.abspath(__file__))
        project_root = os.path.join(script_dir, '..', '..', '..')
        script_file = os.path.join(project_root, 'tests', 'scripts', f'{GAME_CONFIG["name"]}-script.txt')
        
        if not os.path.exists(script_file):
            raise FileNotFoundError(f"Script file not found: {script_file}")
        
        print(f"Loading script: {script_file}")
        
        with open(script_file, 'r') as f:
            lines = f.readlines()
        
        # Filter out empty lines and comments, strip whitespace
        self.script_commands = []
        for line in lines:
            line = line.strip()
            if line and not line.startswith('#'):
                self.script_commands.append(line)
        
        print(f"Loaded {len(self.script_commands)} commands from script")
        self.command_index = 0
    
    def get_next_command(self):
        """Get the next command from the script"""
        if self.command_index < len(self.script_commands):
            command = self.script_commands[self.command_index]
            self.command_index += 1
            print(f"[Command {self.command_index}/{len(self.script_commands)}]: {command}")
            return command
        return None
    
    def create_context(self):
        """Create zvibe context"""
        self.ctx = self.lib.zvibe_create(self.output_callback)
        if not self.ctx:
            raise RuntimeError("Failed to create zvibe context")
    
    def load_story(self, story_path):
        """Load a Z3 story file"""
        if not self.ctx:
            raise RuntimeError("Context not created")
        
        if not os.path.exists(story_path):
            raise FileNotFoundError(f"Story file not found: {story_path}")
        
        result = self.lib.zvibe_load_story(self.ctx, story_path.encode('utf-8'))
        if result != ZVIBE_OK:
            raise RuntimeError(f"Failed to load story: {result}")
    
    def run_test(self):
        """Main test execution - run game with script commands"""
        if not self.ctx:
            raise RuntimeError("Context not created or story not loaded")
        
        print(f"Starting {GAME_CONFIG['name']} test...\n")
        
        try:
            while True:
                result = self.lib.zvibe_run(self.ctx)
                
                if result == ZVIBE_WAIT_FOR_INPUT:
                    # Check for completion markers in output before sending next command
                    if self.check_completion_in_output():
                        print("\n[Game completion detected!]")
                        self.game_completed = True
                        break
                    
                    # Get next command from script
                    command = self.get_next_command()
                    
                    if command is None:
                        print("\n[Script completed - no more commands]")
                        break
                    
                    # Send command to the game
                    self.lib.zvibe_input(self.ctx, command.encode('utf-8'))
                        
                elif result == ZVIBE_GAME_FINISHED:
                    print("\n[Game finished]")
                    self.game_completed = True
                    break
                else:
                    print(f"\n[Game error: {result}]")
                    break
                    
        except Exception as e:
            print(f"\n[Error during game: {e}]")
            # Check if we got completion before the crash
            if self.check_completion_in_output(game_name):
                print("[Game completion detected despite error!]")
                self.game_completed = True
                return True
            return False
        
        return True
    
    def check_completion_in_output(self):
        """Check if completion markers are present in the output buffer"""
        markers = GAME_CONFIG['completion_markers']
        output_text = ''.join(self.output_buffer)
        
        for marker in markers:
            if marker in output_text:
                return True
        return False
    
    def verify_completion(self):
        """Verify that the game completed successfully"""
        markers = GAME_CONFIG['completion_markers']
        output_text = ''.join(self.output_buffer)
        
        print(f"\nVerifying {GAME_CONFIG['name']} completion...")
        
        for marker in markers:
            if marker in output_text:
                print(f"[ok] Found completion marker: '{marker}'")
                return True
        
        print("[fail] No completion markers found")
        print("Last 200 characters of output:")
        print(repr(output_text[-200:]))
        return False
    
    def cleanup(self):
        """Clean up resources"""
        if self.ctx:
            print("[DEBUG] Cleaning up context...")
            self.lib.zvibe_destroy(self.ctx)
            self.ctx = None
            print("[DEBUG] Context cleaned up")

def main():
    # Default library path relative to tests directory
    script_dir = os.path.dirname(os.path.abspath(__file__))
    default_library_path = os.path.join(script_dir, '..', 'build', 'lib', 'libzvibe.so')
    
    if len(sys.argv) == 1:
        # No arguments, use default library
        library_path = default_library_path
    elif len(sys.argv) == 2:
        # Custom library path provided
        library_path = sys.argv[1]
    else:
        print("Usage: python test_zvibe_lib.py [library_path]")
        print(f"Runs automated test of {GAME_CONFIG['name']} using the zvibe shared library")
        print(f"\nDefault library: {default_library_path}")
        print("\nExamples:")
        print("  python test_zvibe_lib.py                    # Use default library")
        print("  python test_zvibe_lib.py /path/to/libzvibe.so  # Use custom library")
        sys.exit(1)
    
    # Check if library exists
    if not os.path.exists(library_path):
        print(f"Error: Library file not found: {library_path}")
        print("\nTo create the library, build the project:")
        print("  make")
        sys.exit(1)
    
    test_runner = ZVibeLibTest(library_path)
    
    try:
        # Download game file
        game_file = test_runner.download_game()
        
        # Load script commands
        test_runner.load_script()
        
        # Initialize game
        test_runner.create_context()
        test_runner.load_story(game_file)
        
        # Run the test
        success = test_runner.run_test()
        
        if success:
            # Verify completion
            if test_runner.verify_completion():
                print(f"\nSUCCESS: {GAME_CONFIG['name']} test completed successfully!")
                sys.exit(0)
            else:
                print(f"\nFAILED: FAILED: {GAME_CONFIG['name']} test did not complete successfully")
                sys.exit(1)
        else:
            print(f"\nFAILED: FAILED: {GAME_CONFIG['name']} test execution failed")
            sys.exit(1)
            
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)
    finally:
        test_runner.cleanup()

if __name__ == "__main__":
    main()
