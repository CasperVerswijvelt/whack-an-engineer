import subprocess
import os

# Path to the patches directory
PATCHES_DIR = os.path.join("patches")

def apply_patch(patch_file):
    # Command to apply the patch
    command = ["patch", "-p0", "-f", "-i", patch_file]

    # Run the patch command
    result = subprocess.run(command, capture_output=True, text=True)

    # Print output for debugging
    print(f"Applying {patch_file}...")
    print(result.stdout)
    print(result.stderr)

    # Check if patch was applied successfully
    if result.returncode != 0:
        print(f"Patch {patch_file} failed!")

def apply_all_patches():
    # Iterate over all .patch files in the PATCHES_DIR
    for patch_filename in os.listdir(PATCHES_DIR):
        if patch_filename.endswith(".diff"):
            patch_path = os.path.join(PATCHES_DIR, patch_filename)
            apply_patch(patch_path)

# Run the function to apply all patches
apply_all_patches()
