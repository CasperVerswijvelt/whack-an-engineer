import subprocess
import os

# Path to the patches directory
PATCHES_DIR = os.path.join("patches")

def undo_patch(patch_file):
    # Command to reverse the patch
    command = ["patch", "-p0", "-R", "-i", patch_file]

    # Run the patch command
    result = subprocess.run(command, capture_output=True, text=True)

    # Print output for debugging
    print(f"Reversing {patch_file}...")
    print(result.stdout)
    print(result.stderr)

    # Check if the undo was successful
    if result.returncode != 0:
        print(f"Failed to reverse patch {patch_file}!")

def undo_all_patches():
    # Iterate over all .diff files in the PATCHES_DIR
    for patch_filename in os.listdir(PATCHES_DIR):
        if patch_filename.endswith(".diff"):
            patch_path = os.path.join(PATCHES_DIR, patch_filename)
            undo_patch(patch_path)

# Run the function to undo all patches
undo_all_patches()
