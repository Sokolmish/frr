#!/usr/bin/env python
# SPDX-License-Identifier: ISC

# Copyright (c) 2024 by
# Mikhail Sokolovskiy <sokolmish@gmail.com>
#

"""
test_bfd_topo4.py: Test the --listenon bfdd option.

Each router has one link per a connected switch and several addresses on them.
Some addresses on R1 and R2 are not listened by bfdd, so sessions over them
shouldn't work.

BFD sessions:
R1-R2:  10.1.0.2     10.1.0.12
        10.1.0.3     10.1.0.13   (X)

R2-R3:  fd00:3::2    fd00:3::12
        fd00:3::3    fd00:3::13  (X)

R1-R3:  10.1.0.2     10.3.0.2
(mhop)  10.1.0.3     10.3.0.3    (X)
        fd00:1::2    fd00:3::2
        fd00:1::3    fd00:3::3   (X)

+--------------------+
|         R1         |   10.3.0.0/24 via 10.1.0.12
| 10.1.0.2      -l   |   fd00:3::/64 via fd00:1::1
| 10.1.0.3           |----------------+
| fd00:1::2     -l   |eth0            |
| fd00:1::3          |        +---------------+
+--------------------+        |      SW1      |
                              | 10.1.0.0/24   |
+--------------------+        | fd00:1::/64   |
|         R2         |        +---------------+
| 10.1.0.12    |     |eth0            |
| 10.1.0.13    |=====|----------------+
| fd00:1::1    |     |
| 10.3.0.1       |   |
| fd00:3::12     |===|----------------+
| fd00:3::13     |   |eth1            |
+--------------------+        +---------------+
                              |      SW1      |
+--------------------+        | 10.3.0.0/24   |
|         R1         |        | fd00:3::/64   |
| 10.3.0.2       -l  |        +---------------+
| 10.3.0.3           |eth0            |
| fd00:3::2      -l  |----------------+
| fd00:3::3          |   10.1.0.0/24 via 10.3.0.1
+--------------------+   fd00:1::/64 via fd00:3::12
"""

import os
import sys
import json
from functools import partial
import pytest

# Save the Current Working Directory to find configuration files.
CWD = os.path.dirname(os.path.realpath(__file__))
sys.path.append(os.path.join(CWD, "../"))

# pylint: disable=C0413
# Import topogen and topotest helpers
from lib import topotest
from lib.topogen import Topogen, TopoRouter, get_topogen
from lib.topolog import logger

pytestmark = [pytest.mark.bfdd]


LISTEN_ADDRESSES = {
    "r1": ["10.1.0.2", "fd00:1::2"],
    "r2": ["0.0.0.0", "::"],
    "r3": ["fd00:3::2", "10.3.0.2"],
}


def setup_module(mod):
    "Sets up the pytest environment"
    topodef = {
        "s1": ("r1", "r2"),
        "s2": ("r2", "r3"),
    }
    tgen = Topogen(topodef, mod.__name__)
    tgen.start_topology()

    router_list = tgen.routers()
    for rname, router in router_list.items():
        tgen.net[rname].daemons_options["bfdd"] = "-l " + " -l ".join(
            LISTEN_ADDRESSES[rname]
        )

        daemon_file = "{}/{}/bfdd.conf".format(CWD, rname)
        if os.path.isfile(daemon_file):
            router.load_config(TopoRouter.RD_BFD, daemon_file)

        daemon_file = "{}/{}/zebra.conf".format(CWD, rname)
        if os.path.isfile(daemon_file):
            router.load_config(TopoRouter.RD_ZEBRA, daemon_file)

        daemon_file = "{}/{}/staticd.conf".format(CWD, rname)
        if os.path.isfile(daemon_file):
            router.load_config(TopoRouter.RD_STATIC, daemon_file)

    # Since bind() might be done before the Zebras configuration is applied,
    # adrresses must be assigned beforehand, so as not to get the "Cannot
    # assign requested address" error. Loading SNMP causes topogen to wait 2
    # seconds, what allows zebra to configure addresses on interfaces.  Oh...
    router_list["r1"].load_config(TopoRouter.RD_SNMP)
    router_list["r2"].load_config(TopoRouter.RD_SNMP)
    router_list["r3"].load_config(TopoRouter.RD_SNMP)

    tgen.start_router()


def test_wait_bfd_convergence():
    "Wait for BFD to converge"
    tgen = get_topogen()
    if tgen.routers_have_failure():
        pytest.skip(tgen.errors)

    logger.info("test BFD configurations")

    r1 = tgen.gears["r1"]
    r2 = tgen.gears["r2"]
    r3 = tgen.gears["r3"]

    # Why staticd can't do it?
    r1.cmd_raises("ip r add 10.3.0.0/24 via 10.1.0.12")
    r1.cmd_raises("ip r add fd00:3::/64 via fd00:1::1")
    r3.cmd_raises("ip r add 10.1.0.0/24 via 10.3.0.1")
    r3.cmd_raises("ip r add fd00:1::/64 via fd00:3::12")

    def expect_bfd_configuration(router):
        "Load JSON file and compare with 'show bfd peer json'"
        logger.info("waiting BFD configuration on router {}".format(router))
        bfd_config = json.loads(open("{}/{}/bfd-peers.json".format(CWD, router)).read())
        test_func = partial(
            topotest.router_json_cmp,
            tgen.gears[router],
            "show bfd peers json",
            bfd_config,
        )
        _, result = topotest.run_and_expect(test_func, None, count=30, wait=1)  # 200

        assertmsg = '"{}" BFD configuration failure'.format(router)
        assert result is None, assertmsg

    expect_bfd_configuration("r1")
    expect_bfd_configuration("r2")
    expect_bfd_configuration("r3")


def teardown_module(_mod):
    "Teardown the pytest environment"
    tgen = get_topogen()
    tgen.stop_topology()


def test_memory_leak():
    "Run the memory leak test and report results."
    tgen = get_topogen()
    if not tgen.is_memleak_enabled():
        pytest.skip("Memory leak test/report is disabled")

    tgen.report_memory_leaks()


if __name__ == "__main__":
    args = ["-s"] + sys.argv[1:]
    sys.exit(pytest.main(args))
