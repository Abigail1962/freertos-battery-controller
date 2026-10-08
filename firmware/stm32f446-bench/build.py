"""Offline ARM build using installed CubeIDE toolchain or explicit ARM_GCC."""
from pathlib import Path
import os
import shutil
import subprocess
root = Path(__file__).resolve().parent
configured = os.environ.get("ARM_GCC") or shutil.which("arm-none-eabi-gcc")
candidates = sorted(Path("/Applications/STM32CubeIDE.app/Contents/Eclipse/plugins").glob(
    "com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.*/tools/bin/arm-none-eabi-gcc"))
if not configured and not candidates:
    raise SystemExit("Set ARM_GCC to your arm-none-eabi-gcc executable")
gcc = str(configured or candidates[-1])
tools = Path(gcc).parent
v = root / "vendor"
includes = ["include", "vendor/device/Include", "vendor/core/CMSIS/Core/Include",
            "vendor/freertos/include", "vendor/freertos/portable/GCC/ARM_CM4F"]
flags = ["-mcpu=cortex-m4", "-mthumb", "-mfpu=fpv4-sp-d16", "-mfloat-abi=hard",
         "-DSTM32F446xx", "-std=gnu11", "-Os", "-g3", "-ffunction-sections", "-fdata-sections",
         "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter"]
flags += ["-I" + str(root / path) for path in includes]
common = ["src/board.c", "src/controller.c", "src/mpu6050.c", "src/bench_app.c", "src/runtime.c",
          "vendor/device/Source/Templates/system_stm32f4xx.c",
          "vendor/device/Source/Templates/gcc/startup_stm32f446xx.s"]
for mode in ["baremetal", "freertos"]:
    out = root / "build" / mode
    out.mkdir(parents=True, exist_ok=True)
    sources = common + ["src/" + mode + "_main.c"]
    if mode == "freertos":
        sources += ["vendor/freertos/" + name for name in [
            "tasks.c", "queue.c", "list.c", "portable/GCC/ARM_CM4F/port.c", "portable/MemMang/heap_4.c"]]
    objects = []
    for source in sources:
        obj = out / (source.replace("/", "_") + ".o")
        subprocess.run([gcc, *flags, "-c", str(root / source), "-o", str(obj)], check=True)
        objects.append(str(obj))
    elf = out / "firmware.elf"
    subprocess.run([gcc, *flags, "-nostartfiles", "--specs=nano.specs", "--specs=nosys.specs",
                    "-T" + str(root / "stm32f446re.ld"), "-Wl,--gc-sections",
                    "-Wl,-Map=" + str(out / "firmware.map"), *objects, "-o", str(elf)], check=True)
    subprocess.run([str(tools / "arm-none-eabi-objcopy"), "-O", "binary", str(elf),
                    str(out / "firmware.bin")], check=True)
    subprocess.run([str(tools / "arm-none-eabi-size"), str(elf)], check=True)
    print(mode + ": ARM build passed; hardware execution NOT verified", flush=True)
