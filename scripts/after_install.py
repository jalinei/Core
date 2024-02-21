import os
import shutil

zephyr_path = os.path.join(".", "zephyr")
print(zephyr_path)
owntech_path = os.path.join(".", "owntech")
print(owntech_path)
if not os.path.isdir(owntech_path):
    os.mkdir(owntech_path)

lib_path = os.path.join(".", "lib", "owntech_core")
print(lib_path)

shutil.move(os.path.join(lib_path, "zephyr"), zephyr_path)
shutil.move(os.path.join(lib_path, "owntech"), owntech_path)