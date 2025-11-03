from __future__ import annotations

import pathlib
import sys

from setuptools import find_packages, setup

try:
    from pybind11.setup_helpers import Pybind11Extension, build_ext
except ModuleNotFoundError as exc:
    raise ModuleNotFoundError(
        "pybind11 is required to build aynime_capture. "
        "Install build dependencies via `pip install pybind11`."
    ) from exc


ROOT = pathlib.Path(__file__).parent

long_description = (ROOT / "README.md").read_text(encoding="utf-8") if (ROOT / "README.md").exists() else ""

if sys.platform != "win32":
    raise RuntimeError("aynime_capture only supports Windows builds (targeting Windows 10/11).")

define_macros = [
    ("UNICODE", None),
    ("_UNICODE", None),
    ("WIN32_LEAN_AND_MEAN", None),
    ("NOMINMAX", None),
]

extra_compile_args: list[str] = []
extra_link_args: list[str] = []
libraries: list[str] = ["d3d11", "dxgi", "windowsapp"]

if sys.platform == "win32":
    extra_compile_args.extend(["/std:c++20", "/EHsc", "/Zc:__cplusplus", "/permissive-"])
else:
    extra_compile_args.append("-std=c++20")

ext_modules = [
    Pybind11Extension(
        name="aynime_capture._ayncap",
        sources=[
            "src/aynime_capture/_ayncap_module.cpp",
            "src/aynime_capture/wgc_core.cpp",
        ],
        define_macros=define_macros,
        extra_compile_args=extra_compile_args,
        extra_link_args=extra_link_args,
        libraries=libraries,
        include_dirs=[],
    )
]

setup(
    name="aynime_capture",
    version="0.1.0",
    description="Windows Graphics Capture ring buffer for HWND/HMONITOR targets",
    long_description=long_description,
    long_description_content_type="text/markdown",
    author="Nu-Pan",
    python_requires=">=3.9",
    packages=find_packages(where="src"),
    package_dir={"": "src"},
    include_package_data=True,
    ext_modules=ext_modules,
    cmdclass={"build_ext": build_ext},
    zip_safe=False,
    classifiers=[
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3 :: Only",
        "Programming Language :: C++",
        "Operating System :: Microsoft :: Windows :: Windows 10",
        "Operating System :: Microsoft :: Windows :: Windows 11",
        "License :: OSI Approved :: MIT License",
    ],
    extras_require={
        "dev": ["pytest"],
    },
)
