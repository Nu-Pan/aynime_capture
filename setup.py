
from setuptools import setup
from pybind11.setup_helpers import Pybind11Extension, build_ext

ext_modules = [
    Pybind11Extension(
        "aynime_capture",
        [
            "aynime_capture/aynime_capture.cpp",
            "aynime_capture/utils.cpp",
            "aynime_capture/stdafx.cpp",
            "aynime_capture/wgc_system.cpp",
            "aynime_capture/wgc_session.cpp"
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
    cmdclass={"build_ext": build_ext},
    zip_safe=False,
    packages=[]
)
