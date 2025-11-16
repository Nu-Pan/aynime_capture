
from setuptools import setup
from pybind11.setup_helpers import Pybind11Extension, build_ext

ext_modules = [
    Pybind11Extension(
        "aynime_capture._aynime_capture",
        [
            "core/source/core.cpp",
            "core/source/utils.cpp",
            "core/source/stdafx.cpp",
            "core/source/wgc_system.cpp",
            "core/source/wgc_session.cpp"
        ],
        include_dirs=[
            "core/include"
        ],
        libraries=[
            "d3d11",
            "dxgi",
            "WindowsApp"
        ],
        extra_compile_args=[
            "/std:c++17",
            "/EHsc"
        ]
    )
]

setup(
    name="aynime_capture",
    version="0.1.0",
    description="Windows desktop capture library",
    ext_modules=ext_modules,
    cmdclass={
        "build_ext": build_ext
    },
    zip_safe=False,
    packages=["aynime_capture"],
    package_data={
        "aynime_capture": ["py.typed"]
    }
)
