# Adafruit_MPU6050::begin() rejects WHO_AM_I values other than 0x68.
# This board is a clone that answers at address 0x68 and reports 0x98.
Import("env")

from os.path import join

path = join(
    env["PROJECT_LIBDEPS_DIR"],
    env["PIOENV"],
    "Adafruit MPU6050",
    "Adafruit_MPU6050.cpp",
)

with open(path) as f:
    text = f.read()

old = """  // make sure we're talking to the right chip
  if (chip_id.read() != MPU6050_DEVICE_ID) {
    return false;
  }"""

new = """  // Genuine parts return 0x68. Widespread clones keep the MPU6050 register
  // map but return 0x98 from WHO_AM_I.
  uint8_t who = chip_id.read();
  if (who != MPU6050_DEVICE_ID && who != 0x98) {
    return false;
  }"""

if old in text:
    with open(path, "w") as f:
        f.write(text.replace(old, new, 1))
elif "who != 0x98" not in text:
    raise SystemExit("Adafruit_MPU6050 WHO_AM_I check not found; patch not applied")
