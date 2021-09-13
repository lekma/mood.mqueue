# -*- coding: utf-8 -*-

#
# Copyright © 2021 Malek Hadj-Ali
# All rights reserved.
#
# This file is part of mood.
#
# mood is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 3
# as published by the Free Software Foundation.
#
# mood is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with mood.  If not, see <http://www.gnu.org/licenses/>.
#


from setuptools import setup, find_packages, Extension

from codecs import open
from os.path import abspath


pkg_name = "mood.mqueue"
pkg_version = "1.5.1"
pkg_desc = "Python POSIX message queues interface (Linux only)"

PKG_VERSION = ("PKG_VERSION", "\"{0}\"".format(pkg_version))


setup(
      name=pkg_name,
      version=pkg_version,
      description=pkg_desc,
      long_description=open(abspath("README.txt"), encoding="utf-8").read(),
      long_description_content_type="text",

      url="https://github.com/lekma/mood.mqueue",
      download_url="https://github.com/lekma/mood.mqueue/releases",
      project_urls={
          "Bug Tracker": "https://github.com/lekma/mood.mqueue/issues"
      },
      author="Malek Hadj-Ali",
      author_email="lekmalek@gmail.com",
      license="GNU General Public License v3 (GPLv3)",
      platforms=["Linux"],
      keywords="linux mqueue",

      setup_requires = ["setuptools>=24.2.0"],
      python_requires="~=3.8",
      packages=find_packages(),
      namespace_packages=["mood"],
      zip_safe=False,

      ext_package="mood",
      ext_modules=[
          Extension(
                    "mqueue",
                    [
                     "src/helpers/helpers.c",
                     "src/mqueue.c"
                    ],
                    define_macros=[PKG_VERSION],
                    libraries=["rt"]
                   )
      ],

      classifiers=[
          "Development Status :: 5 - Production/Stable",
          "Intended Audience :: Developers",
          "License :: OSI Approved :: GNU General Public License v3 (GPLv3)",
          "Operating System :: POSIX :: Linux",
          "Programming Language :: Python :: 3.8",
          "Programming Language :: Python :: Implementation :: CPython"
      ]
)

