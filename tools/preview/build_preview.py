"""Build and run the host UI preview with MSVC (already installed Visual Studio).

Usage: python -I tools/preview/build_preview.py
Screenshots land in .pio/preview/*.png
"""
import glob, os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, ".pio", "preview")
LVGL = os.path.join(ROOT, ".pio", "libdeps", "cyd_st7789", "lvgl")
os.makedirs(OUT, exist_ok=True)

vswhere = r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
vs = subprocess.check_output([vswhere, "-latest", "-products", "*", "-requires",
                              "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                              "-property", "installationPath"], text=True).strip()
vcvars = os.path.join(vs, "VC", "Auxiliary", "Build", "vcvars64.bat")

c_srcs = glob.glob(os.path.join(LVGL, "src", "**", "*.c"), recursive=True)
c_srcs += glob.glob(os.path.join(ROOT, "src", "fonts", "*.c"))
cpp_srcs = [os.path.join(ROOT, "tools", "preview", "preview.cpp"), os.path.join(ROOT, "src", "ui.cpp")]

flags = ["/nologo", "/O1", "/MP", "/utf-8", "/w", "/DLV_CONF_INCLUDE_SIMPLE", "/DLV_LVGL_H_INCLUDE_SIMPLE",
         "/D_CRT_SECURE_NO_WARNINGS",
         "/I" + os.path.join(ROOT, "tools", "preview", "stub"), "/I" + os.path.join(ROOT, "include"),
         "/I" + os.path.join(ROOT, "src"), "/I" + LVGL, "/I" + os.path.join(LVGL, "src")]
rsp = os.path.join(OUT, "build.rsp")
with open(rsp, "w") as f:
    f.write("\n".join('"%s"' % a for a in flags) + "\n")
    f.write("/Fe" + '"' + os.path.join(OUT, "preview.exe") + '"' + "\n")
    f.write("\n".join('/Tc"%s"' % s for s in c_srcs) + "\n")
    f.write("\n".join('/Tp"%s"' % s for s in cpp_srcs) + "\n")

bat = os.path.join(OUT, "build.bat")
with open(bat, "w") as f:
    f.write('@echo off\ncall "%s" >nul\ncl @"%s"\n' % (vcvars, rsp))
r = subprocess.run(["cmd", "/c", bat], cwd=OUT, capture_output=True, text=True)
if r.returncode != 0:
    log = r.stdout + r.stderr
    errors = [l for l in log.splitlines() if "error" in l.lower()]
    print("\n".join(errors[:40]) or log[-4000:])
    sys.exit(1)
subprocess.check_call([os.path.join(OUT, "preview.exe"), OUT])
subprocess.check_call([sys.executable, "-I", os.path.join(ROOT, "tools", "preview", "to_png.py"), OUT])
