#!/usr/bin/env python3
"""
Tmux Job Manager: Run a list of jobs in separate tmux sessions

This script reads a jobs file and creates a separate tmux session for each job.
Each job can have a custom name, command, and working directory.
Now includes status monitoring and output capture features.
"""

import os
import sys
import subprocess
import argparse
import time
import json
import re
import tempfile
import psutil
import datetime
import shutil

# Configuration
OUTPUT_CAPTURE_DIR = os.path.expanduser("~/.tmux_manager/logs")
OUTPUT_LINES = 10  # Number of lines to show in verbose output

def ensure_dir_exists(directory):
    """Ensure a directory exists, creating it if necessary"""
    if not os.path.exists(directory):
        os.makedirs(directory)

def get_terminal_width():
    """Get the width of the terminal"""
    try:
        return shutil.get_terminal_size().columns
    except:
        return 80

def get_process_status(pid):
    """Check if a process is still running and get its status"""
    try:
        process = psutil.Process(int(pid))
        status = process.status()
        cpu_percent = process.cpu_percent(interval=0.1)
        memory_info = process.memory_info()
        memory_mb = memory_info.rss / (1024 * 1024)  # Convert to MB
        
        return {
            'running': True,
            'status': status,
            'cpu': cpu_percent,
            'memory_mb': memory_mb,
            'start_time': datetime.datetime.fromtimestamp(process.create_time()).strftime('%Y-%m-%d %H:%M:%S'),
            'runtime': int(time.time() - process.create_time())
        }
    except (psutil.NoSuchProcess, psutil.AccessDenied, ValueError):
        return {'running': False, 'status': 'NOT RUNNING'}

def format_duration(seconds):
    """Format a duration in seconds to a human-readable string"""
    if seconds < 60:
        return f"{seconds}s"
    elif seconds < 3600:
        return f"{seconds // 60}m {seconds % 60}s"
    elif seconds < 86400:
        hours = seconds // 3600
        minutes = (seconds % 3600) // 60
        return f"{hours}h {minutes}m"
    else:
        days = seconds // 86400
        hours = (seconds % 86400) // 3600
        return f"{days}d {hours}h"

def check_session_exists(session_name):
    """Check if a tmux session already exists"""
    result = subprocess.run(['tmux', 'has-session', '-t', session_name], 
                           stderr=subprocess.PIPE, stdout=subprocess.PIPE)
    return result.returncode == 0

def setup_output_capture(session_name):
    """Set up output capture for a tmux session"""
    ensure_dir_exists(OUTPUT_CAPTURE_DIR)
    output_file = os.path.join(OUTPUT_CAPTURE_DIR, f"{session_name}.log")
    
    try:
        # Configure tmux to pipe pane output to a file
        subprocess.run([
            'tmux', 'pipe-pane', '-t', session_name, 
            f'cat >> {output_file}'
        ])
        return output_file
    except Exception as e:
        print(f"Warning: Failed to set up output capture: {e}")
        return None

def get_session_info(session_name):
    """Get detailed information about a running tmux session"""
    try:
        # Get pane info
        result = subprocess.run(
            ['tmux', 'list-panes', '-t', session_name, '-F', '#{pane_pid}'],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )
        
        if result.returncode != 0 or not result.stdout.strip():
            return None
            
        pane_pid = result.stdout.strip()
        
        # Get process info using psutil
        process_info = get_process_status(pane_pid)
        
        # Get the command running in this pane
        ps_result = subprocess.run(
            ['ps', '-p', pane_pid, '-o', 'command='],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )
        
        command = ps_result.stdout.strip() if ps_result.returncode == 0 else "Unknown"
        
        # Check if output file exists
        output_file = os.path.join(OUTPUT_CAPTURE_DIR, f"{session_name}.log")
        has_output = os.path.exists(output_file)
        
        return {
            'pid': pane_pid,
            'command': command,
            'status': process_info['status'],
            'running': process_info['running'],
            'output_file': output_file if has_output else None,
            'cpu': process_info.get('cpu', 0),
            'memory_mb': process_info.get('memory_mb', 0),
            'start_time': process_info.get('start_time', 'Unknown'),
            'runtime': process_info.get('runtime', 0)
        }
    except Exception as e:
        print(f"Error getting session info: {e}")
    
    return None

def get_session_output(output_file, lines=OUTPUT_LINES):
    """Get the last few lines of a session's output file"""
    if not output_file or not os.path.exists(output_file):
        return "No output captured"
    
    try:
        # Use tail to get the last few lines
        result = subprocess.run(
            ['tail', '-n', str(lines), output_file],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )
        
        if result.returncode == 0 and result.stdout.strip():
            return result.stdout.strip()
        else:
            return "Output file exists but is empty"
    except Exception as e:
        return f"Error reading output: {e}"

def create_tmux_session(session_name, command, working_dir=None, force=False, capture_output=True):
    """Create a new tmux session with the given name and command"""
    # First check if session already exists
    if check_session_exists(session_name):
        session_info = get_session_info(session_name)
        if session_info:
            status = "RUNNING" if session_info['running'] else "NOT RUNNING"
            print(f"⚠️  WARNING: Session '{session_name}' already exists with PID {session_info['pid']} ({status})")
            print(f"    Running command: {session_info['command']}")
        else:
            print(f"⚠️  WARNING: Session '{session_name}' already exists")
            
        if force:
            print(f"    Force flag set. Killing existing session...")
            subprocess.run(['tmux', 'kill-session', '-t', session_name])
        else:
            print(f"    Skipping. Use --force to replace existing sessions.")
            return False
    
    # Prepare the command
    if working_dir:
        # If working directory is specified, cd to it first
        full_command = f"cd {working_dir} && {command}"
    else:
        full_command = command
    
    # Create new detached session
    subprocess.run([
        'tmux', 'new-session', 
        '-d',  # detached
        '-s', session_name,  # session name
        full_command  # command to run
    ])
    
    print(f"✅ Started session: {session_name}")
    
    # Set up output capture if requested
    if capture_output:
        output_file = setup_output_capture(session_name)
        if output_file:
            print(f"   Output captured to: {output_file}")
    
    return True

def read_jobs_file(filename):
    """Read jobs from a file in various formats"""
    if not os.path.exists(filename):
        print(f"Error: Jobs file '{filename}' not found.")
        sys.exit(1)
    
    jobs = []
    
    # Determine file type by extension
    if filename.endswith('.json'):
        # JSON format
        with open(filename, 'r') as f:
            jobs = json.load(f)
    else:
        # Simple text format (name:command:directory)
        with open(filename, 'r') as f:
            for line_num, line in enumerate(f, 1):
                line = line.strip()
                # Skip empty lines and comments
                if not line or line.startswith('#'):
                    continue
                
                parts = line.split(':', 2)  # Split into up to 3 parts
                
                if len(parts) == 1:
                    # If only one part, use it as the command and generate a name
                    jobs.append({
                        'name': f"job-{len(jobs)+1}",
                        'command': parts[0]
                    })
                elif len(parts) == 2:
                    # If two parts, use as name and command
                    jobs.append({
                        'name': parts[0],
                        'command': parts[1]
                    })
                elif len(parts) == 3:
                    # If three parts, use as name, command, and directory
                    jobs.append({
                        'name': parts[0],
                        'command': parts[1],
                        'working_dir': parts[2] if parts[2] else None
                    })
                else:
                    print(f"Warning: Invalid format at line {line_num}: {line}")
    
    return jobs

def list_tmux_sessions(verbose=False, output_lines=OUTPUT_LINES):
    """List all current tmux sessions with optional detailed info"""
    try:
        result = subprocess.run(['tmux', 'list-sessions', '-F', '#{session_name}'],
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        
        if result.returncode != 0 or not result.stdout.strip():
            print("No active tmux sessions found.")
            return []
        
        sessions = result.stdout.strip().split('\n')
        print(f"Found {len(sessions)} active tmux session(s):")
        print()
        
        term_width = get_terminal_width()
        
        for session in sessions:
            session_info = get_session_info(session)
            
            if not session_info:
                print(f"  • {session} (No info available)")
                continue
            
            # Basic status indicator (emoji + color if supported)
            status_emoji = "🟢" if session_info['running'] else "🔴"
            
            # Format runtime
            runtime_str = format_duration(session_info['runtime']) if session_info['running'] else "N/A"
            
            # Print basic session info
            print(f"  {status_emoji} {session} (PID: {session_info['pid']})")
            print(f"    Status: {session_info['status']} | Runtime: {runtime_str}")
            print(f"    Command: {session_info['command']}")
            
            if verbose:
                if session_info['running']:
                    # Print resource usage
                    print(f"    CPU: {session_info['cpu']:.1f}% | Memory: {session_info['memory_mb']:.1f} MB")
                    print(f"    Started: {session_info['start_time']}")
                
                # Print recent output if available
                if session_info['output_file']:
                    output = get_session_output(session_info['output_file'], output_lines)
                    print("\n    Recent output:")
                    # Print a separator line
                    print("    " + "-" * min(70, term_width - 8))
                    # Print each line of output with indentation
                    for line in output.split('\n')[:output_lines]:
                        # Truncate long lines to fit terminal
                        if len(line) > term_width - 8:
                            line = line[:term_width - 11] + "..."
                        print(f"    | {line}")
                    print("    " + "-" * min(70, term_width - 8))
                    print(f"    Full log: {session_info['output_file']}")
                else:
                    print("    No output captured.")
            
            print()
        
        return sessions
    except Exception as e:
        print(f"Error listing sessions: {e}")
        return []

def main():
    parser = argparse.ArgumentParser(description='Run jobs in tmux sessions')
    parser.add_argument('jobs_file', nargs='?', help='File containing jobs to run')
    parser.add_argument('--list', '-l', action='store_true', help='List running tmux sessions')
    parser.add_argument('--verbose', '-v', action='store_true', help='Show detailed session information')
    parser.add_argument('--output-lines', '-n', type=int, default=OUTPUT_LINES, 
                       help=f'Number of output lines to show (default: {OUTPUT_LINES})')
    parser.add_argument('--kill-all', '-K', action='store_true', help='Kill all tmux sessions')
    parser.add_argument('--kill', '-k', metavar='SESSION_NAME', help='Kill a specific tmux session')
    parser.add_argument('--attach', '-a', metavar='SESSION_NAME', help='Attach to a specific tmux session')
    parser.add_argument('--force', '-f', action='store_true', help='Force recreation of sessions even if they exist')
    parser.add_argument('--no-capture', action='store_true', help='Disable output capturing')
    parser.add_argument('--show-output', '-o', metavar='SESSION_NAME', 
                       help='Display the full output log of a specific session')
    
    args = parser.parse_args()
    
    # Check if tmux is installed
    try:
        subprocess.run(['tmux', '-V'], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    except FileNotFoundError:
        print("Error: tmux not found. Please install tmux first.")
        sys.exit(1)
    
    # Check if psutil is installed
    try:
        import psutil
    except ImportError:
        print("Error: psutil module not found. Install it with: pip install psutil")
        sys.exit(1)
    
    if args.show_output:
        session_name = args.show_output
        output_file = os.path.join(OUTPUT_CAPTURE_DIR, f"{session_name}.log")
        
        if os.path.exists(output_file):
            print(f"Full output log for session '{session_name}':")
            print("-" * get_terminal_width())
            # Use less to paginate if it's available, otherwise cat
            if shutil.which('less'):
                subprocess.run(['less', output_file])
            else:
                with open(output_file, 'r') as f:
                    print(f.read())
        else:
            print(f"No output log found for session '{session_name}'.")
        return
    
    if args.list:
        list_tmux_sessions(args.verbose, args.output_lines)
        return
    
    if args.kill_all:
        confirm = input("Are you sure you want to kill all tmux sessions? (y/N): ")
        if confirm.lower() == 'y':
            print("Killing all tmux sessions...")
            subprocess.run(['tmux', 'kill-server'])
            print("All sessions terminated.")
        else:
            print("Operation cancelled.")
        return
    
    if args.kill:
        if check_session_exists(args.kill):
            print(f"Killing tmux session: {args.kill}")
            subprocess.run(['tmux', 'kill-session', '-t', args.kill])
            print(f"Session '{args.kill}' terminated.")
        else:
            print(f"Session '{args.kill}' not found.")
        return
    
    if args.attach:
        if check_session_exists(args.attach):
            print(f"Attaching to tmux session: {args.attach}")
            subprocess.run(['tmux', 'attach-session', '-t', args.attach])
        else:
            print(f"Session '{args.attach}' not found.")
        return
    
    if not args.jobs_file:
        parser.print_help()
        return
    
    # Create output capture directory if it doesn't exist
    if not args.no_capture:
        ensure_dir_exists(OUTPUT_CAPTURE_DIR)
    
    # Read jobs and create tmux sessions
    jobs = read_jobs_file(args.jobs_file)
    
    if not jobs:
        print("No jobs found in file.")
        return
    
    print(f"Starting {len(jobs)} job(s) in tmux sessions...")
    
    created_count = 0
    skipped_count = 0
    
    for job in jobs:
        success = create_tmux_session(
            job['name'],
            job['command'],
            job.get('working_dir'),
            args.force,
            not args.no_capture
        )
        if success:
            created_count += 1
        else:
            skipped_count += 1
    
    print(f"\nJob summary: {created_count} created, {skipped_count} skipped")
    
    print("\nCurrent sessions:")
    sessions = list_tmux_sessions(args.verbose, args.output_lines)
    
    if sessions:
        print("\nYou can:")
        print("  • List sessions (basic):     ./tmux_manager.py --list")
        print("  • List with details:         ./tmux_manager.py --list --verbose")
        print("  • View full session output:  ./tmux_manager.py --show-output SESSION_NAME")
        print("  • Attach to a session:       ./tmux_manager.py --attach SESSION_NAME")
        print("  • Kill a specific session:   ./tmux_manager.py --kill SESSION_NAME")

if __name__ == "__main__":
    main()