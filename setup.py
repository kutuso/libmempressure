from setuptools import Extension, setup

setup(
    ext_modules=[
        Extension(
            "mempressure",
            sources=[
                "src/mempressure.c",
                "bindings/python/mempressure_module.c",
            ],
            include_dirs=["include"],
            define_macros=[("_GNU_SOURCE", "1")],
        )
    ]
)
