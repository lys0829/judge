import os
from distutils.core import setup,Extension

os.environ['CC'] = 'clang'
os.environ['CFLAGS'] = '-Wno-unused-result'

pyext_module = Extension('pyext',
        sources = [
            'src/pyext.c',
            'src/contro.c',
            'src/fog.c',
            'src/snap.c',
            'src/task.c',
            'src/timer.c',
            'src/ev.c',
            'src/io.c'
        ],
        include_dirs = ['include'],
        libraries = ['archive'])

setup(name = 'pyext',
        version = '1.0',
        description = 'PyExt package',
        ext_modules = [pyext_module])