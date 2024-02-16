.. _mozilla_projects_nss_releases:

Releases
========

.. toctree::
   :maxdepth: 0
   :glob:
   :hidden:

   nss_3_98.rst
   nss_3_97.rst
   nss_3_96_1.rst
   nss_3_96.rst
   nss_3_95.rst
   nss_3_94.rst
   nss_3_93.rst
   nss_3_92.rst
   nss_3_91_0.rst
   nss_3_90_0.rst
   nss_3_89_1.rst
   nss_3_89.rst
   nss_3_88_1.rst
   nss_3_88.rst
   nss_3_87_1.rst
   nss_3_87.rst
   nss_3_86.rst
   nss_3_85.rst
   nss_3_84.rst
   nss_3_83.rst
   nss_3_82.rst
   nss_3_81.rst
   nss_3_80.rst
   nss_3_79_4.rst
   nss_3_79_3.rst
   nss_3_79_2.rst
   nss_3_79_1.rst
   nss_3_79.rst
   nss_3_78_1.rst
   nss_3_78.rst
   nss_3_77.rst
   nss_3_76_1.rst
   nss_3_76.rst
   nss_3_75.rst
   nss_3_74.rst
   nss_3_73_1.rst
   nss_3_73.rst
   nss_3_72_1.rst
   nss_3_72.rst
   nss_3_71.rst
   nss_3_70.rst
   nss_3_69_1.rst
   nss_3_69.rst
   nss_3_68_4.rst
   nss_3_68_3.rst
   nss_3_68_2.rst
   nss_3_68_1.rst
   nss_3_68.rst
   nss_3_67.rst
   nss_3_66.rst
   nss_3_65.rst
   nss_3_64.rst

.. note::

   **NSS 3.98** is the latest version of NSS.
   Complete release notes are available here: :ref:`mozilla_projects_nss_nss_3_98_release_notes`

   **NSS 3.90.2 (ESR)** is the latest version of NSS.
   Complete release notes are available here: :ref:`mozilla_projects_nss_nss_3_90_2_release_notes`

.. container::

   Changes in 3.98 included in this release:

  - Bug 1780432 - (CVE-2023-5388) Timing attack against RSA decryption in TLS.
  - Bug 1879513 - Certificate Compression: enabling the check that the compression was advertised.
  - Bug 1831552 - Move Windows workers to nss-1/b-win2022-alpha.
  - Bug 1879945 - Remove Email trust bit from OISTE WISeKey Global Root GC CA.
  - Bug 1877344 - Replace `distutils.spawn.find_executable` with `shutil.which` within `mach` in `nss`.
  - Bug 1548723 - Certificate Compression: Updating nss_bogo_shim to support Certificate compression.
  - Bug 1548723 - TLS Certificate Compression (RFC 8879) Implementation.
  - Bug 1875356 - Add valgrind annotations to freebl kyber operations for constant-time execution tests.
  - Bug 1870673 - Set nssckbi version number to 2.66.
  - Bug 1874017 - Add Telekom Security roots.
  - Bug 1873095 - Add D-Trust 2022 S/MIME roots.
  - Bug 1865450 - Remove expired Security Communication RootCA1 root.
  - Bug 1876179 - move keys to a slot that supports concatenation in PK11_ConcatSymKeys.
  - Bug 1876800 - remove unmaintained tls-interop tests.
  - Bug 1874937 - bogo: add support for the -ipv6 and -shim-id shim flags.
  - Bug 1874937 - bogo: add support for the -curves shim flag and update Kyber expectations.
  - Bug 1874937 - bogo: adjust expectation for a key usage bit test.
  - Bug 1757758 - mozpkix: add option to ignore invalid subject alternative names.
  - Bug 1841029 - Fix selfserv not stripping `publicname:` from -X value.
  - Bug 1876390 - take ownership of ecckilla shims.
  - Bug 1874458 - add valgrind annotations to freebl/ec.c.
  - Bug  864039 - PR_INADDR_ANY needs PR_htonl before assignment to inet.ip.
  - Bug 1875965 - Update zlib to 1.3.1.

