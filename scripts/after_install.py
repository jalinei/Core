import os
import shutil

env = DefaultEnvironment()

zephyr_path = os.path.join(".", "zephyr")
owntech_path = os.path.join(".", "owntech")
if not os.path.isdir(owntech_path):
    os.mkdir(owntech_path)

lib_path = os.path.join(".", "owntech", "lib", env.Dump("PIOENV").strip("''"), "core")
print(lib_path)

shutil.move(os.path.join(lib_path, "zephyr"), zephyr_path)
shutil.move(os.path.join(lib_path, "owntech"), owntech_path)