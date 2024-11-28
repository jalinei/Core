import os
import shutil
import yaml

Import("env")

zephyr_framework_path = os.path.join(env.PioPlatform().get_package_dir("framework-zephyr"))
west_zephyr_manifest_path = os.path.join(zephyr_framework_path, "west.yml")
west_project_manifest_path = os.path.join(env["PROJECT_DIR"], "west.yml")
west_backup_manifest_path = os.path.join(zephyr_framework_path, "west.yml.bak")

def load_west_yml(file_path):
    with open(file_path, 'r') as file:
        return yaml.safe_load(file)

def compare_west_ymls(file1, file2):
    data1 = load_west_yml(file1)
    data2 = load_west_yml(file2)

    projects1 = {proj['name']: proj for proj in data1['manifest']['projects']}
    projects2 = {proj['name']: proj for proj in data2['manifest']['projects']}

    added = []
    modified = []

    # Check for new or modified entries
    for name, project in projects2.items():
        if name not in projects1:
            added.append(project)
        elif projects1[name].get('revision') != project.get('revision'):
            modified.append({
                'name': name,
                'old_revision': projects1[name].get('revision'),
                'new_revision': project.get('revision'),
                'path': project.get('path')
            })

    # Check for removed entries
    removed = [project for name, project in projects1.items() if name not in projects2]

    return added, modified, removed

def print_results(added, modified, removed):
    if added:
        print("Added Projects:")
        for proj in added:
            print(f"  - {proj['name']} (Revision: {proj.get('revision')}, Path: {proj.get('path')})")
    if modified:
        print("\nModified Projects:")
        for proj in modified:
            print(f"  - {proj['name']} (Old Revision: {proj['old_revision']}, New Revision: {proj['new_revision']}, Path: {proj['path']})")
    if removed:
        print("\nRemoved Projects:")
        for proj in removed:
            print(f"  - {proj['name']} (Revision: {proj.get('revision')}, Path: {proj.get('path')})")

def print_results(added, modified, removed):
    if added:
        print("Added Projects:")
        for proj in added:
            print(f"  - {proj['name']} (Revision: {proj.get('revision')}, Path: {proj.get('path')})")
    if modified:
        print("\nModified Projects:")
        for proj in modified:
            print(f"  - {proj['name']} (Old Revision: {proj['old_revision']}, New Revision: {proj['new_revision']}, Path: {proj['path']})")
    if removed:
        print("\nRemoved Projects:")
        for proj in removed:
            print(f"  - {proj['name']} (Revision: {proj.get('revision')}, Path: {proj.get('path')})")

def pre_replace_west_manifest(project_manifest_path, backup_manifest_path,
                              zephyr_manifest_path):

    # Check if local project manifest exists
    if os.path.exists(project_manifest_path):
        print("Local west.yml found in project.")

        # Backup the original framework west.yml if not already backed up
        if not os.path.exists(backup_manifest_path):
            print("Backing up original west.yml...")
            shutil.copy(zephyr_manifest_path, backup_manifest_path)
        else:
            print("Backup of original west.yml already exists.")

        # Replace the framework west.yml with the project-specific manifest
        print("Replacing west.yml with the local project manifest...")
        shutil.copy(project_manifest_path, zephyr_manifest_path)
    else:
        print("No local west.yml found. Using default west.yml.")

def clear_deprecated_package(path):
    deprecated_pkg_path = os.path.join(zephyr_framework_path, "_pio", path)
    if os.path.isdir(deprecated_pkg_path):
        shutil.rmtree(deprecated_pkg_path)
    else:
        print("Package path not found")


added, modified, removed = compare_west_ymls(west_zephyr_manifest_path, west_project_manifest_path)
print_results(added, modified, removed)
if modified:
    for deprecated in modified:
        print(f"Clearing {deprecated['path']} to trigger automatic package update")
        clear_deprecated_package(deprecated['path'])

pre_replace_west_manifest(west_project_manifest_path, west_backup_manifest_path,
                          west_zephyr_manifest_path)