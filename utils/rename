#!/usr/bin/env python3
"""
Batch rename configuration files with flexible pattern matching.
Usage: 
    ./rename.py --replace "16G=>32G" "1G=>2G" --dir tests/benchmarks/bfs_web
    ./rename.py -r "16G=>32G" -d tests/benchmarks/bfs_web --preview
    ./rename.py -r "16G=>32G" -d tests/benchmarks/bfs_web --include-dirs
    ./rename.py -r "16G=>32G" -d tests/benchmarks --pattern "*.cfg"
    ./rename.py --help
"""

import os
import sys
import argparse
import shutil
import fnmatch
from pathlib import Path

def parse_replacement(pattern):
    """Parse a replacement pattern in the format 'old=>new'"""
    if "=>" not in pattern:
        raise ValueError(f"Invalid replacement pattern: {pattern}. Must be in format 'old=>new'")
    old, new = pattern.split("=>")
    return old.strip(), new.strip()

def should_process_path(path, pattern=None, include_dirs=False):
    """Check if a path should be processed based on pattern and settings"""
    if path.is_file():
        return pattern is None or fnmatch.fnmatch(path.name, pattern)
    return include_dirs  # Process directory if include_dirs is True

def generate_new_name(path, replacements, include_dirs=False):
    """Generate new filename/path based on replacement patterns"""
    if include_dirs:
        # Replace in the full path
        old_name = str(path)
        new_name = old_name
        for old, new in replacements:
            new_name = new_name.replace(old, new)
        return new_name
    else:
        # Replace only in the filename
        old_name = path.name
        new_name = old_name
        for old, new in replacements:
            new_name = new_name.replace(old, new)
        return str(path.parent / new_name)

def rename_files(directory, replacements, preview=False, recursive=False, include_dirs=False, pattern=None):
    """Rename files in directory according to replacement patterns"""
    # Convert to absolute path and resolve any symlinks
    dir_path = Path(directory).resolve()
    print(f"Scanning directory: {dir_path}")
    print(f"File pattern: {pattern if pattern else 'all files'}")
    
    if not dir_path.exists():
        print(f"Error: Directory '{directory}' does not exist")
        return

    # Collect all paths to process
    if recursive:
        paths = list(dir_path.rglob("*"))
    else:
        paths = list(dir_path.glob("*"))

    # Sort paths to process directories after their contents
    paths.sort(key=lambda x: (x.is_file(), str(x)))

    # Filter paths based on pattern and settings
    paths = [p for p in paths if should_process_path(p, pattern, include_dirs)]
    print(f"Found {len(paths)} items to process")
    
    changes = []
    for path in paths:
        old_name = str(path)
        new_name = generate_new_name(path, replacements, include_dirs)
        
        if old_name != new_name:
            changes.append((old_name, new_name))

    if not changes:
        print("No items need to be renamed")
        return

    # Show preview or perform renaming
    print(f"\n{'PREVIEW: ' if preview else ''}Found {len(changes)} items to rename:")
    for old_name, new_name in changes:
        print(f"  {old_name} -> {new_name}")

    if not preview:
        confirm = input("\nProceed with renaming? (y/N): ").lower()
        if confirm == 'y':
            # Sort changes to handle deepest paths first
            changes.sort(key=lambda x: len(x[0].split(os.sep)), reverse=True)
            for old_name, new_name in changes:
                os.makedirs(os.path.dirname(new_name), exist_ok=True)
                shutil.move(old_name, new_name)
            print("\nRenaming complete!")
        else:
            print("\nOperation cancelled")

def main():
    parser = argparse.ArgumentParser(description="Batch rename files and directories")
    parser.add_argument('-r', '--replace', nargs='+', required=True,
                      help="Replacement patterns in format 'old=>new'")
    parser.add_argument('-d', '--dir', required=True,
                      help="Directory containing items to rename")
    parser.add_argument('-p', '--preview', action='store_true',
                      help="Preview changes without renaming")
    parser.add_argument('--recursive', action='store_true',
                      help="Recursively process subdirectories")
    parser.add_argument('--include-dirs', action='store_true',
                      help="Include directory names in replacement patterns")
    parser.add_argument('--pattern', 
                      help="File pattern to match (e.g., '*.cfg', '*.txt')")
    
    args = parser.parse_args()

    try:
        # Parse replacement patterns
        replacements = [parse_replacement(pattern) for pattern in args.replace]
        print(f"Using replacement patterns: {replacements}")
        print(f"Including directory names: {args.include_dirs}")
        
        # Remove trailing slash from directory path if present
        directory = args.dir.rstrip('/')
        
        # Perform renaming
        rename_files(directory, replacements, args.preview, args.recursive, 
                    args.include_dirs, args.pattern)
        
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main() 