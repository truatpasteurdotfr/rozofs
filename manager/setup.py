#!/usr/bin/python
# -*- coding: utf-8 -*-

from distutils.core import setup
from distutils.extension import Extension

setup(name='rozo',
      version='${PACKAGE_VERSION}',
      author='Fizians S.A.S.',
      author_email='rd@fizians.com',
      url='http://www.fizians.com/',
      description='Rozofs Management tools',
      license='Copyright Fizians S.A.S.',
      packages=['ext', 'rozofs', 'rozofs.core', 'rozofs.cli'],
      ext_modules=[Extension('_config', ['rozofs/core/libconfig.i'],
                             swig_opts=['-I/usr/include'],
                             libraries=['config']),
                   Extension('_profile', ['rozofs/core/profile.i'],
                             swig_opts=['-I/usr/include', '-I../../..'],
                             include_dirs=['../../..', '../../../build/release'],
                             libraries=['rozofs'],
                             library_dirs=['../../../build/release/rozofs'])
                   ],
      py_modules=['libconfig', 'profile'],
      scripts=['bin/rozo', ],
)
