
from setuptools import setup
from pybind11.setup_helpers import Pybind11Extension, build_ext

ext_modules = [
    Pybind11Extension(
        "aynime_capture._aynime_capture",
        [
            "core/source/async_texture_readback.cpp",
            "core/source/core.cpp",
            "core/source/frame_buffer.cpp",
            "core/source/stdafx.cpp",
            "core/source/utils.cpp",
            "core/source/wgc_session.cpp",
            "core/source/wgc_system.cpp"
        ],
        include_dirs=[
            "core/include"
        ],
        libraries=[
            "d3d11",
            "dxgi",
            "WindowsApp"
        ],
        cxx_std=20,
        extra_compile_args=[
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
